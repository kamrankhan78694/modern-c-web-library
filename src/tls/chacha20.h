/*
 * chacha20.h — ChaCha20 stream cipher (RFC 8439). EXPERIMENTAL / UNAUDITED.
 *
 * Part of the experimental pure-C TLS layer; compiled only under
 * -DWEBLIB_ENABLE_TLS=ON (native-only). One of the from-scratch primitives for
 * the TLS_CHACHA20_POLY1305_SHA256 cipher suite. Verified against the RFC 8439
 * §2.3.2 (block) and §2.4.2 (encryption) known-answer vectors — see
 * tests/test_tls_crypto.c.
 *
 * ChaCha20 is constant-time by construction (only 32-bit add / xor / rotate on
 * fixed-size state; no secret-dependent branches or table lookups).
 */
#ifndef WEBLIB_TLS_CHACHA20_H
#define WEBLIB_TLS_CHACHA20_H

#ifdef WEBLIB_TLS

#include <stddef.h>
#include <stdint.h>

/*
 * Generate one 64-byte ChaCha20 keystream block for the given 256-bit key,
 * 32-bit block counter, and 96-bit nonce (RFC 8439 §2.3). `out` receives 64 bytes.
 */
void chacha20_block(const uint8_t key[32], uint32_t counter,
                    const uint8_t nonce[12], uint8_t out[64]);

/*
 * Encrypt (or decrypt — the operation is its own inverse) `len` bytes from `in`
 * to `out` by XOR with the ChaCha20 keystream, starting at block `counter`
 * (RFC 8439 §2.4). `in` and `out` may alias. The internal keystream buffer is
 * wiped before return. The 32-bit counter limits one (key, nonce) to 256 GiB.
 */
void chacha20_encrypt(const uint8_t key[32], uint32_t counter,
                      const uint8_t nonce[12],
                      const uint8_t *in, size_t len, uint8_t *out);

#endif /* WEBLIB_TLS */
#endif /* WEBLIB_TLS_CHACHA20_H */
