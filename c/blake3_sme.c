#include "blake3_impl.h"

#include <arm_sme.h>
#include <arm_sve.h>

// This backend uses base SME, including streaming SVE's XAR instruction. SME2
// is not required. Compile this file separately with -march=armv8-a+sme.
// The caller must check blake3_sme_supported() before calling this function.
// A streaming vector length of at least 512 bits is required: each ZA0.S row
// holds one complete input block, and its columns hold words from 16 inputs.
// Predicates restrict wider implementations to the same 16-input batch size.

// SVE vector types cannot be array elements. Keep the state in named vectors.
#define MESSAGE(i) svread_ver_za32_u32_m(svdup_n_u32(0), lanes, 0, (i))
#define ADD(a, b) svadd_u32_x(lanes, (a), (b))
#define XOR(a, b) sveor_u32_x(lanes, (a), (b))
#define G(a, b, c, d, x, y)                                                    \
  do {                                                                         \
    a = ADD(ADD(a, b), MESSAGE(x));                                            \
    d = svxar_n_u32(d, a, 16);                                                 \
    c = ADD(c, d);                                                             \
    b = svxar_n_u32(b, c, 12);                                                 \
    a = ADD(ADD(a, b), MESSAGE(y));                                            \
    d = svxar_n_u32(d, a, 8);                                                  \
    c = ADD(c, d);                                                             \
    b = svxar_n_u32(b, c, 7);                                                  \
  } while (0)
#define ROUND(r)                                                               \
  do {                                                                         \
    G(v0, v4, v8, v12, MSG_SCHEDULE[r][0], MSG_SCHEDULE[r][1]);                \
    G(v1, v5, v9, v13, MSG_SCHEDULE[r][2], MSG_SCHEDULE[r][3]);                \
    G(v2, v6, v10, v14, MSG_SCHEDULE[r][4], MSG_SCHEDULE[r][5]);               \
    G(v3, v7, v11, v15, MSG_SCHEDULE[r][6], MSG_SCHEDULE[r][7]);               \
    G(v0, v5, v10, v15, MSG_SCHEDULE[r][8], MSG_SCHEDULE[r][9]);               \
    G(v1, v6, v11, v12, MSG_SCHEDULE[r][10], MSG_SCHEDULE[r][11]);             \
    G(v2, v7, v8, v13, MSG_SCHEDULE[r][12], MSG_SCHEDULE[r][13]);              \
    G(v3, v4, v9, v14, MSG_SCHEDULE[r][14], MSG_SCHEDULE[r][15]);              \
  } while (0)

__arm_new("za") __arm_locally_streaming void blake3_hash_many_sme(
    const uint8_t *const *inputs, size_t num_inputs, size_t blocks,
    const uint32_t key[8], uint64_t counter, bool increment_counter,
    uint8_t flags, uint8_t flags_start, uint8_t flags_end, uint8_t *out) {
  const svbool_t block_words = svwhilelt_b32_u32(0, 16);
  const svbool_t cv_words = svwhilelt_b32_u32(0, 8);
  while (num_inputs > 0) {
    const size_t batch = num_inputs < 16 ? num_inputs : 16;
    const svbool_t lanes = svwhilelt_b32_u64(0, batch);
    const svuint32_t offsets =
        increment_counter ? svindex_u32(0, 1) : svdup_n_u32(0);
    const svuint32_t counter_low = ADD(svdup_n_u32((uint32_t)counter), offsets);
    const svbool_t carry = svcmplt_n_u32(lanes, counter_low, (uint32_t)counter);
    const svuint32_t counter_high =
        ADD(svdup_n_u32((uint32_t)(counter >> 32)),
            svsel_u32(carry, svdup_n_u32(1), svdup_n_u32(0)));
    svuint32_t h0 = svdup_n_u32(key[0]);
    svuint32_t h1 = svdup_n_u32(key[1]);
    svuint32_t h2 = svdup_n_u32(key[2]);
    svuint32_t h3 = svdup_n_u32(key[3]);
    svuint32_t h4 = svdup_n_u32(key[4]);
    svuint32_t h5 = svdup_n_u32(key[5]);
    svuint32_t h6 = svdup_n_u32(key[6]);
    svuint32_t h7 = svdup_n_u32(key[7]);
    for (size_t block = 0; block < blocks; block++) {
      for (uint32_t lane = 0; lane < batch; lane++) {
        svld1_hor_za32(0, lane, block_words,
                       inputs[lane] + block * BLAKE3_BLOCK_LEN);
      }
      uint8_t block_flags = flags;
      if (block == 0) {
        block_flags |= flags_start;
      }
      if (block + 1 == blocks) {
        block_flags |= flags_end;
      }
      svuint32_t v0 = h0, v1 = h1, v2 = h2, v3 = h3;
      svuint32_t v4 = h4, v5 = h5, v6 = h6, v7 = h7;
      svuint32_t v8 = svdup_n_u32(IV[0]), v9 = svdup_n_u32(IV[1]);
      svuint32_t v10 = svdup_n_u32(IV[2]), v11 = svdup_n_u32(IV[3]);
      svuint32_t v12 = counter_low, v13 = counter_high;
      svuint32_t v14 = svdup_n_u32(BLAKE3_BLOCK_LEN);
      svuint32_t v15 = svdup_n_u32(block_flags);
      ROUND(0);
      ROUND(1);
      ROUND(2);
      ROUND(3);
      ROUND(4);
      ROUND(5);
      ROUND(6);
      h0 = XOR(v0, v8);
      h1 = XOR(v1, v9);
      h2 = XOR(v2, v10);
      h3 = XOR(v3, v11);
      h4 = XOR(v4, v12);
      h5 = XOR(v5, v13);
      h6 = XOR(v6, v14);
      h7 = XOR(v7, v15);
    }
    // Transpose the chaining values back into contiguous little-endian outputs.
    svwrite_hor_za32_u32_m(0, 0, lanes, h0);
    svwrite_hor_za32_u32_m(0, 1, lanes, h1);
    svwrite_hor_za32_u32_m(0, 2, lanes, h2);
    svwrite_hor_za32_u32_m(0, 3, lanes, h3);
    svwrite_hor_za32_u32_m(0, 4, lanes, h4);
    svwrite_hor_za32_u32_m(0, 5, lanes, h5);
    svwrite_hor_za32_u32_m(0, 6, lanes, h6);
    svwrite_hor_za32_u32_m(0, 7, lanes, h7);
    for (uint32_t lane = 0; lane < batch; lane++) {
      svst1_ver_za32(0, lane, cv_words, out + lane * BLAKE3_OUT_LEN);
    }
    inputs += batch;
    num_inputs -= batch;
    out += batch * BLAKE3_OUT_LEN;
    if (increment_counter) {
      counter += batch;
    }
  }
}

#undef MESSAGE
#undef ADD
#undef XOR
#undef G
#undef ROUND
