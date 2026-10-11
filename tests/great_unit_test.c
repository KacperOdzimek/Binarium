#include "../include/binarium/binarium.h"

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static void test_builder_create_destroy(void) {
    biu_builder* builder = NULL;

    assert(biu_create_builder(&builder) == biu_status_ok);
    assert(builder != NULL);

    biu_free_builder(builder);
}

static void test_builder_add_int(void) {
    biu_builder* builder = NULL;
    void* buffer = NULL;
    size_t bytes = 0;

    assert(biu_create_builder(&builder) == biu_status_ok);
    assert(biu_builder_add_int(builder, "value", 123456789) == biu_status_ok);
    assert(biu_builder_serialize(builder, &buffer, &bytes) == biu_status_ok);

    assert(buffer != NULL);
    assert(bytes > 0);

    free(buffer);
    biu_free_builder(builder);
}

static void test_builder_add_negative_int(void) {
    biu_builder* builder = NULL;
    void* buffer = NULL;
    size_t bytes = 0;

    assert(biu_create_builder(&builder) == biu_status_ok);
    assert(biu_builder_add_int(builder, "negative", -987654321) == biu_status_ok);
    assert(biu_builder_serialize(builder, &buffer, &bytes) == biu_status_ok);

    free(buffer);
    biu_free_builder(builder);
}

static void test_builder_add_float(void) {
    biu_builder* builder = NULL;
    void* buffer = NULL;
    size_t bytes = 0;

    assert(biu_create_builder(&builder) == biu_status_ok);
    assert(biu_builder_add_float(builder, "pi", 3.1415926535) == biu_status_ok);
    assert(biu_builder_serialize(builder, &buffer, &bytes) == biu_status_ok);

    free(buffer);
    biu_free_builder(builder);
}

static void test_builder_add_binary_copy(void) {
    biu_builder* builder = NULL;
    void* buffer = NULL;
    size_t bytes = 0;

    uint8_t data[] = {1, 2, 3, 4, 5};

    assert(biu_create_builder(&builder) == biu_status_ok);
    assert(biu_builder_add_binary(builder, "bin", sizeof(data), data, biu_access_make_copy) == biu_status_ok);
    assert(biu_builder_serialize(builder, &buffer, &bytes) == biu_status_ok);

    free(buffer);
    biu_free_builder(builder);
}

static void test_builder_add_compressed_binary(void) {
    biu_builder* builder = NULL;
    void* buffer = NULL;
    size_t bytes = 0;

    uint8_t data[1024];

    memset(data, 0xaa, sizeof(data));

    assert(biu_create_builder(&builder) == biu_status_ok);
    assert(biu_builder_add_binary_compressed(builder, "binary", sizeof(data), data, 1) == biu_status_ok);
    assert(biu_builder_serialize(builder, &buffer, &bytes) == biu_status_ok);

    free(buffer);
    biu_free_builder(builder);
}

static void test_duplicate_names(void) {
    biu_builder* builder = NULL;
    void* buffer = NULL;
    size_t bytes = 0;

    assert(biu_create_builder(&builder) == biu_status_ok);

    assert(biu_builder_add_int(builder, "same", 1) == biu_status_ok);
    assert(biu_builder_add_int(builder, "same", 2) == biu_status_ok);

    assert(biu_builder_serialize(builder, &buffer, &bytes) == biu_status_err_duplicate_names);

    biu_free_builder(builder);
}

static void test_viewer_create_invalid_buffer(void) {
    biu_viewer* viewer = NULL;
    biu_viewer_create_info info = {
        .access = biu_access_read_unowned,
        .buffer = NULL,
        .bytes = 0
    };

    assert(biu_create_viewer(&viewer, &info) != biu_status_ok);
}

static void test_roundtrip_int(void) {
    biu_builder* builder = NULL;
    biu_viewer* viewer = NULL;
    void* buffer = NULL;
    size_t bytes = 0;
    int64_t value = 0;

    assert(biu_create_builder(&builder) == biu_status_ok);
    assert(biu_builder_add_int(builder, "number", 42) == biu_status_ok);
    assert(biu_builder_serialize(builder, &buffer, &bytes) == biu_status_ok);

    biu_viewer_create_info info = {
        .access = biu_access_read_unowned,
        .buffer = buffer,
        .bytes = bytes
    };

    assert(biu_create_viewer(&viewer, &info) == biu_status_ok);
    assert(biu_viewer_get_as_int(viewer, 0, &value) == biu_status_ok);
    assert(value == 42);

    biu_free_viewer(viewer);
    free(buffer);
    biu_free_builder(builder);
}

