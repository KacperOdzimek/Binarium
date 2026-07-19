/*
----------------------------------------------------------------
Contents:
This file provides a builder and zero-copy viewer API for the Binarium V0 format.

----------------------------------------------------------------
Code info:
- biu prefix
- BINARIUM_IMPL macro to build
- libzstd >= 1.5.5 dependant

----------------------------------------------------------------
Usage:
- biu_viewer:
    - create viewer over existing binarium file - you can use mmaped file memory for zero copy read
    - query entries types, read entries values
    - free view
- biu_builder:
    - build document by adding entries (ensure each have unique name, see notes)
    - serialize document, and save to file
    - free builder

----------------------------------------------------------------
Thread safety:
- biu_viewer is safe for concurrent read-only access after creation
- biu_builder is NOT thread-safe while being modified

----------------------------------------------------------------
Notes:
- This api uses null-terminated strings for convenience (binarium files use explicit lengths)
- When reading compressed files from unknown sources, it is caller responsibility
    to put a sane limit on uncompressed buffer allocation size
- If access is biu_access_claim_ownership, ownership is only claimed if operation succeeded (returned biu_status_ok)
- biu_builder does not detect duplicate names on add operations, the biu_status_err_duplicate_names error can only be returned
    by biu_builder_serialize. This allows easier implementation and O(1) insertions. Upon adding duplicate name builder becomes unserializable
- biu_create_viewer fully validates the file (checksum, layout, padding, names, labels) before returning, so it reads the
    whole buffer once. Files that are not in the canonical form the spec describes are rejected as malformed. 
    Compressed data is only checked when decompressed
- The buffer given to a viewer must stay valid and must not be modified while the viewer exists
- Memory given to the builder with biu_access_read_unowned must stay valid and unmodified until biu_builder_serialize returns
- On failure, output parameters of create / serialize functions are set to NULL (and 0 for sizes)
*/

#ifndef BINARIUM_H
#define BINARIUM_H

// ===========================
// Dependency

#include <stdint.h>
#include <string.h>

// ===========================
// Data Types

typedef enum biu_type {
    biu_type_int                = 0,
    biu_type_float              = 1,
    biu_type_label              = 2,
    biu_type_binary             = 3,
    biu_type_binary_compressed  = 4
} biu_type;

// ===========================
// Status Codes

typedef enum biu_status {
    biu_status_ok = 0,                  // API:     Operation succeeded
    biu_status_err_null_pointer,        // API:     Required output pointer was NULL
    biu_status_err_name_too_long,       // API:     Given name or label is longer than 255 characters
    biu_status_err_invalid_ascii,       // API:     Given name or label has characters outside ASCII range [32-126]
    biu_status_err_invalid_argument,    // API:     Argument out of range, e.g. not a biu_access value or input too big for zstd
    biu_status_err_allocation_failure,  // API:     Failed to memory allocate (or the result would not be representable)

    biu_status_err_no_entry,            // Viewer:  Entry of name was not found or index does not exist
    biu_status_err_malformed_file,      // Viewer:  File is malformed
    biu_status_err_unknown_version,     // Viewer:  Viewed file has version != 0
    biu_status_err_type_mismatch,       // Viewer:  Bad get operation, entry is not of this type
    biu_status_err_buffer_too_small,    // Viewer:  Buffer is to small to hold decompressed data

    biu_status_err_duplicate_names,     // Builder: Two entries with same name were added
    biu_status_err_too_many_entries,    // Builder: Exceeded maximum entries count
} biu_status;

// ===========================
// Memory access

typedef enum biu_access {
    biu_access_make_copy,               // Given memory is copied, and the copy is freed with the object
    biu_access_read_unowned,            // Given memory is read but is not freed with the object
    biu_access_claim_ownership,         // Given memory is claimed by API, and freed with the object using stdlib free
} biu_access;

// ===========================
// Viewer

typedef struct biu_viewer_create_info {
    biu_access  access;     // Buffer access profile
    const void* buffer;     // File data buffer
    uint64_t    bytes;      // File data bytes
} biu_viewer_create_info;

typedef struct biu_viewer biu_viewer;
biu_status biu_create_viewer(biu_viewer** target, const biu_viewer_create_info* info);
void biu_free_viewer(biu_viewer*);

// ===========================
// Viewer Queries

// Queries entries count
biu_status biu_viewer_query_entry_count(
    biu_viewer*, uint32_t* count
);

// Queries entry type at index
biu_status biu_viewer_query_entry_type(
    biu_viewer*, uint32_t entry, biu_type* out_type
);

// Queries entry at index name
// Returns pointer to in-file name, zero-copy
// Name is not null-terminated!
biu_status biu_viewer_query_name(
    biu_viewer*, uint32_t entry, uint8_t* out_bytes, const char** out_name
);

// Binary search entry by name
biu_status biu_viewer_find(
    biu_viewer*, const char* name, uint32_t* out
);

// ===========================
// Viewer Gets
// Pick version based on entry type

// Get for Int type
biu_status biu_viewer_get_as_int(
    biu_viewer*, uint32_t entry, int64_t* out
);

// Get for Float type
biu_status biu_viewer_get_as_float(
    biu_viewer*, uint32_t entry, double* out
);

// Get for Label type
// Zero-copy: hands back a pointer into the original buffer
biu_status biu_viewer_get_as_label(
    biu_viewer*, uint32_t entry, uint8_t* out_bytes, const char** out_label
);

// Generic get function for all types
// Zero-copy: hands back a pointer into the original buffer
biu_status biu_viewer_get_bytes(
    biu_viewer*, uint32_t entry, uint64_t* out_bytes, const uint8_t** out_data
);

// Uncompressed size get for compressed data types
// See note on limiting uncompressed size
biu_status biu_viewer_get_uncompressed_size(
    biu_viewer*, uint32_t entry, uint64_t* out_bytes
);

// Decompression function for compressed data types
// out_buf must not be NULL (even if the uncompressed size is 0). At most the stored uncompressed size is written
// to out_buf. The data must be exactly one standard zstd frame, any other content is biu_status_err_malformed_file
biu_status biu_viewer_get_decompress(
    biu_viewer*, uint32_t entry, uint64_t out_buf_size, void* out_buf
);

// ===========================
// Builder

typedef struct biu_builder biu_builder;
biu_status biu_create_builder(biu_builder** target);
void biu_free_builder(biu_builder*);

// ===========================
// Builder add operations
// Names in following function calls are copied right-away

biu_status biu_builder_add_int(
    biu_builder*, const char* name, int64_t value
);

biu_status biu_builder_add_float(
    biu_builder*, const char* name, double value
);

biu_status biu_builder_add_label(
    biu_builder*, const char* name, const char* value, biu_access access
);

biu_status biu_builder_add_binary(
    biu_builder*, const char* name, uint64_t bytes, const void* data, biu_access access
);

// Level = zstd compression level; pass <= 0 for sane default
biu_status biu_builder_add_binary_compressed(
    biu_builder*, const char* name, uint64_t bytes, const void* data, int level
);

