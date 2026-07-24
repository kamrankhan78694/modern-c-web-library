/*
 * chacha20.c — ChaCha20 stream cipher (RFC 8439). EXPERIMENTAL / UNAUDITED.
 *
 * From-scratch implementation for the experimental pure-C TLS layer. Compiled
 * only under -DWEBLIB_ENABLE_TLS=ON. Verified against RFC 8439 §2.3.2 / §2.4.2
 * known-answer vectors in tests/test_tls_crypto.c.
 */
#include "chacha20.h"

#ifdef WEBLIB_TLS

#include "kamran.k"   /* secure_zero */
#include <string.h>

static uint32_t load32_le(const uint8_t *p) {
    return (uint32_t)p[0] |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static void store32_le(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v);
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

#define ROTL32(x, n) (((x) << (n)) | ((x) >> (32 - (n))))

/* ChaCha quarter round (RFC 8439 §2.1) operating on four state words. */
#define QUARTERROUND(s, a, b, c, d)              \
    do {                                         \
        (s)[a] += (s)[b]; (s)[d] ^= (s)[a]; (s)[d] = ROTL32((s)[d], 16); \
        (s)[c] += (s)[d]; (s)[b] ^= (s)[c]; (s)[b] = ROTL32((s)[b], 12); \
        (s)[a] += (s)[b]; (s)[d] ^= (s)[a]; (s)[d] = ROTL32((s)[d],  8); \
        (s)[c] += (s)[d]; (s)[b] ^= (s)[c]; (s)[b] = ROTL32((s)[b],  7); \
    } while (0)

void chacha20_block(const uint8_t key[32], uint32_t counter,
                    const uint8_t nonce[12], uint8_t out[64]) {
    uint32_t state[16];
    uint32_t x[16];
    int i;

    /* Constants: "expand 32-byte k" (RFC 8439 §2.3). */
    state[0] = 0x61707865;
    state[1] = 0x3320646e;
    state[2] = 0x79622d32;
    state[3] = 0x6b206574;
    for (i = 0; i < 8; i++) {
        state[4 + i] = load32_le(key + 4 * i);
    }
    state[12] = counter;
    state[13] = load32_le(nonce + 0);
    state[14] = load32_le(nonce + 4);
    state[15] = load32_le(nonce + 8);

    memcpy(x, state, sizeof(x));

    /* 20 rounds = 10 iterations of (4 column + 4 diagonal) quarter rounds. */
    for (i = 0; i < 10; i++) {
        QUARTERROUND(x, 0, 4,  8, 12);
        QUARTERROUND(x, 1, 5,  9, 13);
        QUARTERROUND(x, 2, 6, 10, 14);
        QUARTERROUND(x, 3, 7, 11, 15);
        QUARTERROUND(x, 0, 5, 10, 15);
        QUARTERROUND(x, 1, 6, 11, 12);
        QUARTERROUND(x, 2, 7,  8, 13);
        QUARTERROUND(x, 3, 4,  9, 14);
    }

    /* Add the original state and serialize little-endian. */
    for (i = 0; i < 16; i++) {
        store32_le(out + 4 * i, x[i] + state[i]);
    }

    /* Wipe key-derived working state from the stack (state holds the key words,
     * x the mixed state), matching the hygiene of the SHA-256/HMAC module. */
    secure_zero(state, sizeof(state));
    secure_zero(x, sizeof(x));
}

void chacha20_encrypt(const uint8_t key[32], uint32_t counter,
                      const uint8_t nonce[12],
                      const uint8_t *in, size_t len, uint8_t *out) {
    uint8_t ks[64];

    while (len > 0) {
        size_t n = len < 64 ? len : 64;
        size_t i;

        chacha20_block(key, counter, nonce, ks);
        for (i = 0; i < n; i++) {
            out[i] = in[i] ^ ks[i];
        }
        in += n;
        out += n;
        len -= n;
        counter++;   /* wraps at 2^32 blocks (256 GiB) per (key, nonce) */
    }

    secure_zero(ks, sizeof(ks));
}

#endif /* WEBLIB_TLS */
