// Negative-test fixture: corrupt one selected mixed-dispatch result.
#include "blake3.h"
#include <stdlib.h>

void __real_blake3_hasher_finalize(const blake3_hasher *, uint8_t *, size_t);
void __wrap_blake3_hasher_finalize(const blake3_hasher *hasher, uint8_t *out,
                                   size_t len) {
  static unsigned int calls;
  __real_blake3_hasher_finalize(hasher, out, len);
  ++calls;
  const char *value = getenv("BLAKE3_TEST_CORRUPT_ITERATION");
  if (value) {
    int iteration = atoi(value);
    // Ten capability cases and four width cases precede the mixed loop.
    if (iteration >= 0 && iteration < 16 &&
        calls == 15U + (unsigned int)iteration && len)
      out[0] ^= 1;
  }
}