// ===========================
// Builder Serialization

// Serializes to a new buffer
// caller must free(*out_buffer) if status was biu_status_ok
biu_status biu_builder_serialize(
    biu_builder*, void** out_buffer, uint64_t* out_bytes
);

// ===========================
// Little endian reads and writes helpers
// The write helpers do no bounds checking

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

static inline void biu_write_u32(uint8_t* dst, uint64_t* pos, uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) { dst[*pos + i] = (uint8_t)(value >> (8 * i)); }
    *pos += 4;
}

static inline void biu_write_u64(uint8_t* dst, uint64_t* pos, uint64_t value) {
    for (unsigned i = 0; i < 8; ++i) { dst[*pos + i] = (uint8_t)(value >> (8 * i)); }
    *pos += 8;
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
    if (size) memcpy(result + *position, src, (size_t)size);
    *position += size;
}

#endif // BINARIUM_H

#ifdef BINARIUM_IMPL

// ===========================
// Dependency

#include <stdlib.h>
#include <zstd.h>

// ===========================
// Asserts

#include <float.h>

#if DBL_MANT_DIG != 53 || DBL_MAX_EXP != 1024
#error "Requires IEEE-754 binary64 double"
#endif

// Binarium requires a 64 bit machine, file sizes and offsets are cast to size_t
typedef char biu_static_assert_pointer_size[(sizeof(void*) == 8) ? 1 : -1];
typedef char biu_static_assert_size_t_size[(sizeof(size_t) == 8) ? 1 : -1];

#if ZSTD_VERSION_NUMBER < 10505
#error "Binarium V0 requires zstd 1.5.5 or greater"
#endif

// ===========================
// Constants

#define BIU_HEADER_BYTES    32u
#define BIU_ENTRY_BYTES     24u
#define BIU_FOOTER_BYTES    16u
#define BIU_MAX_ENTRIES     16777215u
#define BIU_MAX_STRING      255
#define BIU_LAST_TYPE       ((uint32_t)biu_type_binary_compressed)
#define BIU_ZSTD_MAGIC      0xFD2FB528u     // Magic number of a standard (non-skippable, non-legacy) zstd frame

static const uint8_t BIU_MAGIC_VALUE[8] = {
    'B','I','N','A','R','I','U','M'
};

// ===========================
// Checksum

