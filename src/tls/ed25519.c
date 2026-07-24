/*
 * ed25519.c — Ed25519 signatures (RFC 8032). EXPERIMENTAL / UNAUDITED.
 *
 * Compiled only under -DWEBLIB_ENABLE_TLS=ON. TweetNaCl-based (public domain)
 * twisted-Edwards group arithmetic over the shared GF(2^255-19) field
 * (field25519), hashing with SHA-512 (sha512). Restructured from TweetNaCl's
 * combined signature||message convention into a detached API that streams the
 * message through SHA-512, so no O(message) concatenation buffer is allocated.
 *
 * Curve constants (D, D2, X, Y, sqrt(-1)) and the group order L were generated
 * from first principles (d = -121665/121666 mod p, etc.) by the reference in the
 * repo's cross-check tooling, not transcribed. Correctness is pinned by the
 * RFC 8032 §7.1 known-answer tests in tests/test_tls_crypto.c.
 */
#include "ed25519.h"

#ifdef WEBLIB_TLS

#include "field25519.h"   /* gf + add/sub/mul/sq/inv/pack/unpack/sel25519 */
#include "sha512.h"
#include "kamran.k"       /* secure_zero, secure_compare */

#include <string.h>       /* memset (deterministic-failure output only) */

/* ---- curve constants (16x 16-bit little-endian limbs) ------------------ */

static const gf
    gf0 = { 0 },
    gf1 = { 1 },
    /* D = -121665/121666 mod p (twisted-Edwards d). */
    D  = { 0x78a3, 0x1359, 0x4dca, 0x75eb, 0xd8ab, 0x4141, 0x0a4d, 0x0070,
           0xe898, 0x7779, 0x4079, 0x8cc7, 0xfe73, 0x2b6f, 0x6cee, 0x5203 },
    /* D2 = 2*D mod p. */
    D2 = { 0xf159, 0x26b2, 0x9b94, 0xebd6, 0xb156, 0x8283, 0x149a, 0x00e0,
           0xd130, 0xeef3, 0x80f2, 0x198e, 0xfce7, 0x56df, 0xd9dc, 0x2406 },
    /* Base point B = (X, Y); Y = 4/5 mod p, X recovered from the curve. */
    X  = { 0xd51a, 0x8f25, 0x2d60, 0xc956, 0xa7b2, 0x9525, 0xc760, 0x692c,
           0xdc5c, 0xfdd6, 0xe231, 0xc0a4, 0x53fe, 0xcd6e, 0x36d3, 0x2169 },
    Y  = { 0x6658, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666,
           0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666 },
    /* sqrt(-1) mod p = 2^((p-1)/4). */
    SQRTM1 = { 0xa0b0, 0x4a0e, 0x1b27, 0xc4ee, 0xe478, 0xad2f, 0x1806, 0x2f43,
               0xd7a7, 0x3dfb, 0x0099, 0x2b4d, 0xdf0b, 0x4fc1, 0x2480, 0x2b83 };

/* Group order L = 2^252 + 27742317777372353535851937790883648493,
 * as 32 little-endian bytes. */
static const int64_t L[32] = {
    0xed, 0xd3, 0xf5, 0x5c, 0x1a, 0x63, 0x12, 0x58,
    0xd6, 0x9c, 0xf7, 0xa2, 0xde, 0xf9, 0xde, 0x14,
    0,    0,    0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    0x10
};

/* ---- small field helpers only Ed25519 needs --------------------------- */

static void set25519(gf r, const gf a) {
    int i;
    for (i = 0; i < 16; i++) {
        r[i] = a[i];
    }
}

/* o = i^(2^252 - 3) mod p — the exponent for the curve square root in decode. */
static void pow2523(gf o, const gf i) {
    gf c;
    int a;
    for (a = 0; a < 16; a++) {
        c[a] = i[a];
    }
    for (a = 250; a >= 0; a--) {
        sq25519(c, c);
        if (a != 1) {
            mul25519(c, c, i);
        }
    }
    for (a = 0; a < 16; a++) {
        o[a] = c[a];
    }
}

/* Low bit of the canonical encoding of `a` (the sign of x used in point pack). */
static int par25519(const gf a) {
    uint8_t d[32];
    pack25519(d, a);
    return d[0] & 1;
}

/* Non-constant-time-safe field inequality: returns 1 if a != b, else 0.
 * Both operands are public (used only on decoded public-key material), and
 * secure_compare keeps the byte comparison itself constant-time. */
static int neq25519(const gf a, const gf b) {
    uint8_t c[32], d[32];
    pack25519(c, a);
    pack25519(d, b);
    return secure_compare(c, d, 32) ? 0 : 1;
}

/* ---- group operations (extended coordinates: p[4] = {X, Y, Z, T}) ------ */

