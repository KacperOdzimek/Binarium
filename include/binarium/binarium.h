/*
----------------------------------------------------------------
Contents:
This file provides a builder and zero-copy view API for the Binarium V0 format.

----------------------------------------------------------------
Code info:
- sht prefix
- BINARIUM_IMPL macro to build
- libzstd >= 1.5.5 dependant

----------------------------------------------------------------
Usage:
- biu_view:
    - create viewer over existing binarium file - you can use mmaped file memory for zero copy read
    - query entires types, read entries values
    - free view
- biu_builder:
    - build document by adding entries (ensure each have unique name, see notes)
    - serialize document, and save to file
    - free builder

----------------------------------------------------------------
Thread safety:
- biu_view is safe for concurrent read-only access after creation
- biu_builder is NOT thread-safe while being modified

----------------------------------------------------------------
Notes:
- Duplicate names error is NOT returned on builder add operations, since this would require
    checking other added names, possibly making add operations time complexity worse. Therefore
    the error is only returned at biu_builder_serialize. This means, adding two entries with same name
    invalidates the builder. This is not a case worth improving, since it is easy to ensure no duplicates on caller side.
- This api uses null-terminated strings for convenience (binarium files use explicit lengths).
- API requires all strings given to "name" fields to be valid.
- When reading compressed files from unknown sources, it is caller responsibility 
    to put a sane limit on uncompressed buffer allocation size.
*/

#ifndef BINARIUM_H
#define BINARIUM_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

// ===========================
// Data Types

typedef enum biu_type {
    biu_type_int64              = 0,
    biu_type_float64            = 1,
    biu_type_text               = 2,
    biu_type_binary             = 3,
    biu_type_text_compressed    = 4,
    biu_type_binary_compressed  = 5
} biu_type;

// ===========================
// Status Codes

typedef enum biu_status {
    biu_status_ok = 0,                  // Operation succeeded
    biu_status_err_bad_file,            // File is malformed
    biu_status_err_bad_entry,           // Entry of index does not exist
    biu_status_err_bad_version,         // Viewed file has version != 0
    biu_status_err_bad_output,          // Output pointer is NULL
    biu_status_err_allocation_failure,  // Failed to allocate
    biu_status_err_duplicate_names,     // Two entries with same name were added
    biu_status_err_not_found,           // Entry of name was not found
    biu_status_err_type_mismatch,       // Bad get operation, entry is not of this type
    biu_status_err_buffer_too_small,    // Buffer is to small to hold decompressed data
    biu_status_err_name_too_long,       // Given name is longer than 255 characters
    biu_status_err_too_many_entries,    // Index entries count * entry sizeof overflows header index bytes
} biu_status;

// ===========================
// Memory access

typedef enum biu_access {
    biu_access_make_copy,               // Given memory is copied, and the copy is freed with the object
    biu_access_read_unowned,            // Given memory is read but is not freed with the object
    biu_access_claim_ownership,         // Given memory is claimed by API, and freed with the object
} biu_access;

// ===========================
// Viewer

typedef struct biu_view_create_info {
    biu_access  access;     // Buffer access profile
    const void* buffer;     // File data buffer
    uint64_t    bytes;      // File data bytes
} biu_view_create_info;

typedef struct biu_view biu_view;
biu_status biu_create_view(biu_view** target, const biu_view_create_info* info);
void biu_free_view(biu_view*);

// Queries entries count
biu_status biu_view_query_entry_count(
    biu_view*, uint32_t* count
);

// Queries entry type at index
biu_status biu_view_query_entry_type(
    biu_view*, uint32_t entry, biu_type* out_type
);

// Queries entry at index name
// Returns pointer to in-file name, zero-copy
// Name is not null-terminated!
biu_status biu_view_query_name(
    biu_view*, uint32_t entry, uint8_t* out_bytes, const char** out_name
);

// Binary search entry by name
biu_status biu_view_find(
    biu_view*, const char* name, uint32_t* out
);

// ===========================
// Viewer Gets
// Pick version based on entry type

biu_status biu_view_get_as_int64(
    biu_view*, uint32_t entry, int64_t* out
);

biu_status biu_view_get_as_float64(
    biu_view*, uint32_t entry, double* out
);