static const uint64_t BIU_CRC64_TABLE[256] = {
    0x0000000000000000ull, 0x42f0e1eba9ea3693ull, 0x85e1c3d753d46d26ull, 0xc711223cfa3e5bb5ull,
    0x493366450e42ecdfull, 0x0bc387aea7a8da4cull, 0xccd2a5925d9681f9ull, 0x8e224479f47cb76aull,
    0x9266cc8a1c85d9beull, 0xd0962d61b56fef2dull, 0x17870f5d4f51b498ull, 0x5577eeb6e6bb820bull,
    0xdb55aacf12c73561ull, 0x99a54b24bb2d03f2ull, 0x5eb4691841135847ull, 0x1c4488f3e8f96ed4ull,
    0x663d78ff90e185efull, 0x24cd9914390bb37cull, 0xe3dcbb28c335e8c9ull, 0xa12c5ac36adfde5aull,
    0x2f0e1eba9ea36930ull, 0x6dfeff5137495fa3ull, 0xaaefdd6dcd770416ull, 0xe81f3c86649d3285ull,
    0xf45bb4758c645c51ull, 0xb6ab559e258e6ac2ull, 0x71ba77a2dfb03177ull, 0x334a9649765a07e4ull,
    0xbd68d2308226b08eull, 0xff9833db2bcc861dull, 0x388911e7d1f2dda8ull, 0x7a79f00c7818eb3bull,
    0xcc7af1ff21c30bdeull, 0x8e8a101488293d4dull, 0x499b3228721766f8ull, 0x0b6bd3c3dbfd506bull,
    0x854997ba2f81e701ull, 0xc7b97651866bd192ull, 0x00a8546d7c558a27ull, 0x4258b586d5bfbcb4ull,
    0x5e1c3d753d46d260ull, 0x1cecdc9e94ace4f3ull, 0xdbfdfea26e92bf46ull, 0x990d1f49c77889d5ull,
    0x172f5b3033043ebfull, 0x55dfbadb9aee082cull, 0x92ce98e760d05399ull, 0xd03e790cc93a650aull,
    0xaa478900b1228e31ull, 0xe8b768eb18c8b8a2ull, 0x2fa64ad7e2f6e317ull, 0x6d56ab3c4b1cd584ull,
    0xe374ef45bf6062eeull, 0xa1840eae168a547dull, 0x66952c92ecb40fc8ull, 0x2465cd79455e395bull,
    0x3821458aada7578full, 0x7ad1a461044d611cull, 0xbdc0865dfe733aa9ull, 0xff3067b657990c3aull,
    0x711223cfa3e5bb50ull, 0x33e2c2240a0f8dc3ull, 0xf4f3e018f031d676ull, 0xb60301f359dbe0e5ull,
    0xda050215ea6c212full, 0x98f5e3fe438617bcull, 0x5fe4c1c2b9b84c09ull, 0x1d14202910527a9aull,
    0x93366450e42ecdf0ull, 0xd1c685bb4dc4fb63ull, 0x16d7a787b7faa0d6ull, 0x5427466c1e109645ull,
    0x4863ce9ff6e9f891ull, 0x0a932f745f03ce02ull, 0xcd820d48a53d95b7ull, 0x8f72eca30cd7a324ull,
    0x0150a8daf8ab144eull, 0x43a04931514122ddull, 0x84b16b0dab7f7968ull, 0xc6418ae602954ffbull,
    0xbc387aea7a8da4c0ull, 0xfec89b01d3679253ull, 0x39d9b93d2959c9e6ull, 0x7b2958d680b3ff75ull,
    0xf50b1caf74cf481full, 0xb7fbfd44dd257e8cull, 0x70eadf78271b2539ull, 0x321a3e938ef113aaull,
    0x2e5eb66066087d7eull, 0x6cae578bcfe24bedull, 0xabbf75b735dc1058ull, 0xe94f945c9c3626cbull,
    0x676dd025684a91a1ull, 0x259d31cec1a0a732ull, 0xe28c13f23b9efc87ull, 0xa07cf2199274ca14ull,
    0x167ff3eacbaf2af1ull, 0x548f120162451c62ull, 0x939e303d987b47d7ull, 0xd16ed1d631917144ull,
    0x5f4c95afc5edc62eull, 0x1dbc74446c07f0bdull, 0xdaad56789639ab08ull, 0x985db7933fd39d9bull,
    0x84193f60d72af34full, 0xc6e9de8b7ec0c5dcull, 0x01f8fcb784fe9e69ull, 0x43081d5c2d14a8faull,
    0xcd2a5925d9681f90ull, 0x8fdab8ce70822903ull, 0x48cb9af28abc72b6ull, 0x0a3b7b1923564425ull,
    0x70428b155b4eaf1eull, 0x32b26afef2a4998dull, 0xf5a348c2089ac238ull, 0xb753a929a170f4abull,
    0x3971ed50550c43c1ull, 0x7b810cbbfce67552ull, 0xbc902e8706d82ee7ull, 0xfe60cf6caf321874ull,
    0xe224479f47cb76a0ull, 0xa0d4a674ee214033ull, 0x67c58448141f1b86ull, 0x253565a3bdf52d15ull,
    0xab1721da49899a7full, 0xe9e7c031e063acecull, 0x2ef6e20d1a5df759ull, 0x6c0603e6b3b7c1caull,
    0xf6fae5c07d3274cdull, 0xb40a042bd4d8425eull, 0x731b26172ee619ebull, 0x31ebc7fc870c2f78ull,
    0xbfc9838573709812ull, 0xfd39626eda9aae81ull, 0x3a28405220a4f534ull, 0x78d8a1b9894ec3a7ull,
    0x649c294a61b7ad73ull, 0x266cc8a1c85d9be0ull, 0xe17dea9d3263c055ull, 0xa38d0b769b89f6c6ull,
    0x2daf4f0f6ff541acull, 0x6f5faee4c61f773full, 0xa84e8cd83c212c8aull, 0xeabe6d3395cb1a19ull,
    0x90c79d3fedd3f122ull, 0xd2377cd44439c7b1ull, 0x15265ee8be079c04ull, 0x57d6bf0317edaa97ull,
    0xd9f4fb7ae3911dfdull, 0x9b041a914a7b2b6eull, 0x5c1538adb04570dbull, 0x1ee5d94619af4648ull,
    0x02a151b5f156289cull, 0x4051b05e58bc1e0full, 0x87409262a28245baull, 0xc5b073890b687329ull,
    0x4b9237f0ff14c443ull, 0x0962d61b56fef2d0ull, 0xce73f427acc0a965ull, 0x8c8315cc052a9ff6ull,
    0x3a80143f5cf17f13ull, 0x7870f5d4f51b4980ull, 0xbf61d7e80f251235ull, 0xfd913603a6cf24a6ull,
    0x73b3727a52b393ccull, 0x31439391fb59a55full, 0xf652b1ad0167feeaull, 0xb4a25046a88dc879ull,
    0xa8e6d8b54074a6adull, 0xea16395ee99e903eull, 0x2d071b6213a0cb8bull, 0x6ff7fa89ba4afd18ull,
    0xe1d5bef04e364a72ull, 0xa3255f1be7dc7ce1ull, 0x64347d271de22754ull, 0x26c49cccb40811c7ull,
    0x5cbd6cc0cc10fafcull, 0x1e4d8d2b65facc6full, 0xd95caf179fc497daull, 0x9bac4efc362ea149ull,
    0x158e0a85c2521623ull, 0x577eeb6e6bb820b0ull, 0x906fc95291867b05ull, 0xd29f28b9386c4d96ull,
    0xcedba04ad0952342ull, 0x8c2b41a1797f15d1ull, 0x4b3a639d83414e64ull, 0x09ca82762aab78f7ull,
    0x87e8c60fded7cf9dull, 0xc51827e4773df90eull, 0x020905d88d03a2bbull, 0x40f9e43324e99428ull,
    0x2cffe7d5975e55e2ull, 0x6e0f063e3eb46371ull, 0xa91e2402c48a38c4ull, 0xebeec5e96d600e57ull,
    0x65cc8190991cb93dull, 0x273c607b30f68faeull, 0xe02d4247cac8d41bull, 0xa2dda3ac6322e288ull,
    0xbe992b5f8bdb8c5cull, 0xfc69cab42231bacfull, 0x3b78e888d80fe17aull, 0x7988096371e5d7e9ull,
    0xf7aa4d1a85996083ull, 0xb55aacf12c735610ull, 0x724b8ecdd64d0da5ull, 0x30bb6f267fa73b36ull,
    0x4ac29f2a07bfd00dull, 0x08327ec1ae55e69eull, 0xcf235cfd546bbd2bull, 0x8dd3bd16fd818bb8ull,
    0x03f1f96f09fd3cd2ull, 0x41011884a0170a41ull, 0x86103ab85a2951f4ull, 0xc4e0db53f3c36767ull,
    0xd8a453a01b3a09b3ull, 0x9a54b24bb2d03f20ull, 0x5d45907748ee6495ull, 0x1fb5719ce1045206ull,
    0x919735e51578e56cull, 0xd367d40ebc92d3ffull, 0x1476f63246ac884aull, 0x568617d9ef46bed9ull,
    0xe085162ab69d5e3cull, 0xa275f7c11f7768afull, 0x6564d5fde549331aull, 0x279434164ca30589ull,
    0xa9b6706fb8dfb2e3ull, 0xeb46918411358470ull, 0x2c57b3b8eb0bdfc5ull, 0x6ea7525342e1e956ull,
    0x72e3daa0aa188782ull, 0x30133b4b03f2b111ull, 0xf7021977f9cceaa4ull, 0xb5f2f89c5026dc37ull,
    0x3bd0bce5a45a6b5dull, 0x79205d0e0db05dceull, 0xbe317f32f78e067bull, 0xfcc19ed95e6430e8ull,
    0x86b86ed5267cdbd3ull, 0xc4488f3e8f96ed40ull, 0x0359ad0275a8b6f5ull, 0x41a94ce9dc428066ull,
    0xcf8b0890283e370cull, 0x8d7be97b81d4019full, 0x4a6acb477bea5a2aull, 0x089a2aacd2006cb9ull,
    0x14dea25f3af9026dull, 0x562e43b4931334feull, 0x913f6188692d6f4bull, 0xd3cf8063c0c759d8ull,
    0x5dedc41a34bbeeb2ull, 0x1f1d25f19d51d821ull, 0xd80c07cd676f8394ull, 0x9afce626ce85b507ull,
};

static uint64_t biu_crc64(uint64_t length, const uint8_t* data) {
    uint64_t crc = 0;
    for (uint64_t i = 0; i < length; ++i) {
        crc = (crc << 8) ^ BIU_CRC64_TABLE[(uint8_t)(crc >> 56) ^ data[i]];
    }
    return crc;
}

// ===========================
// Helpers

// Align up to 8 bytes. No overflow check
static inline uint64_t biu_align8(uint64_t x) {
    return (x + 7ull) & ~7ull;
}

// Overflow-checked a + b. Returns 0 on overflow
static inline int biu_add_u64_checked(uint64_t a, uint64_t b, uint64_t* out) {
    if (a > UINT64_MAX - b) return 0;
    *out = a + b;
    return 1;
}

// Overflow-checked align up to 8 bytes. Returns 0 on overflow
static inline int biu_align8_checked(uint64_t x, uint64_t* out) {
    uint64_t sum;
    if (!biu_add_u64_checked(x, 7, &sum)) return 0;
    *out = sum & ~7ull;
    return 1;
}

