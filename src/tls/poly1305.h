/*
 * poly1305.h — Poly1305 one-time authenticator (RFC 8439 §2.5). EXPERIMENTAL / UNAUDITED.
 *
 * Part of the experimental pure-C TLS layer; compiled only under
 * -DWEBLIB_ENABLE_TLS=ON (native-only). Pairs with ChaCha20 to form the
 * TLS_CHACHA20_POLY1305_SHA256 AEAD. Verified against the RFC 8439 §2.5.2
 * known-answer vector (and several edge cases) — see tests/test_tls_crypto.c.
 *
 * Implementation uses 26-bit limbs with 64-bit products (the well-known
 * constant-time approach): the field arithmetic mod 2^130-5 has no
 * secret-dependent branches, indexing, or table lookups.
 *
 * SECURITY: the 32-byte key is a ONE-TIME key — a given key must authenticate at
 * most one message. In the AEAD it is derived per-record from ChaCha20.
 */
#ifndef WEBLIB_TLS_POLY1305_H
#define WEBLIB_TLS_POLY1305_H

#ifdef WEBLIB_TLS

#include <stddef.h>
#include <stdint.h>

/*
 * Compute the 16-byte Poly1305 tag of `msg` (`len` bytes) under the 32-byte
 * one-time `key` (r || s), writing it to `tag`. `len` may be zero.
 */
void poly1305_mac(const uint8_t key[32], const uint8_t *msg, size_t len,
                  uint8_t tag[16]);

#endif /* WEBLIB_TLS */
#endif /* WEBLIB_TLS_POLY1305_H */
