#include <stddef.h>

// Called only after HWCAP2_SVE2 has been checked. Do not use svcntb() here:
// -msve-vector-bits=128 permits the compiler to fold it to a constant.
size_t blake3_sve2_vector_length(void) {
  size_t bytes;
  __asm__ volatile("cntb %0" : "=r"(bytes));
  return bytes;
}
