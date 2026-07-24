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

#include "kamran.k"   /* secure_zero */
#include <string.h>

typedef int64_t gf[16];

/* 121665 = (A-2)/4 for the Montgomery curve, in 16-bit-limb radix. */
static const gf gf_121665 = { 0xDB41, 1 };

/* Carry/reduce a field element into 16-bit limbs, folding bit 256 back via *38. */
static void car25519(gf o) {
    int i;
    int64_t c;
    for (i = 0; i < 16; i++) {
        o[i] += (int64_t)1 << 16;
        c = o[i] >> 16;
        o[(i + 1) * (i < 15)] += c - 1 + 37 * (c - 1) * (i == 15);
        o[i] -= c << 16;
    }
}

/* Constant-time conditional swap of p and q when b == 1. */
static void sel25519(gf p, gf q, int b) {
    int i;
    int64_t t;
    int64_t c = ~((int64_t)b - 1);
    for (i = 0; i < 16; i++) {
        t = c & (p[i] ^ q[i]);
        p[i] ^= t;
        q[i] ^= t;
    }
}

/* Serialize a field element to 32 little-endian bytes (fully reduced mod p). */
static void pack25519(uint8_t *o, const gf n) {
    int i, j, b;
    gf m, t;
    for (i = 0; i < 16; i++) {
        t[i] = n[i];
    }
    car25519(t);
    car25519(t);
    car25519(t);
    for (j = 0; j < 2; j++) {
        m[0] = t[0] - 0xffed;
        for (i = 1; i < 15; i++) {
            m[i] = t[i] - 0xffff - ((m[i - 1] >> 16) & 1);
            m[i - 1] &= 0xffff;
        }
        m[15] = t[15] - 0x7fff - ((m[14] >> 16) & 1);
        b = (int)((m[15] >> 16) & 1);
        m[14] &= 0xffff;
        sel25519(t, m, 1 - b);
    }
    for (i = 0; i < 16; i++) {
        o[2 * i]     = (uint8_t)(t[i] & 0xff);
        o[2 * i + 1] = (uint8_t)(t[i] >> 8);
    }
}

/* Parse 32 little-endian bytes into a field element (masking the high bit). */
static void unpack25519(gf o, const uint8_t *n) {
    int i;
    for (i = 0; i < 16; i++) {
        o[i] = n[2 * i] + ((int64_t)n[2 * i + 1] << 8);
    }
    o[15] &= 0x7fff;
}

static void fadd(gf o, const gf a, const gf b) {
    int i;
    for (i = 0; i < 16; i++) {
        o[i] = a[i] + b[i];
    }
}

static void fsub(gf o, const gf a, const gf b) {
    int i;
    for (i = 0; i < 16; i++) {
        o[i] = a[i] - b[i];
    }
}

static void fmul(gf o, const gf a, const gf b) {
    int64_t t[31];
    int i, j;
    for (i = 0; i < 31; i++) {
        t[i] = 0;
    }
    for (i = 0; i < 16; i++) {
        for (j = 0; j < 16; j++) {
            t[i + j] += a[i] * b[j];
        }
    }
    for (i = 0; i < 15; i++) {
        t[i] += 38 * t[i + 16];
    }
    for (i = 0; i < 16; i++) {
        o[i] = t[i];
    }
    car25519(o);
    car25519(o);
}

static void fsquare(gf o, const gf a) {
    fmul(o, a, a);
}

/* Field inverse via Fermat: o = i^(p-2) mod p, by a fixed square-and-multiply
 * chain (constant-time; the exceptions at exponent bits 2 and 4 are the two zero
 * bits of p-2). */
static void finverse(gf o, const gf i) {
    gf c;
    int a;
    for (a = 0; a < 16; a++) {
        c[a] = i[a];
    }
    for (a = 253; a >= 0; a--) {
        fsquare(c, c);
        if (a != 2 && a != 4) {
            fmul(c, c, i);
        }
    }
    for (a = 0; a < 16; a++) {
        o[a] = c[a];
    }
    secure_zero(c, sizeof(c));
}

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
        fadd(e, a, c);
        fsub(a, a, c);
        fadd(c, b, d);
        fsub(b, b, d);
        fsquare(d, e);
        fsquare(f, a);
        fmul(a, c, a);
        fmul(c, b, e);
        fadd(e, a, c);
        fsub(a, a, c);
        fsquare(b, a);
        fsub(c, d, f);
        fmul(a, c, gf_121665);
        fadd(a, a, d);
        fmul(c, c, a);
        fmul(a, d, f);
        fmul(d, b, x1);
        fsquare(b, e);
        sel25519(a, b, (int)r);
        sel25519(c, d, (int)r);
    }

    /* out = x2 / z2 = a * c^(p-2). */
    finverse(c, c);
    fmul(a, a, c);
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
