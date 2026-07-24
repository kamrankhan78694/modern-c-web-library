/*
 * field25519.h — GF(2^255-19) field arithmetic for Curve25519. EXPERIMENTAL / UNAUDITED.
 *
 * Shared internal module for the experimental pure-C TLS layer; compiled only
 * under -DWEBLIB_ENABLE_TLS=ON. Both X25519 (RFC 7748) and Ed25519 (RFC 8032)
 * operate over the same prime field, so the field element type and its constant-
 * time operations live here rather than being duplicated per curve. The
 * implementation is the compact, widely-reviewed TweetNaCl formulation: a field
 * element is 16 signed 16-bit-radix limbs in int64 (so 16x16 schoolbook products
 * stay within int64), with the 2^256 overflow folded back via 38 = 2*19. All
 * operations are constant-time (no secret-dependent branch, index, or table).
 *
 * Correctness is exercised transitively by the X25519 and Ed25519 known-answer
 * tests in tests/test_tls_crypto.c.
 */
#ifndef WEBLIB_TLS_FIELD25519_H
#define WEBLIB_TLS_FIELD25519_H

#ifdef WEBLIB_TLS

#include <stdint.h>

/* A field element: 16 signed 16-bit-radix limbs. */
typedef int64_t gf[16];

/* Carry/reduce `o` into canonical 16-bit limbs (folding bit 256 via *38). */
void car25519(gf o);

/* Constant-time conditional swap of `p` and `q` when b == 1. */
void sel25519(gf p, gf q, int b);

/* Serialize `n` to 32 little-endian bytes, fully reduced mod p. */
void pack25519(uint8_t *o, const gf n);

/* Parse 32 little-endian bytes into `o` (the top bit is masked). */
void unpack25519(gf o, const uint8_t *n);

void add25519(gf o, const gf a, const gf b);   /* o = a + b */
void sub25519(gf o, const gf a, const gf b);   /* o = a - b */
void mul25519(gf o, const gf a, const gf b);   /* o = a * b mod p */
void sq25519(gf o, const gf a);                /* o = a^2 mod p */

/* o = i^(p-2) mod p (multiplicative inverse via Fermat; constant-time). */
void inv25519(gf o, const gf i);

#endif /* WEBLIB_TLS */
#endif /* WEBLIB_TLS_FIELD25519_H */
