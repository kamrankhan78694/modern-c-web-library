/*
 * sha256.h — SHA-256 (FIPS 180-4) and HMAC-SHA256 (RFC 2104).
 *
 * Shared internal crypto primitive. These were previously file-static inside
 * middleware_auth.c (used by the JWT verifier); they are promoted here so the
 * upcoming pure-C TLS layer (key schedule / HKDF) can reuse the same, single
 * implementation rather than duplicating it. This module is target-neutral
 * (pure logic, no OS I/O) and is compiled into every build, WASM included.
 *
 * Not part of the public API (`include/kamran.k`); it lives under src/ and is
 * for internal use by the library's own subsystems.
 */
#ifndef WEBLIB_CRYPTO_SHA256_H
#define WEBLIB_CRYPTO_SHA256_H

#include <stddef.h>
#include <stdint.h>

#define SHA256_BLOCK_SIZE  64
#define SHA256_DIGEST_SIZE 32

typedef struct {
    uint32_t state[8];
    uint64_t count;
    uint8_t  buffer[SHA256_BLOCK_SIZE];
} sha256_ctx_t;

/* Streaming SHA-256. */
void sha256_init(sha256_ctx_t *ctx);
void sha256_update(sha256_ctx_t *ctx, const uint8_t *data, size_t len);
void sha256_final(sha256_ctx_t *ctx, uint8_t *digest);   /* digest: SHA256_DIGEST_SIZE bytes */

/* One-shot SHA-256 of `data` into `digest` (SHA256_DIGEST_SIZE bytes). */
void sha256(const uint8_t *data, size_t len, uint8_t *digest);

/*
 * HMAC-SHA256 (RFC 2104). Writes SHA256_DIGEST_SIZE (32) bytes to `output`.
 * Internal key-derived scratch is wiped with secure_zero() before returning.
 */
void hmac_sha256(const uint8_t *key, size_t key_len,
                 const uint8_t *data, size_t data_len,
                 uint8_t *output);

#endif /* WEBLIB_CRYPTO_SHA256_H */
