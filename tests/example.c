#define BINARIUM_IMPL
#include "binarium/binarium.h"
#include <stdio.h>
#include <stdlib.h>

int main(void) {
    // Build a document
    biu_builder* b; biu_create_builder(&b);
    biu_builder_add_int64(b, "score", 1337);
    biu_builder_add_text (b, "title", 15, "Hello Binarium", biu_access_read_unowned);

    void* file; uint64_t bytes;
    biu_builder_serialize(b, &file, &bytes); // Serialize file (possibly for file disc write)
    biu_free_builder(b);

    // Open a zero-copy view over the serialized buffer
    biu_view* v; biu_create_view(&v, &(biu_view_create_info){
        .access = biu_access_claim_ownership,
        .buffer = file,
        .bytes  = bytes,
    });

    // Read entry
    uint32_t entry; if (biu_view_find(v, "title", &entry) == biu_status_ok) {
        uint64_t len; const uint8_t* text;
        biu_view_get_as_bytes(v, entry, &len, &text);
        printf("%.*s\n", (int)len, (const char*)text);
    }

    biu_free_view(v);
}
