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
    /* Total message bytes. This is a 64-bit byte counter, so the maximum
     * supported message length is 2^64-1 bytes (~1.8e19). SHA-512's padding
     * carries a 128-bit *bit* length; the bits above what a 64-bit byte count can
     * express are always 0 here — that ceiling is unreachable for any real input
     * (and vastly exceeds the tiny messages Ed25519 hashes). */
    uint64_t count;
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