// Checks every byte is in the spec's allowed ASCII range [32-126]
static int biu_is_valid_ascii(const uint8_t* s, uint64_t length) {
    for (uint64_t i = 0; i < length; ++i) {
        if (s[i] < 32 || s[i] > 126) return 0;
    }
    return 1;
}

// Checks every byte is zero (padding)
static int biu_is_zero(const uint8_t* s, uint64_t length) {
    for (uint64_t i = 0; i < length; ++i) {
        if (s[i] != 0) return 0;
    }
    return 1;
}

// Checks [offset, offset + length) lies inside [0, total], without overflowing
static inline int biu_range_fits(uint64_t total, uint64_t offset, uint64_t length) {
    return offset <= total && length <= total - offset;
}

static inline int biu_is_valid_access(biu_access access) {
    return access == biu_access_make_copy
        || access == biu_access_read_unowned
        || access == biu_access_claim_ownership;
}

// Lexicographical name comparison
// < 0 means a first, 0 equal, > 0 means b first
static int biu_name_comp(uint8_t a_length, const uint8_t* a, uint8_t b_length, const uint8_t* b) {
    uint8_t min_length = a_length < b_length ? a_length : b_length;

    int cmp = memcmp(a, b, min_length);
    if (cmp != 0) return cmp;

    if (a_length < b_length) return -1;
    if (a_length > b_length) return 1;
    return 0;
}

static void biu_free_memory(biu_access access, void* memory) {
    if (access == biu_access_read_unowned) return;
    free(memory);
}

// ===========================
// Viewer

struct biu_viewer {
    biu_access  access;         // Buffer access
    const void* buffer;         // File data buffer
    uint64_t    bytes;          // File data length
    uint32_t    entries;        // Entries count
    uint64_t    index_begin;    // Byte offset of index array from buffer begin
    uint64_t    names_begin;    // Byte offset of names array from buffer begin
    uint32_t    names_bytes;    // Names array size in bytes
    uint64_t    content_begin;  // Byte offset of content section from buffer begin
    uint64_t    content_bytes;  // Content section size in bytes
};

typedef struct biu_view_entry {
    uint32_t    name_offset;
    uint32_t    type;
    uint64_t    data_bytes;
    uint64_t    data_offset;
} biu_view_entry;

// With assumption entry exists
static inline biu_view_entry biu_view_read_index(const biu_viewer* view, uint32_t entry) {
    const uint8_t* p = (const uint8_t*)view->buffer + view->index_begin + (uint64_t)entry * BIU_ENTRY_BYTES;
    biu_view_entry out;
    out.name_offset = biu_read_u32(p + 0);
    out.type        = biu_read_u32(p + 4);
    out.data_bytes  = biu_read_u64(p + 8);
    out.data_offset = biu_read_u64(p + 16);
    return out;
}

// Gets entry's at index name
// Returns 0 on out-of-bounds (corrupt file), 1 on success
// With assumption entry exists
static inline int biu_view_get_entry_name(
    const biu_viewer* view, uint32_t entry, uint8_t* out_bytes, const char** out_ptr
) {
    biu_view_entry idx = biu_view_read_index(view, entry);
    if ((uint64_t)idx.name_offset >= view->names_bytes) return 0;

    const uint8_t* name_ptr = (const uint8_t*)view->buffer + view->names_begin + idx.name_offset;
    uint8_t length = name_ptr[0];
    if ((uint64_t)idx.name_offset + 1 + length > view->names_bytes) return 0;

    *out_bytes = length;
    *out_ptr = (const char*)(name_ptr + 1);
    return 1;
}

// Checks [data_offset, data_offset + data_bytes) is in content section bounds (overflow safe)
static inline int biu_data_in_content_bounds(const biu_viewer* view, uint64_t data_offset, uint64_t data_bytes) {
    return biu_range_fits(view->content_bytes, data_offset, data_bytes);
}

// Check all entries; Discard malformed files
static biu_status biu_validate_entries(const biu_viewer* view) {
    const uint8_t* buf     = (const uint8_t*)view->buffer;
    const uint8_t* content = buf + view->content_begin;

    // Name cursors to ensure names order
    uint64_t        name_cursor = 0;    // Expected offset of the next name (names are tightly packed)
    const uint8_t*  prev_name = NULL;   // Previous name, for strict ordering (=> sorted and unique)
    uint8_t         prev_length = 0;

    // Content cursor, relative to content begin. A zero-length chunk occupies no bytes
    uint64_t chunk_end = 0;             // End (exclusive) of the previous chunk, 0 before the first one

    for (uint32_t i = 0; i < view->entries; ++i) {
        biu_view_entry idx = biu_view_read_index(view, i);

        // Name
        if (idx.name_offset != name_cursor)     return biu_status_err_malformed_file;
        if (name_cursor >= view->names_bytes)   return biu_status_err_malformed_file;

        const uint8_t* name_ptr = buf + view->names_begin + name_cursor;
        uint8_t length = name_ptr[0];

        if (name_cursor + 1 + length > view->names_bytes) return biu_status_err_malformed_file;

        const uint8_t* name = name_ptr + 1;
        if (!biu_is_valid_ascii(name, length))                                      return biu_status_err_malformed_file;
        if (prev_name && biu_name_comp(prev_length, prev_name, length, name) >= 0)  return biu_status_err_malformed_file;
        prev_name = name; prev_length = length; name_cursor += 1u + length;

        // Ensure type and minimal type sizes
        if (idx.type > BIU_LAST_TYPE) return biu_status_err_malformed_file;
        switch ((biu_type)idx.type) {
        // Exactly 8 bytes
        case biu_type_int: case biu_type_float: {
            if (idx.data_bytes != 8) return biu_status_err_malformed_file;
        } break;
        // At most BIU_MAX_STRING bytes
        case biu_type_label: {
            if (idx.data_bytes > BIU_MAX_STRING) return biu_status_err_malformed_file;
        } break;
        // At least 8 bytes for uncompressed size
        case biu_type_binary_compressed: {
            if (idx.data_bytes < 8) return biu_status_err_malformed_file;
        } break;
        // No constrain
        case biu_type_binary: break;
        }

        // Content layout. chunk_end <= content_bytes <= UINT64_MAX - 7 (checked on creation), align8 cannot wrap
        if (idx.data_offset != biu_align8(chunk_end)) return biu_status_err_malformed_file;
        if (!biu_data_in_content_bounds(view, idx.data_offset, idx.data_bytes)) return biu_status_err_malformed_file;

        // Padding between previous chunk and this one must be zero
        if (!biu_is_zero(content + chunk_end, idx.data_offset - chunk_end)) return biu_status_err_malformed_file;
        chunk_end = idx.data_offset + idx.data_bytes; // Cannot overflow, bounds checked above

        // Check labels ASCII
        if (idx.type == biu_type_label) {
            if (!biu_is_valid_ascii(content + idx.data_offset, idx.data_bytes)) return biu_status_err_malformed_file;
        }
    }

    // Names section must be fully and exactly consumed
    if (name_cursor != view->names_bytes) return biu_status_err_malformed_file;

    // Content section must end exactly where the last chunk ends
    if (chunk_end != view->content_bytes) return biu_status_err_malformed_file;
    return biu_status_ok;
}