// Zero-copy: hands back a pointer into the original buffer
biu_status biu_view_get_as_bytes(
    biu_view*, uint32_t entry, uint64_t* out_bytes, const uint8_t* *out_ptr
);

// Size the output buffer for BINARIUM_entry_decompress
// See note on limiting uncompressed size
biu_status biu_view_get_as_uncompressed_size(
    biu_view*, uint32_t entry, uint64_t* out_bytes
);

// Decompresses a text_compressed / binary_compressed entry into a caller-supplied buffer.
biu_status biu_view_get_as_decompress(
    biu_view*, uint32_t entry, uint64_t out_buf_size, void* out_buf
);

// ===========================
// Builder

typedef struct biu_builder biu_builder;
biu_status biu_create_builder(biu_builder** target);
void biu_free_builder(biu_builder*);

// ===========================
// Builder add operations
// Names in following function calls are copied right-away

biu_status biu_builder_add_int64(
    biu_builder*, const char* name, int64_t value
);

biu_status biu_builder_add_float64(
    biu_builder*, const char* name, double value
);

biu_status biu_builder_add_text(
    biu_builder*, const char* name, uint64_t bytes, const char* text, biu_access access
);

biu_status biu_builder_add_binary(
    biu_builder*, const char* name, uint64_t bytes, const void* data, biu_access access
);

// Level = zstd compression level; pass <= 0 for sane default
biu_status biu_builder_add_text_compressed(
    biu_builder*, const char* name, uint64_t bytes, const char* text, int level
);

// Level = zstd compression level; pass <= 0 for sane default
biu_status biu_builder_add_binary_compressed(
    biu_builder*, const char* name, uint64_t bytes, const void* data, int level
);

// ===========================
// Builder Serialization

// Serializes to a a new buffer; caller must free(*out_buffer) if status was biu_status_ok
biu_status biu_builder_serialize(
    biu_builder*, void** out_buffer, uint64_t* out_bytes
);

// ===========================
// Little endian reads

