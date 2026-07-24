/*
 * x25519.h — X25519 Elliptic-Curve Diffie-Hellman (RFC 7748). EXPERIMENTAL / UNAUDITED.
 *
 * Part of the experimental pure-C TLS layer; compiled only under
 * -DWEBLIB_ENABLE_TLS=ON (native-only). The key-exchange group for TLS 1.3 with
 * this cipher suite. Verified against RFC 7748 §5.2 / §6.1 and the iterated test
 * — see tests/test_tls_crypto.c.
 *
 * The scalar multiplication is a constant-time Montgomery ladder over field
 * arithmetic mod 2^255-19 (16 x 16-bit limbs, int64 products — portable, no
 * __int128). This is the compact, widely-reviewed formulation from TweetNaCl;
 * there are no secret-dependent branches or memory accesses.
 */
#ifndef WEBLIB_TLS_X25519_H
#define WEBLIB_TLS_X25519_H

#ifdef WEBLIB_TLS

#include <stdint.h>

/*
 * X25519 scalar multiplication (RFC 7748 §5): out = scalar * u-coordinate. The
 * scalar is clamped and the u-coordinate's high bit masked internally, per the RFC.
 * `out` may alias neither `scalar` nor `u`. All three buffers are 32 bytes.
 */
void x25519(uint8_t out[32], const uint8_t scalar[32], const uint8_t u[32]);

/*
 * X25519 with the base point (u = 9): derive the public key for a private
 * `scalar`. `out` and `scalar` are 32 bytes.
 */
void x25519_base(uint8_t out[32], const uint8_t scalar[32]);

#endif /* WEBLIB_TLS */
#endif /* WEBLIB_TLS_X25519_H */