biu_status biu_create_viewer(biu_viewer** target, const biu_viewer_create_info* info) {
    // Ensure arguments
    if (!target) return biu_status_err_null_pointer;

    *target = NULL;
    if (!info || !info->buffer) return biu_status_err_null_pointer;

    // An unknown access value would otherwise end up being free()d by biu_free_viewer
    if (!biu_is_valid_access(info->access)) return biu_status_err_invalid_argument;

    const uint8_t* buf  = (const uint8_t*)info->buffer;
    const uint64_t size = info->bytes;

    // Ensure header fits
    if (size < BIU_HEADER_BYTES) return biu_status_err_malformed_file;

    // Ensure header magic value
    if (memcmp(buf, BIU_MAGIC_VALUE, 8) != 0) return biu_status_err_malformed_file;

    // Version - only version 0 is implemented
    if (biu_read_u64(buf + 8) != 0) return biu_status_err_unkown_version;

    // Read sizing
    biu_viewer tmp;
    memset(&tmp, 0, sizeof(tmp));
    tmp.access        = info->access;
    tmp.buffer        = info->buffer;
    tmp.bytes         = size;
    tmp.entries       = biu_read_u32(buf + 16);
    tmp.names_bytes   = biu_read_u32(buf + 20);
    tmp.content_bytes = biu_read_u64(buf + 24);

    // Spec: entries count is capped, bigger counts are malformed
    if (tmp.entries > BIU_MAX_ENTRIES) return biu_status_err_malformed_file;

    // Calculate section beginnings
    tmp.index_begin   = biu_align8(BIU_HEADER_BYTES);
    tmp.names_begin   = tmp.index_begin + (uint64_t)tmp.entries * BIU_ENTRY_BYTES;
    tmp.content_begin = biu_align8(tmp.names_begin + (uint64_t)tmp.names_bytes);

    // content_begin <= size and content_bytes <= size - content_begin, so content_end cannot overflow
    if (!biu_range_fits(size, tmp.content_begin, tmp.content_bytes)) return biu_status_err_malformed_file;
    uint64_t content_end = tmp.content_begin + tmp.content_bytes;

    // Ensure footer. It is the last section, the file must end exactly after it:
    // bytes past the footer would not be covered by the checksum.
    // content_end <= size, so guard align8 against wrapping for absurd buffer sizes
    if (content_end > UINT64_MAX - 7) return biu_status_err_malformed_file;
    uint64_t footer_begin = biu_align8(content_end);
    if (footer_begin > UINT64_MAX - BIU_FOOTER_BYTES)   return biu_status_err_malformed_file;
    if (size != footer_begin + BIU_FOOTER_BYTES)        return biu_status_err_malformed_file;

    // Ensure footer magic value
    if (memcmp(buf + footer_begin + 8, BIU_MAGIC_VALUE, 8) != 0) return biu_status_err_malformed_file;

    // Ensure footer checksum, over [first header byte, footer first byte)
    uint64_t current = biu_crc64(footer_begin, buf);
    uint64_t stored  = biu_read_u64(buf + footer_begin);
    if (current != stored) return biu_status_err_malformed_file;

    // Padding must be zero: names -> content, content -> footer (gaps between chunks are checked below)
    uint64_t names_end = tmp.names_begin + tmp.names_bytes;
    if (!biu_is_zero(buf + names_end, tmp.content_begin - names_end)) return biu_status_err_malformed_file;
    if (!biu_is_zero(buf + content_end, footer_begin - content_end))  return biu_status_err_malformed_file;

    // Ensure names, index and content layout are consistent with the spec
    biu_status status = biu_validate_entries(&tmp);
    if (status != biu_status_ok) return status;

    // File is okay, publish the viewer
    biu_viewer* view = (biu_viewer*)malloc(sizeof(biu_viewer));
    if (!view) return biu_status_err_allocation_failure;
    *view = tmp;

    // Copy file into viewer
    if (info->access == biu_access_make_copy) {
        void* copy = malloc((size_t)size);  // size >= BIU_HEADER_BYTES + BIU_FOOTER_BYTES here
        if (!copy) { free(view); return biu_status_err_allocation_failure; }
        memcpy(copy, info->buffer, (size_t)size);
        view->buffer = copy;
    }

    *target = view; return biu_status_ok;
}

void biu_free_viewer(biu_viewer* view) {
    if (!view) return;
    biu_free_memory(view->access, (void*)view->buffer);
    free(view);
}

biu_status biu_viewer_query_entry_count(biu_viewer* view, uint32_t* count) {
    if (!view || !count) return biu_status_err_null_pointer;
    *count = view->entries;
    return biu_status_ok;
}

biu_status biu_viewer_query_entry_type(biu_viewer* view, uint32_t entry, biu_type* out_type) {
    if (!view || !out_type)     return biu_status_err_null_pointer;
    if (entry >= view->entries) return biu_status_err_no_entry;
    biu_view_entry idx = biu_view_read_index(view, entry);
    if (idx.type > BIU_LAST_TYPE) return biu_status_err_malformed_file;
    *out_type = (biu_type)idx.type;
    return biu_status_ok;
}

biu_status biu_viewer_query_name(biu_viewer* view, uint32_t entry, uint8_t* out_bytes, const char** out_name) {
    if (!view || !out_bytes || !out_name)                           return biu_status_err_null_pointer;
    if (entry >= view->entries)                                     return biu_status_err_no_entry;
    if (!biu_view_get_entry_name(view, entry, out_bytes, out_name)) return biu_status_err_malformed_file;
    return biu_status_ok;
}

biu_status biu_viewer_find(biu_viewer* view, const char* name, uint32_t* out) {
    if (!view || !name || !out) return biu_status_err_null_pointer;

    size_t length = strlen(name);
    if (length > BIU_MAX_STRING) return biu_status_err_name_too_long;

    uint32_t lo = 0, hi = view->entries;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2;
        const char* mid_name; uint8_t mid_len;

        if (!biu_view_get_entry_name(view, mid, &mid_len, &mid_name)) return biu_status_err_malformed_file;
        int cmp = biu_name_comp(mid_len, (const uint8_t*)mid_name, (uint8_t)length, (const uint8_t*)name);

        if (cmp == 0) {
            *out = mid;  return biu_status_ok;
        }

        if (cmp < 0) lo = mid + 1;
        else         hi = mid;
    }

    return biu_status_err_no_entry;
}