/* Twisted-Edwards point addition p += q. q is read-only; passing p for both
 * arguments performs a doubling (all reads precede all writes, so aliasing is
 * safe). q is left non-const to avoid a pointer-to-array qualifier conversion. */
static void edwards_add(gf p[4], gf q[4]) {
    gf a, b, c, d, t, e, f, g, h;
    sub25519(a, p[1], p[0]);
    sub25519(t, q[1], q[0]);
    mul25519(a, a, t);
    add25519(b, p[0], p[1]);
    add25519(t, q[0], q[1]);
    mul25519(b, b, t);
    mul25519(c, p[3], q[3]);
    mul25519(c, c, D2);
    mul25519(d, p[2], q[2]);
    add25519(d, d, d);
    sub25519(e, b, a);
    sub25519(f, d, c);
    add25519(g, d, c);
    add25519(h, b, a);
    mul25519(p[0], e, f);
    mul25519(p[1], h, g);
    mul25519(p[2], g, f);
    mul25519(p[3], e, h);
}

/* Constant-time conditional swap of points p and q when b == 1. */
static void cswap(gf p[4], gf q[4], uint8_t b) {
    int i;
    for (i = 0; i < 4; i++) {
        sel25519(p[i], q[i], (int)b);
    }
}

/* Compress an extended-coordinate point to 32 bytes (y with the x-sign bit). */
static void ed_pack(uint8_t *r, gf p[4]) {
    gf tx, ty, zi;
    inv25519(zi, p[2]);
    mul25519(tx, p[0], zi);
    mul25519(ty, p[1], zi);
    pack25519(r, ty);
    r[31] ^= (uint8_t)(par25519(tx) << 7);
}

/* p = s * q via a constant-time Montgomery-style ladder over the 256 bits. */
static void scalarmult(gf p[4], gf q[4], const uint8_t *s) {
    int i;
    set25519(p[0], gf0);
    set25519(p[1], gf1);
    set25519(p[2], gf1);
    set25519(p[3], gf0);
    for (i = 255; i >= 0; --i) {
        uint8_t b = (uint8_t)((s[i >> 3] >> (i & 7)) & 1);
        cswap(p, q, b);
        edwards_add(q, p);
        edwards_add(p, p);
        cswap(p, q, b);
    }
}

/* p = s * B (the base point). */
static void scalarbase(gf p[4], const uint8_t *s) {
    gf q[4];
    set25519(q[0], X);
    set25519(q[1], Y);
    set25519(q[2], gf1);
    mul25519(q[3], X, Y);
    scalarmult(p, q, s);
}

/* ---- scalar reduction mod L -------------------------------------------- */

/* r[0..31] = x mod L, where x is a little-endian value in 64 signed bytes. */
static void modL(uint8_t *r, int64_t x[64]) {
    int64_t carry;
    int i, j;
    for (i = 63; i >= 32; --i) {
        carry = 0;
        for (j = i - 32; j < i - 12; ++j) {
            x[j] += carry - 16 * x[i] * L[j - (i - 32)];
            carry = (x[j] + 128) >> 8;
            /* carry can be negative; scale by 256 rather than `carry << 8`,
             * which would be a left shift of a negative value (undefined
             * behavior). The product is defined and stays within int64. */
            x[j] -= carry * 256;
        }
        x[j] += carry;
        x[i] = 0;
    }
    carry = 0;
    for (j = 0; j < 32; j++) {
        x[j] += carry - (x[31] >> 4) * L[j];
        carry = x[j] >> 8;
        x[j] &= 255;
    }
    for (j = 0; j < 32; j++) {
        x[j] -= carry * L[j];
    }
    for (i = 0; i < 32; i++) {
        x[i + 1] += x[i] >> 8;
        r[i] = (uint8_t)(x[i] & 255);
    }
}

/* Reduce a 64-byte little-endian value mod L, in place into r[0..31]. */
static void reduce(uint8_t *r) {
    int64_t x[64];
    int i;
    for (i = 0; i < 64; i++) {
        x[i] = (int64_t)r[i];
    }
    for (i = 0; i < 64; i++) {
        r[i] = 0;
    }
    modL(r, x);
    secure_zero(x, sizeof x);
}

/* ---- point decode ------------------------------------------------------ */

/* Decode a 32-byte compressed point into r = -P (negated, as crypto_sign_open
 * expects). Returns 0 on success, -1 if the encoding is not a valid point. */