static inline uint32_t biu_read_u32(const void* p) {
    const uint8_t* b = (const uint8_t*)p;
    return  (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
}
 
static inline uint64_t biu_read_u64(const void* p) {
    const uint8_t* b = (const uint8_t*)p; uint64_t v = 0;
    for (int i = 7; i >= 0; i--) v = (v << 8) | (uint64_t)b[i];
    return v;
}

static inline int64_t biu_read_i64(const void* p) {
    const uint8_t* b = (const uint8_t*)p; uint64_t v = 0;
    for (int i = 7; i >= 0; i--) v = (v << 8) | (uint64_t)b[i];
    return (int64_t)v;
}

static inline double biu_read_double(const void* p) {
    const uint8_t* b = (const uint8_t*)p; uint64_t bits = 0;
    for (int i = 7; i >= 0; i--) bits = (bits << 8) | (uint64_t)b[i];
    double value; memcpy(&value, &bits, sizeof(value));
    return value;
}

// ===========================
// Little endian writes

static inline void biu_write_u32(uint8_t* dst, uint64_t* pos, uint32_t value) {
    for (int i = 0; i < 4; ++i) dst[*pos + i] = (uint8_t)(value >> (8 * i)); *pos += 4;
}

static inline void biu_write_u64(uint8_t* dst, uint64_t* pos, uint64_t value) {
    for (int i = 0; i < 8; ++i) dst[*pos + i] = (uint8_t)(value >> (8 * i)); *pos += 8;
}

static inline void biu_write_i64(uint8_t* result, uint64_t* position, int64_t value) {
    uint64_t bits; memcpy(&bits, &value, sizeof(bits));
    biu_write_u64(result, position, bits);
}

static inline void biu_write_double(uint8_t* result, uint64_t* position, double value) {
    uint64_t bits; memcpy(&bits, &value, sizeof(bits));
    biu_write_u64(result, position, bits);
}

static inline void biu_write_bytes(uint8_t* result, uint64_t* position, const void* src, uint64_t size) {
    memcpy(result + *position, src, size); *position += size;
}

#endif /* BINARIUM_H */

#ifdef BINARIUM_IMPL

// ===========================
// Depedency

#include <stdlib.h>
#include <zstd.h>

// ===========================
// Asssert

#include <float.h>

#if DBL_MANT_DIG != 53
#error "Requires IEEE-754 binary64 double"
#endif

#if DBL_MAX_EXP != 1024
#error "Requires IEEE-754 binary64 double"
#endif

// Assert on 32 bit machines, binarium requires 64 bit.
typedef char static_assert_pointer_size[(sizeof(void*) == 8) ? 1 : -1];

// ===========================
// Constant

#define HEADER_BYTES 32u
#define LAST_TYPE biu_type_binary_compressed
static const uint8_t MAGIC_VALUE[8] = {
    'B','I','N','A','R','I','U','M'
};

// ===========================
// Helpers

// Align up 8 bytes
static inline uint64_t align8(uint64_t x) {
    return (x + 7ull) & ~7ull;
}

// Compare Ops
// Lexicographics name comparison
static int name_comp(uint8_t a_length, const uint8_t* a, uint8_t b_length, const uint8_t* b) {
    uint8_t min_length = a_length < b_length ? a_length : b_length;

    int cmp = memcmp(a, b, min_length);
    if (cmp != 0) return cmp;

    if (a_length < b_length) return -1;
    if (a_length > b_length) return 1;
    return 0;
}

// ===========================
// Viewer

struct biu_view {
    biu_access  access;         // Buffer access

    const void* buffer;         // File data buffer
    uint64_t    bytes;          // File data length

    uint32_t    index_count;    // Number of index entries
    uint64_t    index_begin;    // Always 32 (sizeof header)

    uint64_t    names_begin;    // Byte offset of names array from buffer begin
    uint32_t    names_bytes;    // Names array size in bytes

    uint64_t    content_begin;  // Byte offset of content section from buffer begin
    uint64_t    content_bytes;  // Content section size in bytes
};
 
// View entry as per spec
typedef struct view_entry {
    uint32_t    name_offset;
    uint32_t    type;
    uint64_t    data_bytes;
    uint64_t    data_offset;
} view_entry;

// With assumption entry exists
static inline view_entry view_read_index(const biu_view* viewer, uint32_t entry) {
    const uint8_t* p = (const uint8_t*)viewer->buffer + viewer->index_begin + (uint64_t)entry * 24;
    view_entry out = {
        .name_offset = biu_read_u32(p + 0),
        .type        = biu_read_u32(p + 4),
        .data_bytes  = biu_read_u64(p + 8),
        .data_offset = biu_read_u64(p + 16)
    };
    return out;
}
 
// Gets entry's at index name
// Returns 0 on out-of-bounds (corrupt file), 1 on success.
// With assumption entry exists
static inline int view_get_entry_name(const biu_view* viewer, uint32_t entry, const uint8_t** out_ptr, uint8_t* out_bytes) {
    view_entry idx = view_read_index(viewer, entry);
    if ((uint64_t)idx.name_offset >= viewer->names_bytes) return 0;
 
    const uint8_t* name_ptr = (const uint8_t*)viewer->buffer + viewer->names_begin + idx.name_offset;
    uint8_t length = name_ptr[0];
    if ((uint64_t)idx.name_offset + 1 + length > viewer->names_bytes) return 0;
 
    *out_bytes = length;
    *out_ptr = name_ptr + 1;
    return 1;
}
 
// Checks [data_offset, data_offset + data_bytes) is in view bounds
static inline int view_data_in_bounds(const biu_view* viewer, uint64_t data_offset, uint64_t data_bytes) {
    if (data_offset > viewer->content_bytes) return 0;
    uint64_t end = data_offset + data_bytes;
    if (end < data_offset) return 0; // overflow
    if (end > viewer->content_bytes) return 0;
    return 1;
}
 
biu_status biu_create_view(biu_view** target, const biu_view_create_info* info) {
    if (!target || !info || !info->buffer) return biu_status_err_bad_file;
    if (info->bytes < HEADER_BYTES) return biu_status_err_bad_file;
 
    const uint8_t* buf = (const uint8_t*)info->buffer;
 
    // Magic
    if (memcmp(buf, MAGIC_VALUE, 8) != 0) return biu_status_err_bad_file;
 
    // Version - only version 0 is implemented
    uint64_t version = biu_read_u64(buf + 8);
    if (version != 0) return biu_status_err_bad_version;
 
    uint32_t index_bytes   = biu_read_u32(buf + 16);
    uint32_t names_bytes   = biu_read_u32(buf + 20);
    uint64_t content_bytes = biu_read_u64(buf + 24);
 
    // Index entries are fixed 24 bytes each
    if (index_bytes % 24 != 0) return biu_status_err_bad_file;
 
    uint64_t names_begin = (uint64_t)HEADER_BYTES + index_bytes;
    if (names_begin < index_bytes) return biu_status_err_bad_file;      // overflow guard
 
    uint64_t content_begin = align8(names_begin + names_bytes);
    if (content_begin < names_begin) return biu_status_err_bad_file;    // overflow guard
 
    uint64_t total_size = content_begin + content_bytes;
    if (total_size < content_begin) return biu_status_err_bad_file;     // overflow guard
    if (total_size > (uint64_t)info->bytes) return biu_status_err_bad_file;
 
    *target = malloc(sizeof(biu_view));
    if (!(*target)) return biu_status_err_allocation_failure;
 
    biu_view* view = *target;
    *view = (biu_view){
        .access         = info->access,
        .bytes          = info->bytes,
        .index_count    = index_bytes / 24,
        .index_begin    = HEADER_BYTES,
        .names_begin    = names_begin,
        .names_bytes    = names_bytes,
        .content_begin  = content_begin,
        .content_bytes  = content_bytes
    };
 
    if (info->access == biu_access_make_copy) {
        void* copy = malloc(info->bytes);
        if (!copy) {
            free(view); *target = NULL;
            return biu_status_err_allocation_failure;
        }
        memcpy(copy, info->buffer, info->bytes);
        view->buffer = copy;
    } 
    else {
        view->buffer = info->buffer;
    }
 
    return biu_status_ok;
}
 
void biu_free_view(biu_view* viewer) {
    if (!viewer) return;
    if (viewer->access != biu_access_read_unowned) {
        free((void*)viewer->buffer);
    }
    free(viewer);
}
 
// Queries entries count
biu_status biu_view_query_entry_count(biu_view* viewer, uint32_t* count) {
    if (!count) return biu_status_err_bad_output;
    *count = viewer->index_count;
    return biu_status_ok;
}
 
// Queries entry type at index
biu_status biu_view_query_entry_type(biu_view* viewer, uint32_t entry, biu_type* out_type) {
    if (!out_type)                      return biu_status_err_bad_output;
    if (entry >= viewer->index_count)   return biu_status_err_bad_entry;
 
    view_entry idx = view_read_index(viewer, entry);
    if (idx.type > LAST_TYPE) return biu_status_err_bad_file;
    *out_type = (biu_type)idx.type;

    return biu_status_ok;
}

biu_status biu_view_query_name(
    biu_view* viewer, uint32_t entry, uint8_t* out_bytes, const char** out_name
) {
    if (!out_bytes | !out_name)         return biu_status_err_bad_output;
    if (entry >= viewer->index_count)   return biu_status_err_bad_entry;

    if (!view_get_entry_name(viewer, entry, (const uint8_t**)out_name, out_bytes)) return biu_status_err_bad_file;
    return biu_status_ok;
}
 
// Binary search entry by name (index is guaranteed sorted lexicographically per spec)
biu_status biu_view_find(biu_view* viewer, const char* name, uint32_t* out) {
    if (!name) return biu_status_err_not_found;
    if (!out)  return biu_status_err_bad_output;

    size_t length = strlen(name);
    if (length > 255) return biu_status_err_name_too_long;

    uint32_t lo = 0, hi = viewer->index_count;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2;
        const uint8_t* mid_name; uint8_t mid_len;

        if (!view_get_entry_name(viewer, mid, &mid_name, &mid_len)) return biu_status_err_bad_file;
        int cmp = name_comp(mid_len, mid_name, (uint8_t)length, (const uint8_t*)name);

        if (cmp == 0) {
            *out = mid;  return biu_status_ok;
        }

        if (cmp < 0) lo = mid + 1;
        else         hi = mid;
    }
 
    return biu_status_err_not_found;
}