biu_status biu_viewer_get_as_int(biu_viewer* view, uint32_t entry, int64_t* out) {
    if (!view || !out)          return biu_status_err_null_pointer;
    if (entry >= view->entries) return biu_status_err_no_entry;

    biu_view_entry idx = biu_view_read_index(view, entry);
    if (idx.type != biu_type_int)   return biu_status_err_type_mismatch;
    if (idx.data_bytes != 8)        return biu_status_err_malformed_file;
    if (!biu_data_in_content_bounds(view, idx.data_offset, idx.data_bytes)) return biu_status_err_malformed_file;

    const uint8_t* p = (const uint8_t*)view->buffer + view->content_begin + idx.data_offset;
    *out = biu_read_i64(p); return biu_status_ok;
}

biu_status biu_viewer_get_as_float(biu_viewer* view, uint32_t entry, double* out) {
    if (!view || !out)          return biu_status_err_null_pointer;
    if (entry >= view->entries) return biu_status_err_no_entry;

    biu_view_entry idx = biu_view_read_index(view, entry);
    if (idx.type != biu_type_float) return biu_status_err_type_mismatch;
    if (idx.data_bytes != 8)        return biu_status_err_malformed_file;
    if (!biu_data_in_content_bounds(view, idx.data_offset, idx.data_bytes)) return biu_status_err_malformed_file;

    const uint8_t* p = (const uint8_t*)view->buffer + view->content_begin + idx.data_offset;
    *out = biu_read_double(p); return biu_status_ok;
}

biu_status biu_viewer_get_as_label(biu_viewer* view, uint32_t entry, uint8_t* out_bytes, const char** out_label) {
    if (!view || !out_bytes || !out_label)  return biu_status_err_null_pointer;
    if (entry >= view->entries)             return biu_status_err_no_entry;

    biu_view_entry idx = biu_view_read_index(view, entry);
    if (idx.type != biu_type_label)         return biu_status_err_type_mismatch;
    if (idx.data_bytes > BIU_MAX_STRING)    return biu_status_err_malformed_file;
    if (!biu_data_in_content_bounds(view, idx.data_offset, idx.data_bytes)) return biu_status_err_malformed_file;

    // Label characters were validated as ASCII [32-126] in biu_create_viewer
    *out_bytes = (uint8_t)idx.data_bytes;
    *out_label = (const char*)view->buffer + view->content_begin + idx.data_offset;
    return biu_status_ok;
}

biu_status biu_viewer_get_bytes(biu_viewer* view, uint32_t entry, uint64_t* out_bytes, const uint8_t** out_data) {
    if (!view || !out_bytes || !out_data)   return biu_status_err_null_pointer;
    if (entry >= view->entries)             return biu_status_err_no_entry;

    biu_view_entry idx = biu_view_read_index(view, entry);
    if (!biu_data_in_content_bounds(view, idx.data_offset, idx.data_bytes)) return biu_status_err_malformed_file;

    *out_bytes = idx.data_bytes;
    *out_data  = (const uint8_t*)view->buffer + view->content_begin + idx.data_offset;
    return biu_status_ok;
}

// Reads the uncompressed size prefix without decompressing anything
biu_status biu_viewer_get_uncompressed_size(biu_viewer* view, uint32_t entry, uint64_t* out_bytes) {
    if (!view || !out_bytes)                return biu_status_err_null_pointer;
    if (entry >= view->entries)             return biu_status_err_no_entry;

    biu_view_entry idx = biu_view_read_index(view, entry);
    if (idx.type != biu_type_binary_compressed) return biu_status_err_type_mismatch;
    if (!biu_data_in_content_bounds(view, idx.data_offset, idx.data_bytes)) return biu_status_err_malformed_file;
    if (idx.data_bytes < 8) return biu_status_err_malformed_file;

    const uint8_t* p = (const uint8_t*)view->buffer + view->content_begin + idx.data_offset;
    *out_bytes = biu_read_u64(p); return biu_status_ok;
}

// Decompresses fresh into the caller-supplied buffer every call - result is never cached
biu_status biu_viewer_get_decompress(biu_viewer* view, uint32_t entry, uint64_t out_buf_size, void* out_buf) {
    if (!view || !out_buf)      return biu_status_err_null_pointer;
    if (entry >= view->entries) return biu_status_err_no_entry;

    biu_view_entry idx = biu_view_read_index(view, entry);
    if (idx.type != biu_type_binary_compressed) return biu_status_err_type_mismatch;
    if (!biu_data_in_content_bounds(view, idx.data_offset, idx.data_bytes)) return biu_status_err_malformed_file;
    if (idx.data_bytes < 8)                                                 return biu_status_err_malformed_file;

    const uint8_t* p = (const uint8_t*)view->buffer + view->content_begin + idx.data_offset;
    uint64_t uncompressed_size = biu_read_u64(p); const uint8_t* compressed_data = p + 8; uint64_t compressed_bytes = idx.data_bytes - 8;

    if (out_buf_size < uncompressed_size) return biu_status_err_buffer_too_small;   // Ensure out buffer size

    // Spec: exactly one standard ZSTD frame. Skippable and legacy frames are not allowed, and neither is
    // anything past the frame (ZSTD_decompress alone would accept concatenated and skippable frames)
    if (compressed_bytes < 4 || biu_read_u32(compressed_data) != BIU_ZSTD_MAGIC) return biu_status_err_malformed_file;
    size_t frame_size = ZSTD_findFrameCompressedSize(compressed_data, (size_t)compressed_bytes);
    if (ZSTD_isError(frame_size) || (uint64_t)frame_size != compressed_bytes) return biu_status_err_malformed_file;

    // Capacity is the declared uncompressed size, never more: nothing past it is ever written, even if the
    // caller's buffer is bigger and the stored size lies
    size_t written = ZSTD_decompress(out_buf, (size_t)uncompressed_size, compressed_data, (size_t)compressed_bytes);

    // Check decompression succeeded and produced exactly the declared size
    if (ZSTD_isError(written) || (uint64_t)written != uncompressed_size) return biu_status_err_malformed_file;

    return biu_status_ok;
}

// ===========================
// Builder

typedef struct biu_builder_entry {
    uint8_t  name_length;
    uint8_t  name[BIU_MAX_STRING];
    biu_type type;
    union {
        int64_t as_int;
        double  as_float;
        struct {
            char        do_free;
            uint64_t    uncompressed;
            uint64_t    length;
            const void* begin;
        } as_bytes;
    } value;
} biu_builder_entry;

struct biu_builder {
    uint32_t            entries_count;
    uint32_t            entries_capacity;
    biu_builder_entry*  entries;
};

biu_status biu_create_builder(biu_builder** target) {
    if (!target) return biu_status_err_null_pointer;
    *target = (biu_builder*)calloc(1, sizeof(biu_builder));
    if (!(*target)) return biu_status_err_allocation_failure;
    return biu_status_ok;
}

static inline int biu_is_fixed_size_type(biu_type type) {
    if (type == biu_type_int || type == biu_type_float) return 1;
    return 0;
}

