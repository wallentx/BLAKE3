#include "arm_dispatch_trace.h"
#include <stdatomic.h>
#include <stdio.h>
#include <sys/auxv.h>

static _Atomic unsigned long calls[5];
static _Thread_local unsigned long allowed = ~0UL;
static _Thread_local size_t sve_length, sme_length;
static _Atomic bool mixed_dispatch;
static _Atomic unsigned long mixed_sequence;
void trace_mask(unsigned long mask) { allowed = mask; }
void trace_lengths(size_t sve, size_t sme) {
  sve_length = sve;
  sme_length = sme;
}
void trace_mix_batches(bool enabled) { atomic_store(&mixed_dispatch, enabled); }
size_t __real_blake3_sve2_vector_length(void);
size_t __real_blake3_sme_vector_length(void);
size_t __wrap_blake3_sve2_vector_length(void) {
  return sve_length ? sve_length : __real_blake3_sve2_vector_length();
}
size_t __wrap_blake3_sme_vector_length(void) {
  return sme_length ? sme_length : __real_blake3_sme_vector_length();
}
void trace_reset(void) {
  for (int i = 0; i < 5; i++)
    atomic_store(&calls[i], 0);
  atomic_store(&mixed_sequence, 0);
}
unsigned long trace_count(int backend) { return atomic_load(&calls[backend]); }
unsigned long __real_getauxval(unsigned long type);
unsigned long __wrap_getauxval(unsigned long type) {
  unsigned long value = __real_getauxval(type);
  return type == AT_HWCAP2 ? value & allowed : value;
}

void __real_blake3_hash_many(const uint8_t *const *a, size_t b, size_t c,
                             const uint32_t *d, uint64_t e, bool f, uint8_t g,
                             uint8_t h, uint8_t i, uint8_t *j);
void __wrap_blake3_hash_many(const uint8_t *const *a, size_t b, size_t c,
                             const uint32_t *d, uint64_t e, bool f, uint8_t g,
                             uint8_t h, uint8_t i, uint8_t *j) {
  bool mix = atomic_load(&mixed_dispatch);
  size_t saved_sve = sve_length, saved_sme = sme_length;
  if (mix) {
    // Every hash traverses all three eligibility classes, even if TBB executes
    // all tasks on one worker. Overrides remain local to the executing thread.
    unsigned long kind = atomic_fetch_add(&mixed_sequence, 1) % 3;
    sve_length = kind == 2 ? 32 : 0;
    sme_length = kind == 0 ? 0 : 16;
  }
  __real_blake3_hash_many(a, b, c, d, e, f, g, h, i, j);
  if (mix) {
    sve_length = saved_sve;
    sme_length = saved_sme;
  }
}
#define WRAP(name, id)                                                         \
  void __real_blake3_hash_many_##name(                                         \
      const uint8_t *const *a, size_t b, size_t c, const uint32_t *d,          \
      uint64_t e, bool f, uint8_t g, uint8_t h, uint8_t i, uint8_t *j);        \
  void __wrap_blake3_hash_many_##name(                                         \
      const uint8_t *const *a, size_t b, size_t c, const uint32_t *d,          \
      uint64_t e, bool f, uint8_t g, uint8_t h, uint8_t i, uint8_t *j) {       \
    atomic_fetch_add(&calls[id], 1);                                           \
    __real_blake3_hash_many_##name(a, b, c, d, e, f, g, h, i, j);              \
  }
WRAP(portable, BLAKE3_ARM_PORTABLE)
WRAP(neon, BLAKE3_ARM_NEON)
WRAP(sve2, BLAKE3_ARM_SVE2)
WRAP(sme, BLAKE3_ARM_SME)
WRAP(sme2, BLAKE3_ARM_SME2)