biu_status biu_view_get_as_int64(biu_view* viewer, uint32_t entry, int64_t* out) {
    if (!out)                           return biu_status_err_bad_output;       // Ensure out
    if (entry >= viewer->index_count)   return biu_status_err_bad_entry;        // Ensure entry
 
    view_entry idx = view_read_index(viewer, entry);
    if (idx.type != biu_type_int64)     return biu_status_err_type_mismatch;        // Ensure type
    if (idx.data_bytes != 8)            return biu_status_err_bad_file;             // Ensure size
    if (!view_data_in_bounds(viewer, idx.data_offset, idx.data_bytes)) return biu_status_err_bad_file;  // Ensure bounds
 
    const uint8_t* p = (const uint8_t*)viewer->buffer + viewer->content_begin + idx.data_offset;
    *out = biu_read_i64(p); return biu_status_ok;
}
 
biu_status biu_view_get_as_float64(biu_view* viewer, uint32_t entry, double* out) {
    if (!out)                           return biu_status_err_bad_output;      // Ensure out
    if (entry >= viewer->index_count)   return biu_status_err_bad_entry;        // Ensure entry
 
    view_entry idx = view_read_index(viewer, entry);
    if (idx.type != biu_type_float64)   return biu_status_err_type_mismatch;    // Ensure type
    if (idx.data_bytes != 8)            return biu_status_err_bad_file;         // Ensure size
    if (!view_data_in_bounds(viewer, idx.data_offset, idx.data_bytes)) return biu_status_err_bad_file;  // Ensure bounds
 
    const uint8_t* p = (const uint8_t*)viewer->buffer + viewer->content_begin + idx.data_offset;
    *out = biu_read_double(p); return biu_status_ok;
}
 