void biu_free_builder(biu_builder* builder) {
    if (!builder) return;
    for (uint32_t i = 0; i < builder->entries_count; i++) {
        biu_builder_entry* entry = &builder->entries[i];
        if (biu_is_fixed_size_type(entry->type))continue;
        if (!entry->value.as_bytes.do_free)     continue;
        free((void*)entry->value.as_bytes.begin);
    }
    free(builder->entries);
    free(builder);
}

// On failure nothing is claimed from the caller and the entry owns nothing
static inline biu_status biu_link_data(biu_builder_entry* entry, uint64_t bytes, const void* data, biu_access access) {
    switch (access) {
    case biu_access_claim_ownership: {
        entry->value.as_bytes.do_free = 1;
        entry->value.as_bytes.length  = bytes;
        entry->value.as_bytes.begin   = data;
    } break;
    case biu_access_read_unowned: {
        entry->value.as_bytes.do_free = 0;
        entry->value.as_bytes.length  = bytes;
        entry->value.as_bytes.begin   = data;
    } break;
    case biu_access_make_copy: {
        entry->value.as_bytes.length  = bytes;
        if (bytes == 0) {
            entry->value.as_bytes.do_free = 0;
            entry->value.as_bytes.begin   = NULL;
            break;
        }
        void* copy = malloc((size_t)bytes);
        if (!copy) return biu_status_err_allocation_failure;
        memcpy(copy, data, (size_t)bytes);
        entry->value.as_bytes.do_free = 1;
        entry->value.as_bytes.begin   = copy;
    } break;
    default: return biu_status_err_invalid_argument;    // Never leave the entry half-initialised
    }
    return biu_status_ok;
}

static inline biu_status biu_link_data_and_compress(biu_builder_entry* entry, uint64_t bytes, const void* data, int level) {
    if (level <= 0) level = ZSTD_CLEVEL_DEFAULT;

    size_t bound = ZSTD_compressBound((size_t)bytes);
    if (bound == 0 || ZSTD_isError(bound)) return biu_status_err_invalid_argument;  // Input too big for zstd

    void* compressed = malloc(bound);
    if (!compressed) {
        return biu_status_err_allocation_failure;
    }

    size_t compressed_size = ZSTD_compress(compressed, bound, data, (size_t)bytes, level);
    if (ZSTD_isError(compressed_size)) {
        free(compressed);
        return biu_status_err_allocation_failure;
    }

    void* tmp = realloc(compressed, compressed_size ? compressed_size : 1);
    if (tmp) compressed = tmp;

    entry->value.as_bytes.do_free      = 1;
    entry->value.as_bytes.uncompressed = bytes;
    entry->value.as_bytes.begin        = compressed;
    entry->value.as_bytes.length       = compressed_size;

    return biu_status_ok;
}

static inline biu_status biu_try_grow(biu_builder* builder, uint32_t new_capacity) {
    biu_builder_entry* new_entries = (biu_builder_entry*)realloc(builder->entries, (size_t)new_capacity * sizeof(biu_builder_entry));
    if (!new_entries) return biu_status_err_allocation_failure;
    builder->entries          = new_entries;
    builder->entries_capacity = new_capacity;
    return biu_status_ok;
}

static inline biu_status biu_reserve_entry(biu_builder* builder) {
    if (builder->entries_count >= BIU_MAX_ENTRIES) return biu_status_err_too_many_entries;   // Spec: up to BIU_MAX_ENTRIES inclusive
    if (builder->entries_count == builder->entries_capacity) {
        uint32_t ambitious_strategy = builder->entries_capacity ? builder->entries_capacity * 2 : 16;
        if (ambitious_strategy > BIU_MAX_ENTRIES) ambitious_strategy = BIU_MAX_ENTRIES;
        uint32_t minimal_strategy   = builder->entries_capacity + 1;
        if (biu_try_grow(builder, ambitious_strategy) == biu_status_ok) return biu_status_ok;
        if (biu_try_grow(builder, minimal_strategy)   == biu_status_ok) return biu_status_ok;
        return biu_status_err_allocation_failure;
    }
    return biu_status_ok;
}

// Validates the name, makes room for a new entry and fills the name and type.
// The slot is zeroed so no field is ever read uninitialised. The entry is not part of the builder until push_entry
static inline biu_status biu_prepare_entry(biu_builder* builder, const char* name, biu_type type) {
    size_t length = strlen(name);
    if (length > BIU_MAX_STRING)                            return biu_status_err_name_too_long;
    if (!biu_is_valid_ascii((const uint8_t*)name, length))  return biu_status_err_invalid_ascii;

    biu_status status = biu_reserve_entry(builder); if (status != biu_status_ok) return status;
    biu_builder_entry* entry = &builder->entries[builder->entries_count];
    memset(entry, 0, sizeof(*entry));
    memcpy(entry->name, name, length); entry->name_length = (uint8_t)length;
    entry->type = type; return biu_status_ok;
}

static inline void biu_push_entry(biu_builder* builder) {
    builder->entries_count++;
}

biu_status biu_builder_add_int(biu_builder* builder, const char* name, int64_t value) {
    if (!builder || !name) return biu_status_err_null_pointer;
    biu_status status = biu_prepare_entry(builder, name, biu_type_int);
    if (status) return status;
    builder->entries[builder->entries_count].value.as_int = value;
    biu_push_entry(builder); return biu_status_ok;
}

biu_status biu_builder_add_float(biu_builder* builder, const char* name, double value) {
    if (!builder || !name) return biu_status_err_null_pointer;
    biu_status status = biu_prepare_entry(builder, name, biu_type_float);
    if (status) return status;
    builder->entries[builder->entries_count].value.as_float = value;
    biu_push_entry(builder); return biu_status_ok;
}

biu_status biu_builder_add_label(biu_builder* builder, const char* name, const char* value, biu_access access) {
    if (!builder || !name || !value) return biu_status_err_null_pointer;
    size_t length = strlen(value); if (length > BIU_MAX_STRING) return biu_status_err_name_too_long;
    if (!biu_is_valid_ascii((const uint8_t*)value, length))    return biu_status_err_invalid_ascii;
    biu_status status = biu_prepare_entry(builder, name, biu_type_label); if (status) return status;
    status = biu_link_data(&builder->entries[builder->entries_count], length, value, access); if (status) return status;
    biu_push_entry(builder); return biu_status_ok;
}

biu_status biu_builder_add_binary(biu_builder* builder, const char* name, uint64_t bytes, const void* data, biu_access access) {
    if (!builder || !name || (!data && bytes != 0)) return biu_status_err_null_pointer;
    biu_status status = biu_prepare_entry(builder, name, biu_type_binary); if (status) return status;
    status = biu_link_data(&builder->entries[builder->entries_count], bytes, data, access); if (status) return status;
    biu_push_entry(builder); return biu_status_ok;
}

