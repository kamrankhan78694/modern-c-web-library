/*
 * poly1305.c — Poly1305 one-time authenticator (RFC 8439 §2.5). EXPERIMENTAL / UNAUDITED.
 *
 * From-scratch implementation for the experimental pure-C TLS layer. Compiled
 * only under -DWEBLIB_ENABLE_TLS=ON. Verified against RFC 8439 §2.5.2 (and edge
 * cases) in tests/test_tls_crypto.c.
 *
 * The accumulator is held in five 26-bit limbs (h0..h4). Each 16-byte message
 * block, taken as a little-endian integer with an appended high bit, is added to
 * the accumulator, which is then multiplied by the clamped key r modulo the prime
 * 2^130-5. All arithmetic is on fixed-size integers with no secret-dependent
 * branch, index, or memory access.
 */
#include "poly1305.h"

#ifdef WEBLIB_TLS

#include "kamran.k"   /* secure_zero */
#include <string.h>

static uint32_t load32_le(const uint8_t *p) {
    return (uint32_t)p[0] |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static void store32_le(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v);
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

void poly1305_mac(const uint8_t key[32], const uint8_t *msg, size_t len,
                  uint8_t tag[16]) {
    uint32_t r0, r1, r2, r3, r4;
    uint32_t s1, s2, s3, s4;
    uint32_t h0, h1, h2, h3, h4;
    uint32_t t0, t1, t2, t3;
    uint32_t c;
    uint8_t final_block[16];

    /* Load and clamp r (the low 16 bytes of the key) into five 26-bit limbs.
     * Clamp mask overall: 0x0ffffffc0ffffffc0ffffffc0fffffff. */
    t0 = load32_le(key + 0);
    t1 = load32_le(key + 4);
    t2 = load32_le(key + 8);
    t3 = load32_le(key + 12);
    r0 =  t0                        & 0x03ffffff;
    r1 = ((t0 >> 26) | (t1 <<  6))  & 0x03ffff03;
    r2 = ((t1 >> 20) | (t2 << 12))  & 0x03ffc0ff;
    r3 = ((t2 >> 14) | (t3 << 18))  & 0x03f03fff;
    r4 =  (t3 >>  8)                & 0x000fffff;

    /* Precompute 5*r1..5*r4 for the reduction mod 2^130-5. */
    s1 = r1 * 5;
    s2 = r2 * 5;
    s3 = r3 * 5;
    s4 = r4 * 5;

    h0 = h1 = h2 = h3 = h4 = 0;

    while (len > 0) {
        const uint8_t *block;
        uint32_t hibit;
        uint64_t d0, d1, d2, d3, d4;

        if (len >= 16) {
            block = msg;
            hibit = (UINT32_C(1) << 24);   /* 2^128: full block appends 0x01 at byte 16 */
            msg += 16;
            len -= 16;
        } else {
            /* Final partial block: copy, append the 0x01 bit right after the data. */
            memset(final_block, 0, sizeof(final_block));
            memcpy(final_block, msg, len);
            final_block[len] = 1;
            block = final_block;
            hibit = 0;                       /* the high bit is explicit in the buffer */
            len = 0;
        }

        /* h += block */
        t0 = load32_le(block + 0);
        t1 = load32_le(block + 4);
        t2 = load32_le(block + 8);
        t3 = load32_le(block + 12);
        h0 +=  t0                       & 0x03ffffff;
        h1 += ((t0 >> 26) | (t1 <<  6)) & 0x03ffffff;
        h2 += ((t1 >> 20) | (t2 << 12)) & 0x03ffffff;
        h3 += ((t2 >> 14) | (t3 << 18)) & 0x03ffffff;
        h4 +=  (t3 >>  8) | hibit;

        /* h *= r (schoolbook, folding the 2^130 overflow back with the *5 terms) */
        d0 = (uint64_t)h0 * r0 + (uint64_t)h1 * s4 + (uint64_t)h2 * s3 + (uint64_t)h3 * s2 + (uint64_t)h4 * s1;
        d1 = (uint64_t)h0 * r1 + (uint64_t)h1 * r0 + (uint64_t)h2 * s4 + (uint64_t)h3 * s3 + (uint64_t)h4 * s2;
        d2 = (uint64_t)h0 * r2 + (uint64_t)h1 * r1 + (uint64_t)h2 * r0 + (uint64_t)h3 * s4 + (uint64_t)h4 * s3;
        d3 = (uint64_t)h0 * r3 + (uint64_t)h1 * r2 + (uint64_t)h2 * r1 + (uint64_t)h3 * r0 + (uint64_t)h4 * s4;
        d4 = (uint64_t)h0 * r4 + (uint64_t)h1 * r3 + (uint64_t)h2 * r2 + (uint64_t)h3 * r1 + (uint64_t)h4 * r0;

        /* Partial carry propagation back into 26-bit limbs. */
        c  = (uint32_t)(d0 >> 26); h0 = (uint32_t)d0 & 0x03ffffff;
        d1 += c; c = (uint32_t)(d1 >> 26); h1 = (uint32_t)d1 & 0x03ffffff;
        d2 += c; c = (uint32_t)(d2 >> 26); h2 = (uint32_t)d2 & 0x03ffffff;
        d3 += c; c = (uint32_t)(d3 >> 26); h3 = (uint32_t)d3 & 0x03ffffff;
        d4 += c; c = (uint32_t)(d4 >> 26); h4 = (uint32_t)d4 & 0x03ffffff;
        h0 += c * 5; c = h0 >> 26; h0 &= 0x03ffffff;
        h1 += c;
    }

    /* Fully carry h. */
    c = h1 >> 26; h1 &= 0x03ffffff; h2 += c;
    c = h2 >> 26; h2 &= 0x03ffffff; h3 += c;
    c = h3 >> 26; h3 &= 0x03ffffff; h4 += c;
    c = h4 >> 26; h4 &= 0x03ffffff; h0 += c * 5;
    c = h0 >> 26; h0 &= 0x03ffffff; h1 += c;

    /* Compute g = h - p = h + 5 - 2^130, then select g if h >= p else h. */
    {
        uint32_t g0, g1, g2, g3, g4;
        uint32_t mask;

        g0 = h0 + 5; c = g0 >> 26; g0 &= 0x03ffffff;
        g1 = h1 + c; c = g1 >> 26; g1 &= 0x03ffffff;
        g2 = h2 + c; c = g2 >> 26; g2 &= 0x03ffffff;
        g3 = h3 + c; c = g3 >> 26; g3 &= 0x03ffffff;
        g4 = h4 + c - (UINT32_C(1) << 26);

        /* mask = 0xffffffff if h >= p (g did not borrow), else 0. */
        mask = (g4 >> 31) - 1;
        g0 &= mask; g1 &= mask; g2 &= mask; g3 &= mask; g4 &= mask;
        mask = ~mask;
        h0 = (h0 & mask) | g0;
        h1 = (h1 & mask) | g1;
        h2 = (h2 & mask) | g2;
        h3 = (h3 & mask) | g3;
        h4 = (h4 & mask) | g4;
    }

    /* Reassemble the 130-bit value into four 32-bit words (mod 2^128). */
    {
        uint32_t hh0, hh1, hh2, hh3;
        uint64_t f;

        hh0 = ((h0)       | (h1 << 26));
        hh1 = ((h1 >>  6) | (h2 << 20));
        hh2 = ((h2 >> 12) | (h3 << 14));
        hh3 = ((h3 >> 18) | (h4 <<  8));

        /* tag = (h + s) mod 2^128, where s is the high 16 bytes of the key. */
        f = (uint64_t)hh0 + load32_le(key + 16);                 hh0 = (uint32_t)f;
        f = (uint64_t)hh1 + load32_le(key + 20) + (f >> 32);     hh1 = (uint32_t)f;
        f = (uint64_t)hh2 + load32_le(key + 24) + (f >> 32);     hh2 = (uint32_t)f;
        f = (uint64_t)hh3 + load32_le(key + 28) + (f >> 32);     hh3 = (uint32_t)f;

        store32_le(tag + 0,  hh0);
        store32_le(tag + 4,  hh1);
        store32_le(tag + 8,  hh2);
        store32_le(tag + 12, hh3);
    }

    /* Wipe the stack copy of the last message block. The key-derived scalars
     * (r/s) and accumulator (h) are held in local scalars that a portable C
     * implementation cannot reliably clear (the compiler may keep register-only
     * copies), so they are not "wiped" with an ineffective self-assignment; the
     * one-time key they derive from is the caller's to manage. */
    secure_zero(final_block, sizeof(final_block));
}

#endif /* WEBLIB_TLS */