// Zero-copy: hands back a pointer straight into the source buffer, no allocation
biu_status biu_view_get_as_bytes(biu_view* viewer, uint32_t entry, uint64_t* out_bytes, const uint8_t** out_ptr) {
    if (!out_bytes || !out_ptr)         return biu_status_err_bad_output;      // Ensure out
    if (entry >= viewer->index_count)   return biu_status_err_bad_entry;        // Ensure entry
 
    view_entry idx = view_read_index(viewer, entry);
    if (idx.type != biu_type_text && idx.type != biu_type_binary)       return biu_status_err_type_mismatch;    // Ensure type
    if (!view_data_in_bounds(viewer, idx.data_offset, idx.data_bytes))  return biu_status_err_bad_file;         // Ensure bounds
 
    *out_ptr = (const uint8_t*)viewer->buffer + viewer->content_begin + idx.data_offset;
    *out_bytes = idx.data_bytes; return biu_status_ok;
}
 
// Reads the uncompressed size prefix without decompressing anything
biu_status biu_view_get_as_uncompressed_size(biu_view* viewer, uint32_t entry, uint64_t* out_bytes) {
    if (!out_bytes)                     return biu_status_err_bad_output;  // Ensure out
    if (entry >= viewer->index_count)   return biu_status_err_bad_entry;    // Ensure entry
 
    view_entry idx = view_read_index(viewer, entry);
    if (idx.type != biu_type_text_compressed && idx.type != biu_type_binary_compressed) return biu_status_err_type_mismatch;    // Ensure type
    if (idx.data_bytes < 8)                                                             return biu_status_err_bad_file;         // Ensure size
    if (!view_data_in_bounds(viewer, idx.data_offset, idx.data_bytes))                  return biu_status_err_bad_file;         // Ensure bounds
 
    const uint8_t* p = (const uint8_t*)viewer->buffer + viewer->content_begin + idx.data_offset;
    *out_bytes = biu_read_u64(p); return biu_status_ok;
}
 
// Decompresses fresh into the caller-supplied buffer every call - result is never cached
biu_status biu_view_get_as_decompress(biu_view* viewer, uint32_t entry, uint64_t out_buf_size, void* out_buf) {
    if (!out_buf)                       return biu_status_err_bad_output;  // Ensure out
    if (entry >= viewer->index_count)   return biu_status_err_bad_entry;    // Ensure entry
 
    view_entry idx = view_read_index(viewer, entry);
    if (idx.type != biu_type_text_compressed && idx.type != biu_type_binary_compressed) return biu_status_err_type_mismatch;    // Ensure type
    if (idx.data_bytes < 8)                                                             return biu_status_err_bad_file;         // Ensure size
    if (!view_data_in_bounds(viewer, idx.data_offset, idx.data_bytes))                  return biu_status_err_bad_file;         // Ensure bounds
 
    const uint8_t* p = (const uint8_t*)viewer->buffer + viewer->content_begin + idx.data_offset;
    uint64_t uncompressed_size = biu_read_u64(p); const uint8_t* compressed_data  = p + 8; uint64_t compressed_bytes = idx.data_bytes - 8;
 
    if (out_buf_size < uncompressed_size) return biu_status_err_buffer_too_small;   // Ensure out buffer size
    size_t written = ZSTD_decompress(out_buf, (size_t)out_buf_size, compressed_data, (size_t)compressed_bytes);
    if (ZSTD_isError(written) || (uint64_t)written != uncompressed_size) return biu_status_err_bad_file;    // Check decompression succeeded
 
    return biu_status_ok;
}

