#ifndef BLAKE3_ARM_DISPATCH_TRACE_H
#define BLAKE3_ARM_DISPATCH_TRACE_H

#include "blake3_impl.h"

enum blake3_arm_backend {
  BLAKE3_ARM_PORTABLE,
  BLAKE3_ARM_NEON,
  BLAKE3_ARM_SVE2,
  BLAKE3_ARM_SME,
  BLAKE3_ARM_SME2,
};

#ifdef __cplusplus
extern "C" {
#endif
void trace_mask(unsigned long mask);
void trace_lengths(size_t sve, size_t sme);
void trace_mix_batches(bool enabled);
void trace_reset(void);
unsigned long trace_count(int backend);
#ifdef __cplusplus
}
#endif

#endif