static void test_roundtrip_float(void) {
    biu_builder* builder = NULL;
    biu_viewer* viewer = NULL;
    void* buffer = NULL;
    size_t bytes = 0;
    double value = 0;

    assert(biu_create_builder(&builder) == biu_status_ok);
    assert(biu_builder_add_float(builder, "number", 1.25) == biu_status_ok);
    assert(biu_builder_serialize(builder, &buffer, &bytes) == biu_status_ok);

    biu_viewer_create_info info = {
        .access = biu_access_read_unowned,
        .buffer = buffer,
        .bytes = bytes
    };

    assert(biu_create_viewer(&viewer, &info) == biu_status_ok);
    assert(biu_viewer_get_as_float(viewer, 0, &value) == biu_status_ok);
    assert(value == 1.25);

    biu_free_viewer(viewer);
    free(buffer);
    biu_free_builder(builder);
}

static void test_viewer_query_entry_count(void) {
    biu_builder* builder = NULL;
    biu_viewer* viewer = NULL;
    void* buffer = NULL;
    size_t bytes = 0;
    uint32_t count = 0;

    assert(biu_create_builder(&builder) == biu_status_ok);

    assert(biu_builder_add_int(builder, "a", 1) == biu_status_ok);
    assert(biu_builder_add_int(builder, "b", 2) == biu_status_ok);
    assert(biu_builder_add_int(builder, "c", 3) == biu_status_ok);

    assert(biu_builder_serialize(builder, &buffer, &bytes) == biu_status_ok);

    biu_viewer_create_info info = {
        .access = biu_access_read_unowned,
        .buffer = buffer,
        .bytes = bytes
    };

    assert(biu_create_viewer(&viewer, &info) == biu_status_ok);
    assert(biu_viewer_query_entry_count(viewer, &count) == biu_status_ok);
    assert(count == 3);

    biu_free_viewer(viewer);
    free(buffer);
    biu_free_builder(builder);
}

static void test_viewer_find_existing(void) {
    biu_builder* builder = NULL;
    biu_viewer* viewer = NULL;
    void* buffer = NULL;
    size_t bytes = 0;
    uint32_t entry = 999;

    assert(biu_create_builder(&builder) == biu_status_ok);
    assert(biu_builder_add_int(builder, "hello", 1) == biu_status_ok);
    assert(biu_builder_serialize(builder, &buffer, &bytes) == biu_status_ok);

    biu_viewer_create_info info = {
        .access = biu_access_read_unowned,
        .buffer = buffer,
        .bytes = bytes
    };

    assert(biu_create_viewer(&viewer, &info) == biu_status_ok);
    assert(biu_viewer_find(viewer, "hello", &entry) == biu_status_ok);

    biu_free_viewer(viewer);
    free(buffer);
    biu_free_builder(builder);
}

static void test_viewer_find_missing(void) {
    biu_builder* builder = NULL;
    biu_viewer* viewer = NULL;
    void* buffer = NULL;
    size_t bytes = 0;
    uint32_t entry = 0;

    assert(biu_create_builder(&builder) == biu_status_ok);
    assert(biu_builder_add_int(builder, "hello", 1) == biu_status_ok);
    assert(biu_builder_serialize(builder, &buffer, &bytes) == biu_status_ok);

    biu_viewer_create_info info = {
        .access = biu_access_read_unowned,
        .buffer = buffer,
        .bytes = bytes
    };

    assert(biu_create_viewer(&viewer, &info) == biu_status_ok);
    assert(biu_viewer_find(viewer, "world", &entry) == biu_status_err_no_entry);

    biu_free_viewer(viewer);
    free(buffer);
    biu_free_builder(builder);
}