// ===========================
// Builder

typedef struct builder_entry {
    uint8_t             name_length;
    uint8_t             name[255];
    biu_type            type;
    union {
        int64_t         int64;
        double          float64;
        struct {
            int         dofree;
            uint64_t    uncompressed;
            uint64_t    length;
            const void* begin;
        } data_block;
    } value;
} builder_entry;

struct biu_builder {
    // Unsorted entries dynamic array
    uint32_t        entries_count;
    uint32_t        entries_capacity;
    builder_entry*  entries;
};

biu_status biu_create_builder(biu_builder** target) {
    if (!target) return biu_status_err_bad_output;
    *target = calloc(1, sizeof(biu_builder));
    if (!(*target)) return biu_status_err_allocation_failure;
    return biu_status_ok;
}

static inline int no_free_type(biu_type type) {
    if (type == biu_type_int64 || type == biu_type_float64) return 1;
    return 0;
}

void biu_free_builder(biu_builder* builder) {
    if (!builder) return;
    for (uint32_t i = 0; i < builder->entries_count; i++) {
        builder_entry* entry = &builder->entries[i];
        if (no_free_type(entry->type))       continue;
        if (!entry->value.data_block.dofree) continue;
        free((void*)entry->value.data_block.begin);
    }
    free(builder->entries);
    free(builder);
}

static inline biu_status link_data_block(builder_entry* entry, uint64_t bytes, const void* data, biu_access access) {
    switch (access) {
    case biu_access_claim_ownership: {
        entry->value.data_block.dofree  = 1;
        entry->value.data_block.length  = bytes;
        entry->value.data_block.begin   = data;
    } break;
    case biu_access_read_unowned: {
        entry->value.data_block.dofree  = 0;
        entry->value.data_block.length  = bytes;
        entry->value.data_block.begin   = data;
    } break;
    case biu_access_make_copy: {
        entry->value.data_block.dofree  = 1;
        entry->value.data_block.length  = bytes;
        entry->value.data_block.begin   = malloc(bytes);
        if (!entry->value.data_block.begin) return biu_status_err_allocation_failure;
        memcpy((void*)entry->value.data_block.begin, data, bytes);
    } break;
    }

    return biu_status_ok;
}

static inline biu_status top_entry(biu_builder* builder, const char* name, builder_entry** out_entry) {
    if (builder->entries_count + 1 >= builder->entries_capacity) {
        uint32_t new_cap = builder->entries_capacity * 2; if (new_cap == 0) new_cap = 16;
        builder_entry* new_entries = realloc(builder->entries, new_cap * sizeof(builder_entry));
        if (!new_entries) return biu_status_err_allocation_failure; // Failure
        builder->entries            = new_entries;
        builder->entries_capacity   = new_cap;
    }

    size_t name_length = strlen(name);
    if (name_length > 255) return biu_status_err_name_too_long;

    builder_entry* entry = &builder->entries[builder->entries_count];
    *entry = (builder_entry){0};    // Initialize entry
    for (int i = 0; i < name_length; i++) {
        if (name[i] == '\0') break;
        entry->name[i] = name[i];
    } entry->name_length = name_length;

    *out_entry = entry; return biu_status_ok;
}

static inline void top_entry_add(biu_builder* builder) {
    builder->entries_count++; // Make top entry actuall entry
}

#define SAFE_GET_TOP_ENTRY() \
    do {biu_status s = top_entry(builder, name, &entry); if (s != biu_status_ok) return s;} while (0)

