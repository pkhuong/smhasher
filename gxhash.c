#include "gxhash.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#if defined(__SSE__) && defined(__AES__)
#include <immintrin.h>

#if defined(HAVE_AVX2) && defined(__VAES__)
typedef __m256i state;
typedef __m128i output;

static inline state create_empty() {
    return _mm256_setzero_si256();
}

static inline state load_unaligned(const state* p) {
    return _mm256_loadu_si256(p);
}

static inline int check_same_page(const state* ptr) {
    uintptr_t address = (uintptr_t)ptr;
    uintptr_t offset_within_page = address & 0xFFF;
    return offset_within_page <= (4096 - sizeof(state) - 1);
}

static inline state get_partial_safe(const uint8_t* data, size_t len) {
    uint8_t buffer[sizeof(state)] = {0};
    memcpy(buffer, data, len);
    return _mm256_loadu_si256((const state*)buffer);
}

static inline state get_partial(const state* p, intptr_t len) {
    static const uint8_t MASK[2 * sizeof(state)] = {
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
    };

    if (check_same_page(p)) {
        const uint8_t* mask_ptr = &MASK[32 - len];
        __m256i mask = _mm256_loadu_si256((const __m256i*)mask_ptr);
        return _mm256_and_si256(_mm256_loadu_si256(p), mask);
    } else {
        return get_partial_safe((const uint8_t*)p, (size_t)len);
    }
}

static inline state compress(state a, state b) {
    state keys_1 = _mm256_set_epi32(0xFC3BC28E, 0x89C222E5, 0xB09D3E21, 0xF2784542, 0x4155EE07, 0xC897CCE2, 0x780AF2C3, 0x8A72B781);
    state keys_2 = _mm256_set_epi32(0x03FCE279, 0xCB6B2E9B, 0xB361DC58, 0x39136BD9, 0x7A83D76B, 0xB1E8F9F0, 0x028925A8, 0x3B9A4E71);

    b = _mm256_aesenc_epi128(b, keys_1);
    b = _mm256_aesenc_epi128(b, keys_2);

    return _mm256_aesenclast_epi128(a, b);
}

static inline state compress_fast(state a, state b) {
    return _mm256_aesenc_epi128(a, b);
}

static inline output finalize(state hash, uint32_t seed) {
    __m128i lower = _mm256_castsi256_si128(hash);
    __m128i upper = _mm256_extracti128_si256(hash, 1);
    __m128i hash128 = _mm_xor_si128(lower, upper);

    __m128i keys_1 = _mm_set_epi32(0x5A3BC47E, 0x89F216D5, 0xB09D2F61, 0xE37845F2);
    __m128i keys_2 = _mm_set_epi32(0xE7554D6F, 0x6EA75BBA, 0xDE3A74DB, 0x3D423129);
    __m128i keys_3 = _mm_set_epi32(0xC992E848, 0xA735B3F2, 0x790FC729, 0x444DF600);

    hash128 = _mm_aesenc_si128(hash128, _mm_set1_epi32(seed));
    hash128 = _mm_aesenc_si128(hash128, keys_1);
    hash128 = _mm_aesenc_si128(hash128, keys_2);
    hash128 = _mm_aesenclast_si128(hash128, keys_3);

    return hash128;
}
#else // __AVX2__ && __VAES__
typedef __m128i state;
typedef __m128i output;

static inline state create_empty() {
    return _mm_setzero_si128();
}

static inline state load_unaligned(const state* p) {
    return _mm_loadu_si128(p);
}

static inline int check_same_page(const state* ptr) {
    uintptr_t address = (uintptr_t)ptr;
    uintptr_t offset_within_page = address & 0xFFF;
    return offset_within_page <= (4096 - sizeof(state) - 1);
}

static inline state get_partial_safe(const uint8_t* data, size_t len) {
    uint8_t buffer[sizeof(state)] = {0};
    memcpy(buffer, data, len);
    return _mm_loadu_si128((const state*)buffer);
}

static inline state get_partial(const state* p, intptr_t len) {
    static const uint8_t MASK[2 * sizeof(state)] = {
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
    };

    if (check_same_page(p)) {
        const uint8_t* mask_ptr = &MASK[16 - len];
        state mask = _mm_loadu_si128((const state*)mask_ptr);
        return _mm_and_si128(_mm_loadu_si128(p), mask);
    } else {
        return get_partial_safe((const uint8_t*)p, (size_t)len);
    }
}

static inline state compress(state a, state b) {
    state keys_1 = _mm_set_epi32(0xFC3BC28E, 0x89C222E5, 0xB09D3E21, 0xF2784542);
    state keys_2 = _mm_set_epi32(0x03FCE279, 0xCB6B2E9B, 0xB361DC58, 0x39136BD9);

    b = _mm_aesenc_si128(b, keys_1);
    b = _mm_aesenc_si128(b, keys_2);

    return _mm_aesenclast_si128(a, b);
}

static inline state compress_fast(state a, state b) {
    return _mm_aesenc_si128(a, b);
}

static inline output finalize(state hash, uint32_t seed) {
    __m128i keys_1 = _mm_set_epi32(0x5A3BC47E, 0x89F216D5, 0xB09D2F61, 0xE37845F2);
    __m128i keys_2 = _mm_set_epi32(0xE7554D6F, 0x6EA75BBA, 0xDE3A74DB, 0x3D423129);
    __m128i keys_3 = _mm_set_epi32(0xC992E848, 0xA735B3F2, 0x790FC729, 0x444DF600);

    hash = _mm_aesenc_si128(hash, _mm_set1_epi32(seed));
    hash = _mm_aesenc_si128(hash, keys_1);
    hash = _mm_aesenc_si128(hash, keys_2);
    hash = _mm_aesenclast_si128(hash, keys_3);

    return hash;
}
#endif // __AVX2__ && __VAES__