static void test_grand_roundtrip(void) {
    biu_builder* builder = NULL;
    biu_viewer* viewer = NULL;
    void* buffer = NULL;
    size_t bytes = 0;

    uint32_t count = 0;
    uint32_t entry = 0;

    int64_t int_value = 0;
    double float_value = 0;
    uint64_t data_bytes = 0;
    const uint8_t* data_ptr = NULL;

    uint8_t binary[4096];
    memset(binary, 0x5a, sizeof(binary));

    assert(biu_create_builder(&builder) == biu_status_ok);

    /* Many integer entries */
    for (int i = 0; i < 100; i++) {
        char name[32];
        snprintf(name, sizeof(name), "int_%d", i);

        assert(biu_builder_add_int(builder, name, i * 1000) == biu_status_ok);
    }

    /* Many float entries */
    for (int i = 0; i < 50; i++) {
        char name[32];
        snprintf(name, sizeof(name), "float_%d", i);

        assert(biu_builder_add_float(builder, name, i * 0.5) == biu_status_ok);
    }

    /* Large binary payload */
    assert(biu_builder_add_binary(
        builder,
        "large_binary",
        sizeof(binary),
        binary,
        1
    ) == biu_status_ok);

    assert(biu_builder_serialize(builder, &buffer, &bytes) == biu_status_ok);
    assert(buffer != NULL);
    assert(bytes > 0);

    biu_viewer_create_info info = {
        .access = biu_access_read_unowned,
        .buffer = buffer,
        .bytes = bytes
    };

    assert(biu_create_viewer(&viewer, &info) == biu_status_ok);

    assert(biu_viewer_query_entry_count(viewer, &count) == biu_status_ok);
    assert(count == 151); /* 100 ints + 50 floats + 1 payload */

    /* Verify integer entry */
    assert(biu_viewer_find(viewer, "int_42", &entry) == biu_status_ok);
    assert(biu_viewer_get_as_int(viewer, entry, &int_value) == biu_status_ok);
    assert(int_value == 42000);

    /* Verify float entry */
    assert(biu_viewer_find(viewer, "float_10", &entry) == biu_status_ok);
    assert(biu_viewer_get_as_float(viewer, entry, &float_value) == biu_status_ok);
    assert(float_value == 5.0);

    /* Verify binary payload */
    assert(biu_viewer_find(viewer, "large_binary", &entry) == biu_status_ok);
    assert(biu_viewer_get_bytes(viewer, entry, &data_bytes, &data_ptr) == biu_status_ok);

    assert(data_bytes == sizeof(binary));
    assert(memcmp(data_ptr, binary, sizeof(binary)) == 0);

    biu_free_viewer(viewer);
    free(buffer);
    biu_free_builder(builder);
}

static void test_viewer_query_name(void) {
    biu_builder* builder = NULL;
    biu_viewer* viewer = NULL;
    void* buffer = NULL;
    size_t bytes = 0;

    uint8_t name_bytes = 0;
    const char* name = NULL;

    assert(biu_create_builder(&builder) == biu_status_ok);
    assert(biu_builder_add_int(builder, "hello", 123) == biu_status_ok);
    assert(biu_builder_serialize(builder, &buffer, &bytes) == biu_status_ok);

    biu_viewer_create_info info = {
        .access = biu_access_read_unowned,
        .buffer = buffer,
        .bytes = bytes
    };

    assert(biu_create_viewer(&viewer, &info) == biu_status_ok);
    assert(biu_viewer_query_name(viewer, 0, &name_bytes, &name) == biu_status_ok);
    assert(name_bytes == 5);
    assert(memcmp(name, "hello", name_bytes) == 0);

    biu_free_viewer(viewer);
    free(buffer);
    biu_free_builder(builder);
}

void main(void) {
    test_builder_create_destroy();
    test_builder_add_int();
    test_builder_add_negative_int();
    test_builder_add_float();
    test_builder_add_binary_copy();
    test_builder_add_compressed_binary();
    test_duplicate_names();

    test_viewer_create_invalid_buffer();

    test_roundtrip_int();
    test_roundtrip_float();
    test_viewer_query_entry_count();
    test_viewer_find_existing();
    test_viewer_find_missing();
    
    test_grand_roundtrip();
    test_viewer_query_name();

    printf("All good\n");
}

#define BINARIUM_IMPL
#include "../include/binarium/binarium.h"