biu_status biu_builder_add_binary_compressed(biu_builder* builder, const char* name, uint64_t bytes, const void* data, int level) {
    if (!builder || !name || (!data && bytes != 0)) return biu_status_err_null_pointer;
    biu_status status = biu_prepare_entry(builder, name, biu_type_binary_compressed); if (status) return status;
    status = biu_link_data_and_compress(&builder->entries[builder->entries_count], bytes, data, level); if (status) return status;
    biu_push_entry(builder); return biu_status_ok;
}

// Lexicographical name comparison for qsort of builder entry objects
static int biu_name_comp_for_builder_entries(const void* a, const void* b) {
    const biu_builder_entry* entry_a = (const biu_builder_entry*)a;
    const biu_builder_entry* entry_b = (const biu_builder_entry*)b;
    return biu_name_comp(entry_a->name_length, entry_a->name, entry_b->name_length, entry_b->name);
}

// Get entry content size
static inline int biu_entry_data_bytes(const biu_builder_entry* entry, uint64_t* out) {
    switch (entry->type) {
    case biu_type_int:                  *out = 8;                               return 1;
    case biu_type_float:                *out = 8;                               return 1;
    case biu_type_label:                *out = entry->value.as_bytes.length;    return 1;
    case biu_type_binary:               *out = entry->value.as_bytes.length;    return 1;
    case biu_type_binary_compressed:    return biu_add_u64_checked(8, entry->value.as_bytes.length, out);
    } return 0; // unreachable
}

biu_status biu_builder_serialize(biu_builder* builder, void** out_buffer, uint64_t* out_bytes) {
    if (!builder || !out_buffer || !out_bytes) return biu_status_err_null_pointer;
    *out_buffer = NULL; *out_bytes = 0;

    // Spec: entries count is capped
    if (builder->entries_count > BIU_MAX_ENTRIES) return biu_status_err_too_many_entries;

    // Sort entries
    if (builder->entries_count > 1) {
        qsort(builder->entries, builder->entries_count, sizeof(biu_builder_entry), biu_name_comp_for_builder_entries);
    }

    // Ensure no names are equal, calculate names size, calculate content bytes
    // name_bytes cannot overflow: BIU_MAX_ENTRIES * 256 < 2^32
    // Sizes that do not fit in 64 bit are reported as allocation failure: such a file cannot be allocated anyway
    uint32_t name_bytes = 0; uint64_t content_bytes = 0;
    for (uint32_t i = 0; i < builder->entries_count; ++i) {
        const biu_builder_entry* curr = &builder->entries[i];
        name_bytes += 1u + curr->name_length;

        uint64_t data_bytes;

        // Spec: each content starts at 8-byte alignment
        if (!biu_align8_checked(content_bytes, &content_bytes))             return biu_status_err_allocation_failure;
        if (!biu_entry_data_bytes(curr, &data_bytes))                       return biu_status_err_allocation_failure;
        if (!biu_add_u64_checked(content_bytes, data_bytes, &content_bytes))return biu_status_err_allocation_failure;

        if (i == 0) continue;

        const biu_builder_entry* prev = &builder->entries[i - 1];
        if (prev->name_length == curr->name_length &&
            memcmp(prev->name, curr->name, prev->name_length) == 0) {
            return biu_status_err_duplicate_names;
        }
    }

    // Calculate section positions and required file size (header + index + names cannot overflow, < 2^33)
    uint64_t index_bytes   = (uint64_t)builder->entries_count * BIU_ENTRY_BYTES;
    uint64_t content_begin = biu_align8((uint64_t)BIU_HEADER_BYTES + index_bytes + name_bytes);
    uint64_t content_end, footer_begin, file_size;
    if (!biu_add_u64_checked(content_begin, content_bytes, &content_end))   return biu_status_err_allocation_failure;
    if (!biu_align8_checked(content_end, &footer_begin))                    return biu_status_err_allocation_failure;
    if (!biu_add_u64_checked(footer_begin, BIU_FOOTER_BYTES, &file_size))   return biu_status_err_allocation_failure;
    if (file_size > (uint64_t)SIZE_MAX)                                     return biu_status_err_allocation_failure;

    // Alloc with calloc to ensure padding bytes are 0-initialized
    uint8_t* result = (uint8_t*)calloc(1, (size_t)file_size);
    if (!result) return biu_status_err_allocation_failure;

    uint64_t position = 0;

    // Write header
    biu_write_bytes(result, &position, BIU_MAGIC_VALUE, 8);
    biu_write_u64(result,  &position, 0); // Version
    biu_write_u32(result,  &position, builder->entries_count);
    biu_write_u32(result,  &position, name_bytes);
    biu_write_u64(result,  &position, content_bytes);

    // Write index
    uint32_t name_offset = 0;
    uint64_t data_offset = 0;

    for (uint32_t i = 0; i < builder->entries_count; ++i) {
        const biu_builder_entry* entry = &builder->entries[i];
        uint64_t data_bytes = 0; biu_entry_data_bytes(entry, &data_bytes);  // Cannot fail, checked above
        data_offset = biu_align8(data_offset); // Spec: each content starts at 8-byte alignment

        biu_write_u32(result, &position, name_offset);
        biu_write_u32(result, &position, (uint32_t)entry->type);
        biu_write_u64(result, &position, data_bytes);   // Real length, a zero-length entry stays 0
        biu_write_u64(result, &position, data_offset);

        name_offset += 1u + entry->name_length;
        data_offset += data_bytes;
    }

    // Write names
    for (uint32_t i = 0; i < builder->entries_count; ++i) {
        const biu_builder_entry* entry = &builder->entries[i];
        biu_write_bytes(result, &position, &entry->name_length, 1);
        biu_write_bytes(result, &position, entry->name, entry->name_length);
    }

    // Write content
    position = biu_align8(position);
    for (uint32_t i = 0; i < builder->entries_count; ++i) {
        position = biu_align8(position); // Spec: each content starts at 8-byte alignment
        const biu_builder_entry* entry = &builder->entries[i];
        switch (entry->type) {
        case biu_type_int: {
            biu_write_i64(result, &position, entry->value.as_int);
        } break;
        case biu_type_float: {
            biu_write_double(result, &position, entry->value.as_float);
        } break;
        case biu_type_label: case biu_type_binary: {
            biu_write_bytes(result, &position, entry->value.as_bytes.begin, entry->value.as_bytes.length);
        } break;
        case biu_type_binary_compressed: {
            biu_write_u64  (result, &position, entry->value.as_bytes.uncompressed);
            biu_write_bytes(result, &position, entry->value.as_bytes.begin, entry->value.as_bytes.length);
        } break;
        }
    }

    // Write footer - checksum covers [first header byte, footer first byte)
    // including the padding before the footer
    position = footer_begin; // As per spec
    uint64_t checksum = biu_crc64(position, result);
    biu_write_u64(result, &position, checksum);
    biu_write_bytes(result, &position, BIU_MAGIC_VALUE, 8);

    // Return
    *out_buffer = result;
    *out_bytes  = position;
    return biu_status_ok;
}

#endif // BINARIUM_IMPL
