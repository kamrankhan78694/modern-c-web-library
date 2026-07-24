/*
 * sha256.c — SHA-256 (FIPS 180-4) and HMAC-SHA256 (RFC 2104).
 *
 * Promoted verbatim from the file-static implementation that lived in
 * middleware_auth.c so the JWT verifier and the pure-C TLS layer share one
 * implementation. Behaviour is byte-for-byte identical to the original; the
 * only change is linkage (the public entry points are no longer static) and
 * that the round macros / K constants / transform are now file-local here.
 */
#include "sha256.h"
#include "kamran.k"   /* secure_zero */
#include <string.h>

/* ===== SHA-256 Implementation (FIPS 180-4) ===== */

/* SHA-256 constants (first 32 bits of the fractional parts of cube roots of first 64 primes) */
static const uint32_t sha256_k[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

/* Rotate right macro */
#define ROTR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))

/* SHA-256 functions */
#define CH(x, y, z)  (((x) & (y)) ^ (~(x) & (z)))
#define MAJ(x, y, z) (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))
#define EP0(x) (ROTR(x, 2) ^ ROTR(x, 13) ^ ROTR(x, 22))
#define EP1(x) (ROTR(x, 6) ^ ROTR(x, 11) ^ ROTR(x, 25))
#define SIG0(x) (ROTR(x, 7) ^ ROTR(x, 18) ^ ((x) >> 3))
#define SIG1(x) (ROTR(x, 17) ^ ROTR(x, 19) ^ ((x) >> 10))

/* sha256_transform - Process one 512-bit block */
static void sha256_transform(sha256_ctx_t *ctx, const uint8_t *data)
{
    uint32_t a, b, c, d, e, f, g, h, t1, t2;
    uint32_t w[64];
    int i;

    /* Prepare message schedule */
    for (i = 0; i < 16; i++) {
        w[i] = ((uint32_t)data[i * 4] << 24) |
               ((uint32_t)data[i * 4 + 1] << 16) |
               ((uint32_t)data[i * 4 + 2] << 8) |
               ((uint32_t)data[i * 4 + 3]);
    }

    for (i = 16; i < 64; i++) {
        w[i] = SIG1(w[i - 2]) + w[i - 7] + SIG0(w[i - 15]) + w[i - 16];
    }

    /* Initialize working variables */
    a = ctx->state[0];
    b = ctx->state[1];
    c = ctx->state[2];
    d = ctx->state[3];
    e = ctx->state[4];
    f = ctx->state[5];
    g = ctx->state[6];
    h = ctx->state[7];

    /* Main loop */
    for (i = 0; i < 64; i++) {
        t1 = h + EP1(e) + CH(e, f, g) + sha256_k[i] + w[i];
        t2 = EP0(a) + MAJ(a, b, c);
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }

    /* Add compressed chunk to current hash value */
    ctx->state[0] += a;
    ctx->state[1] += b;
    ctx->state[2] += c;
    ctx->state[3] += d;
    ctx->state[4] += e;
    ctx->state[5] += f;
    ctx->state[6] += g;
    ctx->state[7] += h;
}

/* sha256_init - Initialize SHA-256 context */
void sha256_init(sha256_ctx_t *ctx)
{
    /* Initial hash values (first 32 bits of fractional parts of square roots of first 8 primes) */
    ctx->state[0] = 0x6a09e667;
    ctx->state[1] = 0xbb67ae85;
    ctx->state[2] = 0x3c6ef372;
    ctx->state[3] = 0xa54ff53a;
    ctx->state[4] = 0x510e527f;
    ctx->state[5] = 0x9b05688c;
    ctx->state[6] = 0x1f83d9ab;
    ctx->state[7] = 0x5be0cd19;
    ctx->count = 0;
}

/* sha256_update - Add data to SHA-256 hash */
void sha256_update(sha256_ctx_t *ctx, const uint8_t *data, size_t len)
{
    size_t index = (size_t)(ctx->count % SHA256_BLOCK_SIZE);

    ctx->count += len;

    /* Process any buffered data first */
    if (index > 0) {
        size_t space = SHA256_BLOCK_SIZE - index;
        if (len < space) {
            memcpy(&ctx->buffer[index], data, len);
            return;
        }
        memcpy(&ctx->buffer[index], data, space);
        sha256_transform(ctx, ctx->buffer);
        data += space;
        len -= space;
    }

    /* Process complete blocks */
    while (len >= SHA256_BLOCK_SIZE) {
        sha256_transform(ctx, data);
        data += SHA256_BLOCK_SIZE;
        len -= SHA256_BLOCK_SIZE;
    }

    /* Buffer remaining data */
    if (len > 0) {
        memcpy(ctx->buffer, data, len);
    }
}

/* sha256_final - Finalize SHA-256 hash and produce digest */
void sha256_final(sha256_ctx_t *ctx, uint8_t *digest)
{
    size_t i;
    size_t index = (size_t)(ctx->count % SHA256_BLOCK_SIZE);
    uint64_t bits = ctx->count * 8;

    /* Pad with 0x80 followed by zeros */
    ctx->buffer[index++] = 0x80;

    if (index > 56) {
        /* Not enough space for length, pad and process block */
        while (index < SHA256_BLOCK_SIZE) {
            ctx->buffer[index++] = 0x00;
        }
        sha256_transform(ctx, ctx->buffer);
        index = 0;
    }

    /* Pad with zeros */
    while (index < 56) {
        ctx->buffer[index++] = 0x00;
    }

    /* Append length in bits as big-endian 64-bit integer */
    ctx->buffer[56] = (uint8_t)(bits >> 56);
    ctx->buffer[57] = (uint8_t)(bits >> 48);
    ctx->buffer[58] = (uint8_t)(bits >> 40);
    ctx->buffer[59] = (uint8_t)(bits >> 32);
    ctx->buffer[60] = (uint8_t)(bits >> 24);
    ctx->buffer[61] = (uint8_t)(bits >> 16);
    ctx->buffer[62] = (uint8_t)(bits >> 8);
    ctx->buffer[63] = (uint8_t)(bits);

    sha256_transform(ctx, ctx->buffer);

    /* Produce final hash value (big-endian) */
    for (i = 0; i < 8; i++) {
        digest[i * 4] = (uint8_t)(ctx->state[i] >> 24);
        digest[i * 4 + 1] = (uint8_t)(ctx->state[i] >> 16);
        digest[i * 4 + 2] = (uint8_t)(ctx->state[i] >> 8);
        digest[i * 4 + 3] = (uint8_t)(ctx->state[i]);
    }
}

/* sha256 - Compute SHA-256 hash of data */
void sha256(const uint8_t *data, size_t len, uint8_t *digest)
{
    sha256_ctx_t ctx;
    sha256_init(&ctx);
    sha256_update(&ctx, data, len);
    sha256_final(&ctx, digest);
}

/* ===== HMAC-SHA256 Implementation (RFC 2104) ===== */

void hmac_sha256(const uint8_t *key, size_t key_len,
                 const uint8_t *data, size_t data_len,
                 uint8_t *output)
{
    sha256_ctx_t ctx;
    uint8_t k_pad[SHA256_BLOCK_SIZE];
    uint8_t tk[SHA256_DIGEST_SIZE];
    size_t i;

    /* If key is longer than block size, hash it first */
    if (key_len > SHA256_BLOCK_SIZE) {
        sha256(key, key_len, tk);
        key = tk;
        key_len = SHA256_DIGEST_SIZE;
    }

    /* Prepare inner padding (key XOR 0x36) */
    memset(k_pad, 0x36, SHA256_BLOCK_SIZE);
    for (i = 0; i < key_len; i++) {
        k_pad[i] ^= key[i];
    }

    /* Compute inner hash: H(K XOR ipad, text) */
    sha256_init(&ctx);
    sha256_update(&ctx, k_pad, SHA256_BLOCK_SIZE);
    sha256_update(&ctx, data, data_len);
    sha256_final(&ctx, output);

    /* Prepare outer padding (key XOR 0x5c) */
    memset(k_pad, 0x5c, SHA256_BLOCK_SIZE);
    for (i = 0; i < key_len; i++) {
        k_pad[i] ^= key[i];
    }

    /* Compute outer hash: H(K XOR opad, inner_hash) */
    sha256_init(&ctx);
    sha256_update(&ctx, k_pad, SHA256_BLOCK_SIZE);
    sha256_update(&ctx, output, SHA256_DIGEST_SIZE);
    sha256_final(&ctx, output);

    /* Wipe sensitive key material from stack */
    secure_zero(k_pad, sizeof(k_pad));
    secure_zero(tk, sizeof(tk));
    secure_zero(&ctx, sizeof(ctx));
}