static int unpackneg(gf r[4], const uint8_t p[32]) {
    gf t, chk, num, den, den2, den4, den6;
    set25519(r[2], gf1);
    unpack25519(r[1], p);
    sq25519(num, r[1]);
    mul25519(den, num, D);
    sub25519(num, num, r[2]);
    add25519(den, r[2], den);

    sq25519(den2, den);
    sq25519(den4, den2);
    mul25519(den6, den4, den2);
    mul25519(t, den6, num);
    mul25519(t, t, den);

    pow2523(t, t);
    mul25519(t, t, num);
    mul25519(t, t, den);
    mul25519(t, t, den);
    mul25519(r[0], t, den);

    sq25519(chk, r[0]);
    mul25519(chk, chk, den);
    if (neq25519(chk, num)) {
        mul25519(r[0], r[0], SQRTM1);
    }

    sq25519(chk, r[0]);
    mul25519(chk, chk, den);
    if (neq25519(chk, num)) {
        return -1;
    }

    if (par25519(r[0]) == (p[31] >> 7)) {
        sub25519(r[0], gf0, r[0]);
    }

    mul25519(r[3], r[0], r[1]);
    return 0;
}

/* ---- public API -------------------------------------------------------- */

void ed25519_public_key(uint8_t pk[32], const uint8_t seed[32]) {
    uint8_t d[64];
    gf p[4];

    /* Secret scalar a = clamp(H(seed)[0..31]). */
    sha512(seed, 32, d);
    d[0]  &= 248;
    d[31] &= 127;
    d[31] |= 64;

    scalarbase(p, d);
    ed_pack(pk, p);

    secure_zero(d, sizeof d);   /* d[0..31] is the secret scalar */
}

void ed25519_sign(uint8_t sig[64], const uint8_t *m, size_t n,
                  const uint8_t seed[32], const uint8_t pk[32]) {
    uint8_t d[64], hram[64], nonce[64];
    sha512_ctx_t ctx;
    gf p[4];
    int64_t x[64];
    int i, j;

    /* A NULL message is only valid with n == 0. Fail deterministically with an
     * all-zero (unverifiable) signature instead of dereferencing NULL. */
    if (n > 0 && m == NULL) {
        memset(sig, 0, 64);
        return;
    }

    /* d = H(seed): d[0..31] -> clamped secret scalar a; d[32..63] -> prefix. */
    sha512(seed, 32, d);
    d[0]  &= 248;
    d[31] &= 127;
    d[31] |= 64;

    /* nonce r = H(prefix || M) mod L. */
    sha512_init(&ctx);
    sha512_update(&ctx, d + 32, 32);
    if (n) {
        sha512_update(&ctx, m, n);
    }
    sha512_final(&ctx, nonce);
    reduce(nonce);

    /* R = r*B ; sig[0..31] = enc(R). */
    scalarbase(p, nonce);
    ed_pack(sig, p);

    /* k = H(enc(R) || A || M) mod L. */
    sha512_init(&ctx);
    sha512_update(&ctx, sig, 32);
    sha512_update(&ctx, pk, 32);
    if (n) {
        sha512_update(&ctx, m, n);
    }
    sha512_final(&ctx, hram);
    reduce(hram);

    /* S = (r + k*a) mod L ; sig[32..63] = enc(S). */
    for (i = 0; i < 64; i++) {
        x[i] = 0;
    }
    for (i = 0; i < 32; i++) {
        x[i] = (int64_t)nonce[i];
    }
    for (i = 0; i < 32; i++) {
        for (j = 0; j < 32; j++) {
            x[i + j] += (int64_t)hram[i] * (int64_t)d[j];
        }
    }
    modL(sig + 32, x);

    /* Scrub every buffer that touched the secret scalar or nonce. */
    secure_zero(d, sizeof d);
    secure_zero(nonce, sizeof nonce);
    secure_zero(hram, sizeof hram);
    secure_zero(x, sizeof x);
    secure_zero(&ctx, sizeof ctx);
}

int ed25519_verify(const uint8_t sig[64], const uint8_t *m, size_t n,
                   const uint8_t pk[32]) {
    uint8_t h[64], rcheck[32];
    sha512_ctx_t ctx;
    gf p[4], q[4];

    /* A NULL message is only valid with n == 0; reject the invalid call. */
    if (n > 0 && m == NULL) {
        return 0;
    }

    if (unpackneg(q, pk)) {
        return 0;   /* malformed public key */
    }

    /* h = H(enc(R) || A || M) mod L. */
    sha512_init(&ctx);
    sha512_update(&ctx, sig, 32);
    sha512_update(&ctx, pk, 32);
    if (n) {
        sha512_update(&ctx, m, n);
    }
    sha512_final(&ctx, h);
    reduce(h);

    /* rcheck = enc(S*B - h*A). unpackneg gave q = -A, so scalarmult(p,q,h) is
     * -h*A and adding S*B yields S*B - h*A; a valid signature makes this = R. */
    scalarmult(p, q, h);
    scalarbase(q, sig + 32);
    edwards_add(p, q);
    ed_pack(rcheck, p);

    return secure_compare(rcheck, sig, 32) ? 1 : 0;
}

#endif /* WEBLIB_TLS */
