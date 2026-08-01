# Binarium

Binarium is a lightweight, minimalist binary data format designed to support user-defined formats. It provides a simple indexed structure for efficient embedding of arbitrary data blocks, metadata, and binary assets while keeping access predictable and reasonably fast (O(log n)).

# Example

```c
#define BINARIUM_IMPL
#include "binarium/binarium.h"
#include <stdio.h>
#include <stdlib.h>

int main(void) {
    // Build a document
    sht_builder* b; sht_create_builder(&b);
    sht_builder_add_int64(b, "score", 1337);
    sht_builder_add_text (b, "title", 15, "Hello Binarium", sht_access_read_unowned);

    void* file; uint64_t bytes;
    sht_builder_serialize(b, &file, &bytes); // Serialize file (possibly for file disc write)
    sht_free_builder(b);

    // Open a zero-copy view over the serialized buffer
    sht_view* v; sht_create_view(&v, &(sht_view_create_info){
        .access = sht_access_claim_ownership,
        .buffer = file,
        .bytes  = bytes,
    });

    // Read entry
    uint32_t entry; if (sht_view_find(v, "title", &entry) == sht_status_ok) {
        uint64_t len; const uint8_t* text;
        sht_view_get_as_bytes(v, entry, &len, &text);
        printf("%.*s\n", (int)len, (const char*)text);
    }

    sht_free_view(v);
}
```

# Contents

This repository contains binarium standard documents in ``formats`` folder, and a C99 single-header-library for binarium read/write inside ``include/binarium``.

# Building

Binarium reader/viewer library is implemented as a single-header-library. To build it, in a .c file include:
```
#define BINARIUM_IMPL
#include "binarium/binarium.h"
```
Preferably use an empty file, to avoid naming collision.  

Binarium depends on the Zstandard (Zstd) compression library. You must install a version compatible with the format version specified by the Binarium specification. https://github.com/facebook/zstd

When compiling your application, ensure the Zstandard headers are available in your include path and the library is linked.

# Portability

Format uses uint64 for data sizes - which practically discards 32bit systems.
The implementation also does not allow 32 bit systmes.
Also implementation does not support machines that use ``double`` standard other than ``IEEE 754 binary64.``

# Versions

| Index | Markdown Document | Changes | Implementations |
| - | - | - | - |
| 0 | [Format Version 0](formats/V0.md) | First Version | [C99 API](include/binarium/binarium.h)

## License

This project is licensed under MIT License. See [LICENSE.md](LICENSE.md).

## Zstandard license

This project uses Zstandard (zstd).
Copyright (c) Meta Platforms, Inc. and contributors.
Licensed under the BSD 3-Clause License or GPLv2.
Zstandard: https://github.com/facebook/zstd