#define SAFE_ADD_SUCCESS() \
    do { top_entry_add(builder); return biu_status_ok; } while(0)

biu_status biu_builder_add_int64(
    biu_builder* builder, const char* name, int64_t value
) {
    builder_entry* entry; SAFE_GET_TOP_ENTRY();
    
    entry->type = biu_type_int64;
    entry->value.int64 = value;

    SAFE_ADD_SUCCESS();
}

biu_status biu_builder_add_float64(
    biu_builder* builder, const char* name, double value
) {
    builder_entry* entry; SAFE_GET_TOP_ENTRY();
    
    entry->type = biu_type_float64;
    entry->value.float64 = value;

    SAFE_ADD_SUCCESS();
}

biu_status biu_builder_add_text(
    biu_builder* builder, const char* name, uint64_t bytes, const char* text, biu_access access
) {
    builder_entry* entry; SAFE_GET_TOP_ENTRY();

    entry->type = biu_type_text;
    biu_status status = link_data_block(entry, bytes, text, access);
    if (status != biu_status_ok) return status;

    SAFE_ADD_SUCCESS();
}

biu_status biu_builder_add_binary(
    biu_builder* builder, const char* name, uint64_t bytes, const void* data, biu_access access
) {
    builder_entry* entry; SAFE_GET_TOP_ENTRY();

    entry->type = biu_type_binary;
    biu_status status = link_data_block(entry, bytes, data, access);
    if (status != biu_status_ok) return status;

    SAFE_ADD_SUCCESS();
}

biu_status biu_builder_add_text_compressed(
    biu_builder* builder, const char* name, uint64_t bytes, const char* text, int level
) {
    builder_entry* entry; SAFE_GET_TOP_ENTRY();

    if (level == 0) level = ZSTD_CLEVEL_DEFAULT;
    size_t bound = ZSTD_compressBound((size_t)bytes);

    void* compressed = malloc(bound);
    if (!compressed) {
        builder->entries_count--;
        return biu_status_err_allocation_failure;
    }

    size_t compressed_size = ZSTD_compress(compressed, bound, text, (size_t)bytes, level);
    if (ZSTD_isError(compressed_size)) {
        free(compressed);
        builder->entries_count--;
        return biu_status_err_allocation_failure;
    }

    // Shrink allocation
    void* tmp = realloc(compressed, compressed_size);
    if (tmp) compressed = tmp;

    entry->type = biu_type_text_compressed;
    entry->value.data_block.dofree          = 1;
    entry->value.data_block.uncompressed    = bytes;
    entry->value.data_block.begin           = compressed;
    entry->value.data_block.length          = compressed_size;

    SAFE_ADD_SUCCESS();
}

biu_status biu_builder_add_binary_compressed(
    biu_builder*    builder,
    const char*     name,
    uint64_t        bytes,
    const void*     data,
    int             level
) {
    builder_entry* entry; SAFE_GET_TOP_ENTRY();

    if (level <= 0) level = ZSTD_CLEVEL_DEFAULT;
    size_t bound = ZSTD_compressBound((size_t)bytes);

    void* compressed = malloc(bound);
    if (!compressed) {
        builder->entries_count--;
        return biu_status_err_allocation_failure;
    }

    size_t compressed_size = ZSTD_compress(compressed, bound, data, (size_t)bytes, level);
    if (ZSTD_isError(compressed_size)) {
        free(compressed);
        builder->entries_count--;
        return biu_status_err_allocation_failure;
    }

    // Shrink allocation
    void* tmp = realloc(compressed, compressed_size);
    if (tmp) compressed = tmp;

    entry->type = biu_type_binary_compressed;
    entry->value.data_block.dofree          = 1;
    entry->value.data_block.uncompressed    = bytes;
    entry->value.data_block.begin           = compressed;
    entry->value.data_block.length          = compressed_size;

    SAFE_ADD_SUCCESS();
}

// Lexicographics name comparison for qsort of builder entry objects
static int name_comp_for_builder_entries(const void* a, const void* b) {
    const builder_entry* entry_a = (const builder_entry *)a;
    const builder_entry* entry_b = (const builder_entry *)b;
    return name_comp(entry_a->name_length, entry_a->name, entry_b->name_length, entry_b->name);
}

