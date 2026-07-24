/*
 * ed25519.h — Ed25519 signatures (RFC 8032). EXPERIMENTAL / UNAUDITED.
 *
 * Part of the experimental pure-C TLS layer; compiled only under
 * -DWEBLIB_ENABLE_TLS=ON (native-only). Detached sign/verify over the
 * twisted-Edwards curve, built on the shared GF(2^255-19) field arithmetic
 * (field25519) and SHA-512 (sha512). This is the last primitive of the
 * TLS_CHACHA20_POLY1305_SHA256 + X25519 + Ed25519 set.
 *
 * The group/point layer is the compact, widely-reviewed TweetNaCl (public
 * domain) formulation, chosen over a from-first-principles implementation to
 * minimise the risk inherent in hand-rolled elliptic-curve code, and adapted to
 * a detached-signature API that streams the message through SHA-512 (no
 * signature||message concatenation buffer). Verified against the RFC 8032 §7.1
 * known-answer tests in tests/test_tls_crypto.c.
 */
#ifndef WEBLIB_TLS_ED25519_H
#define WEBLIB_TLS_ED25519_H

#ifdef WEBLIB_TLS

#include <stddef.h>
#include <stdint.h>

#define ED25519_SEED_SIZE       32
#define ED25519_PUBLIC_KEY_SIZE 32
#define ED25519_SIGNATURE_SIZE  64

/* Derive the 32-byte public key from a 32-byte seed (the private key). */
void ed25519_public_key(uint8_t pk[32], const uint8_t seed[32]);

/*
 * Write a 64-byte detached signature of `m` (length `n`) under `seed`. `pk` must
 * be the public key for `seed` — i.e. the value ed25519_public_key() writes for
 * that seed — passed in so the caller can cache it rather than re-deriving it on
 * every signature. `m` may be NULL only when `n` is 0; a NULL `m` with `n > 0` is
 * an invalid call and yields a deterministic all-zero (unverifiable) signature
 * rather than a crash.
 */
void ed25519_sign(uint8_t sig[64], const uint8_t *m, size_t n,
                  const uint8_t seed[32], const uint8_t pk[32]);

/*
 * Verify a 64-byte detached signature of `m` (length `n`) against public key
 * `pk`. Returns 1 if the signature is valid, 0 otherwise (including a malformed
 * public key). `m` may be NULL only when `n` is 0; a NULL `m` with `n > 0` is an
 * invalid call and returns 0.
 */
int ed25519_verify(const uint8_t sig[64], const uint8_t *m, size_t n,
                   const uint8_t pk[32]);

#endif /* WEBLIB_TLS */
#endif /* WEBLIB_TLS_ED25519_H */
