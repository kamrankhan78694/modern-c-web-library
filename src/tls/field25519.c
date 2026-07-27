/*
 * field25519.c — GF(2^255-19) field arithmetic for Curve25519. EXPERIMENTAL / UNAUDITED.
 *
 * Compiled only under -DWEBLIB_ENABLE_TLS=ON. Promoted from the file-static field
 * ops that lived in x25519.c so X25519 and Ed25519 share one implementation. The
 * arithmetic is unchanged (behaviour is bit-identical, verified by the X25519
 * known-answer tests) apart from one correctness fix carried over for both curves:
 * car25519 now folds its carry with a multiply instead of a left shift, which for
 * a negative carry was undefined behaviour (see the note there). Exercised by the
 * X25519 / Ed25519 known-answer tests in tests/test_tls_crypto.c.
 */
#include "field25519.h"

#ifdef WEBLIB_TLS

#include "kamran.k"   /* secure_zero */

void car25519(gf o) {
    int i;
    int64_t c;
    for (i = 0; i < 16; i++) {
        o[i] += (int64_t)1 << 16;
        c = o[i] >> 16;
        o[(i + 1) * (i < 15)] += c - 1 + 37 * (c - 1) * (i == 15);
        /* c can be negative, so fold the carry back with multiplication rather
         * than `c << 16`: left-shifting a negative value is undefined behavior
         * (flagged by UBSan). Scaling by the limb radix 2^16 is defined and
         * arithmetically identical for every in-range c, and cannot overflow
         * int64 given TweetNaCl's limb bounds (|c| < 2^40, so |c * 2^16| stays
         * well below INT64_MAX). The parentheses matter: `*` binds tighter than
         * `<<`, so `c * ((int64_t)1 << 16)` scales c, whereas dropping them would
         * re-form the very `c << 16` shift this avoids. */
        o[i] -= c * ((int64_t)1 << 16);
    }
}

void sel25519(gf p, gf q, int b) {
    int i;
    int64_t t;
    int64_t c = ~((int64_t)b - 1);
    for (i = 0; i < 16; i++) {
        t = c & (p[i] ^ q[i]);
        p[i] ^= t;
        q[i] ^= t;
    }
}

void pack25519(uint8_t *o, const gf n) {
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
    /* `t`/`m` hold the fully-reduced field element being packed — for X25519 that is
     * the ECDHE shared secret in limb form. Wipe them so the secret does not survive
     * in this leaf's stack frame after the caller (which wipes its own copy and `o`)
     * returns. Called ~once per operation, so the cost is negligible. */
    secure_zero(m, sizeof(m));
    secure_zero(t, sizeof(t));
}

void unpack25519(gf o, const uint8_t *n) {
    int i;
    for (i = 0; i < 16; i++) {
        o[i] = n[2 * i] + ((int64_t)n[2 * i + 1] << 8);
    }
    o[15] &= 0x7fff;
}

void add25519(gf o, const gf a, const gf b) {
    int i;
    for (i = 0; i < 16; i++) {
        o[i] = a[i] + b[i];
    }
}

void sub25519(gf o, const gf a, const gf b) {
    int i;
    for (i = 0; i < 16; i++) {
        o[i] = a[i] - b[i];
    }
}

void mul25519(gf o, const gf a, const gf b) {
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
    /* Every field and curve-point operation bottoms out here, so `t` holds products
     * of secret operands (the X25519 scalar/coordinate, the Ed25519 nonce). Wipe the
     * accumulator so no secret-derived limb product survives in this leaf's frame
     * once the operation returns. The output `o` is the caller's and is wiped by the
     * caller when it is secret. The extra zeroing is a small fixed cost dwarfed by
     * the 16x16 multiply this function already performs. */
    secure_zero(t, sizeof(t));
}

void sq25519(gf o, const gf a) {
    mul25519(o, a, a);
}

void inv25519(gf o, const gf i) {
    gf c;
    int a;
    for (a = 0; a < 16; a++) {
        c[a] = i[a];
    }
    for (a = 253; a >= 0; a--) {
        sq25519(c, c);
        if (a != 2 && a != 4) {
            mul25519(c, c, i);
        }
    }
    for (a = 0; a < 16; a++) {
        o[a] = c[a];
    }
    secure_zero(c, sizeof(c));
}

#endif /* WEBLIB_TLS */
