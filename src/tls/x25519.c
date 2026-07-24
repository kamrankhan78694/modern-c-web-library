/*
 * x25519.c — X25519 ECDH (RFC 7748). EXPERIMENTAL / UNAUDITED.
 *
 * Constant-time Montgomery ladder over GF(2^255-19). The field element is 16
 * signed 16-bit-radix limbs held in int64 (so 16x16 schoolbook products stay well
 * within int64); reduction folds the 2^256 overflow with the constant 38 = 2*19.
 * This is the compact, widely-reviewed formulation from TweetNaCl (public domain),
 * adapted to this codebase — chosen over a from-first-principles field
 * implementation to minimise the risk inherent in hand-rolled bignum crypto. It is
 * fully constant-time: the ladder's conditional swaps are arithmetic (no branch on
 * secret bits), and there are no secret-indexed memory accesses.
 *
 * Compiled only under -DWEBLIB_ENABLE_TLS=ON. Verified against RFC 7748 §5.2/§6.1
 * and the iterated test in tests/test_tls_crypto.c.
 */
#include "x25519.h"

#ifdef WEBLIB_TLS

#include "kamran.k"        /* secure_zero */
#include "field25519.h"    /* gf, (un)pack25519, sel25519, add/sub/mul/sq/inv25519 */
#include <string.h>

/* 121665 = (A-2)/4 for the Montgomery curve, in 16-bit-limb radix. The field
 * element type `gf` and its operations now live in field25519.{c,h}. */
static const gf gf_121665 = { 0xDB41, 1 };

void x25519(uint8_t out[32], const uint8_t scalar[32], const uint8_t u[32]) {
    uint8_t z[32];
    gf x1, a, b, c, d, e, f;
    int i;

    /* Clamp the scalar (RFC 7748 §5). */
    for (i = 0; i < 31; i++) {
        z[i] = scalar[i];
    }
    z[31] = (scalar[31] & 127) | 64;
    z[0] &= 248;

    unpack25519(x1, u);
    for (i = 0; i < 16; i++) {
        b[i] = x1[i];
        d[i] = a[i] = c[i] = 0;
    }
    a[0] = d[0] = 1;

    /* Montgomery ladder over the 255 scalar bits, high to low. */
    for (i = 254; i >= 0; i--) {
        int64_t r = (z[i >> 3] >> (i & 7)) & 1;
        sel25519(a, b, (int)r);
        sel25519(c, d, (int)r);
        add25519(e, a, c);
        sub25519(a, a, c);
        add25519(c, b, d);
        sub25519(b, b, d);
        sq25519(d, e);
        sq25519(f, a);
        mul25519(a, c, a);
        mul25519(c, b, e);
        add25519(e, a, c);
        sub25519(a, a, c);
        sq25519(b, a);
        sub25519(c, d, f);
        mul25519(a, c, gf_121665);
        add25519(a, a, d);
        mul25519(c, c, a);
        mul25519(a, d, f);
        mul25519(d, b, x1);
        sq25519(b, e);
        sel25519(a, b, (int)r);
        sel25519(c, d, (int)r);
    }

    /* out = x2 / z2 = a * c^(p-2). */
    inv25519(c, c);
    mul25519(a, a, c);
    pack25519(out, a);

    /* Wipe the clamped scalar and all working field elements. */
    secure_zero(z, sizeof(z));
    secure_zero(x1, sizeof(x1));
    secure_zero(a, sizeof(a));
    secure_zero(b, sizeof(b));
    secure_zero(c, sizeof(c));
    secure_zero(d, sizeof(d));
    secure_zero(e, sizeof(e));
    secure_zero(f, sizeof(f));
}

void x25519_base(uint8_t out[32], const uint8_t scalar[32]) {
    uint8_t basepoint[32];
    memset(basepoint, 0, sizeof(basepoint));
    basepoint[0] = 9;
    x25519(out, scalar, basepoint);
}

#endif /* WEBLIB_TLS */