#elif defined(__ARM_NEON) || defined(__ARM_NEON__) || defined(__aarch64__)
#include <arm_neon.h>

typedef int8x16_t state;
typedef int8x16_t output;

static inline state create_empty() {
    return vdupq_n_s8(0);
}

static inline state load_unaligned(const state* p) {
    return vld1q_s8((const int8_t*)p);
}

static inline int check_same_page(const state* ptr) {
    uintptr_t address = (uintptr_t)ptr;
    uintptr_t offset_within_page = address & 0xFFF;
    return offset_within_page <= (4096 - sizeof(state) - 1);
}

static inline state get_partial(const state* p, int len) {
    int8x16_t partial;
    if (check_same_page(p)) {
        // Unsafe (hence the check) but much faster
        static const int8_t indices[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
        int8x16_t mask = vcgtq_s8(vdupq_n_u8(len), vld1q_s8(indices));
        partial = vandq_s8(load_unaligned(p), mask);
    } else {
        // Safer but slower, using memcpy
        uint8_t buffer[sizeof(state)] = {0};
        memcpy(buffer, (const uint8_t*)p, len);
        partial = vld1q_s8((const int8_t*)buffer);
    }

    // Prevents padded zeroes to introduce bias
    return vaddq_u8(partial, vdupq_n_u8(len));
}

static inline uint8x16_t aes_encrypt(uint8x16_t data, uint8x16_t keys) {
    uint8x16_t encrypted = vaeseq_u8(data, vdupq_n_u8(0));
    uint8x16_t mixed = vaesmcq_u8(encrypted);
    return veorq_u8(mixed, keys);
}

static inline uint8x16_t aes_encrypt_last(uint8x16_t data, uint8x16_t keys) {
    uint8x16_t encrypted = vaeseq_u8(data, vdupq_n_u8(0));
    return veorq_u8(encrypted, keys);
}

static inline state compress(state a, state b) {
    static const uint32_t keys_1[4] = {0xFC3BC28E, 0x89C222E5, 0xB09D3E21, 0xF2784542};
    static const uint32_t keys_2[4] = {0x03FCE279, 0xCB6B2E9B, 0xB361DC58, 0x39136BD9};

    b = aes_encrypt(b, vld1q_u32(keys_1));
    b = aes_encrypt(b, vld1q_u32(keys_2));

    return aes_encrypt_last(a, b);
}

static inline state compress_fast(state a, state b) {
    return aes_encrypt(a, b);
}

static inline state finalize(state hash, uint32_t seed) {
    static const uint32_t keys_1[4] = {0x5A3BC47E, 0x89F216D5, 0xB09D2F61, 0xE37845F2};
    static const uint32_t keys_2[4] = {0xE7554D6F, 0x6EA75BBA, 0xDE3A74DB, 0x3D423129};
    static const uint32_t keys_3[4] = {0xC992E848, 0xA735B3F2, 0x790FC729, 0x444DF600};

    hash = aes_encrypt(hash, vdupq_n_u32(seed + 0xC992E848));
    hash = aes_encrypt(hash, vld1q_u32(keys_1));
    hash = aes_encrypt(hash, vld1q_u32(keys_2));
    hash = aes_encrypt_last(hash, vld1q_u32(keys_3));

    return hash;
}
#else
// Fallback ?
#endif

static inline output gxhash(const uint8_t* input, int len, uint32_t seed) {
    const int VECTOR_SIZE = sizeof(state);
    const state* p = (const state*)input;
    const state* v = p;
    const state* end_address;
    int remaining_blocks_count = len / VECTOR_SIZE;
    state hash_vector = create_empty();

    if (len <= VECTOR_SIZE) {
        hash_vector = get_partial(v, len);
        goto skip;
    }

    const int UNROLL_FACTOR = 8;
    if (len >= VECTOR_SIZE * UNROLL_FACTOR) {
        int unrollable_blocks_count = (len / (VECTOR_SIZE * UNROLL_FACTOR)) * UNROLL_FACTOR;
        end_address = v + unrollable_blocks_count;

        while (v < end_address) {
            state v0 = load_unaligned(v++);
            state v1 = load_unaligned(v++);
            state v2 = load_unaligned(v++);
            state v3 = load_unaligned(v++);
            state v4 = load_unaligned(v++);
            state v5 = load_unaligned(v++);
            state v6 = load_unaligned(v++);
            state v7 = load_unaligned(v++);

            v0 = compress_fast(v0, v1);
            v0 = compress_fast(v0, v2);
            v0 = compress_fast(v0, v3);
            v0 = compress_fast(v0, v4);
            v0 = compress_fast(v0, v5);
            v0 = compress_fast(v0, v6);
            v0 = compress_fast(v0, v7);

            hash_vector = compress(hash_vector, v0);
        }

        remaining_blocks_count -= unrollable_blocks_count;
    }

    end_address = v + remaining_blocks_count;

    while (v < end_address) {
        state v0 = load_unaligned(v++);
        hash_vector = compress(hash_vector, v0);
    }

    int remaining_bytes = len % VECTOR_SIZE;
    if (remaining_bytes > 0) {
        state partial_vector = get_partial(v, remaining_bytes);
        hash_vector = compress(hash_vector, partial_vector);
    }

skip:
    return finalize(hash_vector, seed);
}

uint32_t gxhash32(const uint8_t* input, int len, uint32_t seed) {
    output full_hash = gxhash(input, len, seed);
    return *(uint32_t*)&full_hash;
}

uint64_t gxhash64(const uint8_t* input, int len, uint32_t seed) {
    output full_hash = gxhash(input, len, seed);
    return *(uint64_t*)&full_hash;
}