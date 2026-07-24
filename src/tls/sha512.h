/*
 * sha512.h — SHA-512 (FIPS 180-4). EXPERIMENTAL / UNAUDITED.
 *
 * Part of the experimental pure-C TLS layer; compiled only under
 * -DWEBLIB_ENABLE_TLS=ON (native-only). Implemented here because Ed25519
 * (RFC 8032) hashes with SHA-512; kept TLS-gated since no non-TLS subsystem needs
 * it (unlike the shared SHA-256 in src/crypto). Verified against the FIPS 180-4 /
 * NIST test vectors — see tests/test_tls_crypto.c.
 */
#ifndef WEBLIB_TLS_SHA512_H
#define WEBLIB_TLS_SHA512_H

#ifdef WEBLIB_TLS

#include <stddef.h>
#include <stdint.h>

#define SHA512_BLOCK_SIZE  128
#define SHA512_DIGEST_SIZE 64

typedef struct {
    uint64_t state[8];
    uint64_t count;      /* total message bytes (bit length is count*8) */
    uint8_t  buffer[SHA512_BLOCK_SIZE];
} sha512_ctx_t;

/* Streaming SHA-512. */
void sha512_init(sha512_ctx_t *ctx);
void sha512_update(sha512_ctx_t *ctx, const uint8_t *data, size_t len);
void sha512_final(sha512_ctx_t *ctx, uint8_t *digest);   /* SHA512_DIGEST_SIZE bytes */

/* One-shot SHA-512 of `data` into `digest` (SHA512_DIGEST_SIZE bytes). */
void sha512(const uint8_t *data, size_t len, uint8_t *digest);

#endif /* WEBLIB_TLS */
#endif /* WEBLIB_TLS_SHA512_H */