// Get entry content size
static inline uint64_t entry_data_bytes(builder_entry* entry) {
    switch (entry->type) {
    case biu_type_int64:            case biu_type_float64:              return 8;
    case biu_type_text:             case biu_type_binary:               return entry->value.data_block.length;
    case biu_type_text_compressed:  case biu_type_binary_compressed:    return 8 + entry->value.data_block.length;
    } return 0; // unreachable
}

biu_status biu_builder_serialize(biu_builder* builder, void** out_buffer, uint64_t* out_bytes) {
    // Ensure output
    if (!out_buffer || !out_bytes) return biu_status_err_bad_output;

    // Sort by name
    qsort(builder->entries, builder->entries_count, sizeof(builder_entry), name_comp_for_builder_entries);

    // Ensure no names are equal, calculate names size, calculate content bytes
    uint32_t name_bytes = 0; uint64_t content_bytes = 0;
    for (uint32_t i = 0; i < builder->entries_count; ++i) {
        builder_entry* curr = &builder->entries[i];

        if (name_bytes + 1 + curr->name_length <= name_bytes) return biu_status_err_too_many_entries;    // Overflow check
        name_bytes += 1 + curr->name_length;

        content_bytes =  align8(content_bytes);   // Spec: each content starts at 8-byte alignment
        content_bytes += entry_data_bytes(curr);

        if (i == 0) continue;

        builder_entry* prev = &builder->entries[i - 1];
        if (prev->name_length == curr->name_length &&
            memcmp(prev->name, curr->name, prev->name_length) == 0) {
            return biu_status_err_duplicate_names;
        }
    }

    // Calculate index bytes
    uint64_t index_bytes = (uint64_t)builder->entries_count * 24;
    if (index_bytes > UINT32_MAX) return biu_status_err_too_many_entries;

    // Calculate required file size
    uint64_t file_size = align8((uint64_t)32u + index_bytes + name_bytes) + content_bytes;

    // Alloc with calloc to ensure padding bytes are 0-initialized
    uint8_t* result = calloc(1, file_size);
    if (!result) return biu_status_err_allocation_failure;

    uint64_t position = 0;

    // Write header
    biu_write_bytes(result, &position, MAGIC_VALUE, 8);
    biu_write_u64(result,  &position, 0); // Version
    biu_write_u32(result,  &position, index_bytes);
    biu_write_u32(result,  &position, name_bytes);
    biu_write_u64(result,  &position, content_bytes);

    // Write index
    uint32_t name_offset = 0;
    uint64_t data_offset = 0;

    for (uint32_t i = 0; i < builder->entries_count; ++i) {
        builder_entry* entry = &builder->entries[i];
        data_offset = align8(data_offset); // Spec: each content starts at 8-byte alignment

        biu_write_u32(result, &position, name_offset);
        biu_write_u32(result, &position, entry->type);
        biu_write_u64(result, &position, entry_data_bytes(entry));
        biu_write_u64(result, &position, data_offset);

        name_offset += entry->name_length + 1;
        data_offset += entry_data_bytes(entry);
    }

    // Write names
    for (uint32_t i = 0; i < builder->entries_count; ++i) {
        builder_entry* entry = &builder->entries[i];
        biu_write_bytes(result, &position, &entry->name_length, 1);
        biu_write_bytes(result, &position, entry->name, entry->name_length);
    }

    // Write content
    for (uint32_t i = 0; i < builder->entries_count; ++i) {
        position = align8(position); // Spec: each content starts at 8-byte alignment
        builder_entry* entry = &builder->entries[i];

        switch (entry->type) {
        case biu_type_int64: {
            biu_write_i64(result, &position, entry->value.int64);
        } break;
        case biu_type_float64: {
            biu_write_double(result, &position, entry->value.float64);
        } break;
        case biu_type_text_compressed: case biu_type_binary_compressed: {
            biu_write_u64 (result, &position, entry->value.data_block.uncompressed);
            biu_write_bytes(result, &position, entry->value.data_block.begin, entry->value.data_block.length);
        } break;
        case biu_type_text: case biu_type_binary: {
            biu_write_bytes(result, &position, entry->value.data_block.begin, entry->value.data_block.length);
        } break;
        }
    }

    *out_buffer = result;
    *out_bytes  = position;
    return biu_status_ok;
}

#endif // BINARIUM_IMPL
