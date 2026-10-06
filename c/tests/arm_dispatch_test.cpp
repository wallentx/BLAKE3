#include "arm_dispatch_trace.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#if defined(BLAKE3_USE_TBB)
#include <oneapi/tbb/global_control.h>
#endif
int main() {
  // The instrumented cases only mask capabilities/lengths down. Require the
  // full native/emulated profile before simulating narrower eligibility.
  if (!blake3_sve2_supported() || !blake3_sme2_supported()) {
    fputs("SKIP: requires 128-bit SVE2 and at least 512-bit SME2\n", stderr);
    return 77;
  }
  const int policy = BLAKE3_PREFER_SME;
  constexpr unsigned long SVE2 = 1UL << 1, SME = 1UL << 23, SME2 = 1UL << 37;
  unsigned long masks[] = {~0UL, SVE2, SME, SME | SME2, 0};
  for (auto mask : masks)
    for (size_t length : {size_t(4096), size_t(1048576)}) {
      trace_mask(mask);
      trace_reset();
      std::vector<uint8_t> data(length);
      for (size_t i = 0; i < length; i++)
        data[i] = i % 251;
      blake3_hasher h;
      blake3_hasher_init(&h);
      blake3_hasher_update(&h, data.data(), length);
      uint8_t digest[32];
      blake3_hasher_finalize(&h, digest, 32);
      bool want_sme = policy == 1 && length >= 16384 && (mask & SME);
      int wanted = want_sme ? (mask & SME2 ? BLAKE3_ARM_SME2 : BLAKE3_ARM_SME)
                            : (mask & SVE2 ? BLAKE3_ARM_SVE2 : BLAKE3_ARM_NEON);
      if (!trace_count(wanted)) {
        fprintf(stderr, "missing expected backend %d\n", wanted);
        return 4;
      }
      if (!want_sme &&
          (trace_count(BLAKE3_ARM_SME) || trace_count(BLAKE3_ARM_SME2)))
        return 5;
      if (!(mask & SVE2) && trace_count(BLAKE3_ARM_SVE2))
        return 6;
      printf("{\"policy\":%d,\"mask\":%lu,\"bytes\":%zu,\"degree\":%zu,"
             "\"calls\":[",
             policy, mask, length, blake3_simd_degree());
      for (int i = 0; i < 5; i++)
        printf("%s%lu", i ? "," : "", trace_count(i));
      printf("],\"digest\":\"");
      for (auto b : digest)
        printf("%02x", b);
      puts("\"}");
    }
  for (size_t sve : {size_t(16), size_t(32)})
    for (size_t sme : {size_t(16), size_t(64)}) {
      trace_mask(~0UL);
      trace_lengths(sve, sme);
      trace_reset();
      std::vector<uint8_t> input(1048576);
      for (size_t i = 0; i < input.size(); i++)
        input[i] = i % 251;
      blake3_hasher h;
      blake3_hasher_init(&h);
      blake3_hasher_update(&h, input.data(), input.size());
      uint8_t digest[32];
      blake3_hasher_finalize(&h, digest, 32);
      int wanted = policy == 1 && sme == 64 ? BLAKE3_ARM_SME2
                   : sve == 16              ? BLAKE3_ARM_SVE2
                                            : BLAKE3_ARM_NEON;
      if (!trace_count(wanted))
        return 9;
      if (sve != 16 && trace_count(BLAKE3_ARM_SVE2))
        return 10;
      if (sme < 64 &&
          (trace_count(BLAKE3_ARM_SME) || trace_count(BLAKE3_ARM_SME2)))
        return 11;
      printf("{\"policy\":%d,\"simulated_sve_bytes\":%zu,\"simulated_sme_"
             "bytes\":%zu,\"bytes\":1048576,\"calls\":[",
             policy, sve, sme);
      for (int i = 0; i < 5; i++)
        printf("%s%lu", i ? "," : "", trace_count(i));
      printf("],\"digest\":\"");
      for (auto b : digest)
        printf("%02x", b);
      puts("\"}");
    }
  trace_lengths(0, 0);
#if defined(BLAKE3_USE_TBB)
  oneapi::tbb::global_control limit(
      oneapi::tbb::global_control::max_allowed_parallelism, 3);
#endif
  trace_mask(~0UL);
  trace_mix_batches(true);
  std::vector<uint8_t> data(1048576);
  for (size_t i = 0; i < data.size(); i++)
    data[i] = i % 251;
  // Independent Rust reference: one MiB with byte i equal to i % 251.
  constexpr uint8_t expected[32] = {
      0x74, 0xcb, 0x44, 0x1f, 0xd0, 0x87, 0x76, 0x4c, 0xa9, 0xc3, 0x69,
      0x4d, 0xa7, 0x42, 0xeb, 0xe3, 0x0c, 0xbe, 0xb3, 0x06, 0x0a, 0x17,
      0x00, 0x9c, 0xa8, 0x18, 0x25, 0xc7, 0xa8, 0xd1, 0x03, 0x43};
  uint8_t digest[32];
  for (int iteration = 0; iteration < 16; iteration++) {
    trace_reset();
    blake3_hasher h;
    blake3_hasher_init(&h);
#if defined(BLAKE3_USE_TBB)
    blake3_hasher_update_tbb(&h, data.data(), data.size());
#else
    blake3_hasher_update(&h, data.data(), data.size());
#endif
    blake3_hasher_finalize(&h, digest, 32);
    if (memcmp(digest, expected, sizeof(digest))) {
      fprintf(stderr, "mixed-dispatch digest mismatch at iteration %d\n",
              iteration);
      return 12;
    }
    if (!trace_count(BLAKE3_ARM_NEON) || !trace_count(BLAKE3_ARM_SVE2))
      return 7;
    if (policy == 1 && !trace_count(BLAKE3_ARM_SME2))
      return 8;
    printf("{\"policy\":%d,\"mixed_dispatch_simulated_lengths\":true,"
           "\"iteration\":%d,\"bytes\":1048576,\"calls\":[",
           policy, iteration);
    for (int i = 0; i < 5; i++)
      printf("%s%lu", i ? "," : "", trace_count(i));
    printf("],\"digest\":\"");
    for (auto b : digest)
      printf("%02x", b);
    puts("\"}");
  }
  trace_mix_batches(false);
}
