/*
 * test_tls_crypto.c — known-answer tests for the experimental TLS crypto
 * primitives. Built and run only when configured with -DWEBLIB_ENABLE_TLS=ON.
 *
 * Every primitive is checked against its official RFC test vector; no primitive
 * is trusted (or used elsewhere) until its KAT passes here.
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>

#ifdef WEBLIB_TLS
#include "chacha20.h"
#include "poly1305.h"
#include "chacha20poly1305.h"
#endif

static int g_failures = 0;

static int hexval(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;   /* not a hex digit */
}

/* Decode exactly out_len bytes of hex from `hex`. Fails (marks g_failures, zeroes
 * out) if the string is too short or contains a non-hex character — never reads
 * past the terminating NUL. */
static void from_hex(const char *hex, uint8_t *out, size_t out_len) {
    size_t i;
    for (i = 0; i < out_len; i++) {
        int hi = 0, lo = 0;
        if (hex[i * 2] == '\0' || hex[i * 2 + 1] == '\0' ||
            (hi = hexval(hex[i * 2])) < 0 || (lo = hexval(hex[i * 2 + 1])) < 0) {
            printf("FAIL: from_hex: short or non-hex input\n");
            g_failures++;
            memset(out, 0, out_len);
            return;
        }
        out[i] = (uint8_t)((hi << 4) | lo);
    }
}

static void to_hex(const uint8_t *buf, size_t len, char *out) {
    static const char hexchars[] = "0123456789abcdef";
    size_t i;
    for (i = 0; i < len; i++) {
        out[i * 2]     = hexchars[buf[i] >> 4];
        out[i * 2 + 1] = hexchars[buf[i] & 0x0f];
    }
    out[len * 2] = '\0';
}

static void check_hex(const char *label, const uint8_t *got, size_t len,
                      const char *expected_hex) {
    char hex[512];
    if (len * 2 + 1 > sizeof(hex)) {
        printf("FAIL: %s: buffer too small\n", label);
        g_failures++;
        return;
    }
    to_hex(got, len, hex);
    if (strcmp(hex, expected_hex) != 0) {
        printf("FAIL: %s\n  expected %s\n  got      %s\n", label, expected_hex, hex);
        g_failures++;
    } else {
        printf("PASS: %s\n", label);
    }
}

#ifdef WEBLIB_TLS
/* RFC 8439 §2.3.2 — ChaCha20 block function known-answer test. */
static void test_chacha20_block(void) {
    uint8_t key[32];
    uint8_t nonce[12] = { 0, 0, 0, 0x09, 0, 0, 0, 0x4a, 0, 0, 0, 0 };
    uint8_t out[64];
    int i;
    for (i = 0; i < 32; i++) key[i] = (uint8_t)i;

    chacha20_block(key, 1, nonce, out);
    check_hex("chacha20_block (RFC 8439 2.3.2)", out, sizeof(out),
              "10f1e7e4d13b5915500fdd1fa32071c4"
              "c7d1f4c733c068030422aa9ac3d46c4e"
              "d2826446079faa0914c2d705d98b02a2"
              "b5129cd1de164eb9cbd083e8a2503c4e");
}

/* RFC 8439 §2.4.2 — ChaCha20 encryption known-answer test (114-byte message
 * spanning two keystream blocks, initial counter 1). */
static void test_chacha20_encrypt(void) {
    uint8_t key[32];
    uint8_t nonce[12] = { 0, 0, 0, 0, 0, 0, 0, 0x4a, 0, 0, 0, 0 };
    const char *pt =
        "Ladies and Gentlemen of the class of '99: If I could offer you "
        "only one tip for the future, sunscreen would be it.";
    uint8_t ct[114];
    uint8_t rt[114];
    size_t ptlen = strlen(pt);
    int i;
    for (i = 0; i < 32; i++) key[i] = (uint8_t)i;

    /* Guard the single message length so a future edit to `pt` can't silently
     * overflow ct/rt or compare uninitialized tail bytes. */
    if (ptlen != sizeof(ct)) {
        printf("FAIL: chacha20 test plaintext is %zu bytes, expected %zu\n",
               ptlen, sizeof(ct));
        g_failures++;
        return;
    }

    chacha20_encrypt(key, 1, nonce, (const uint8_t *)pt, ptlen, ct);
    check_hex("chacha20_encrypt (RFC 8439 2.4.2)", ct, ptlen,
              "6e2e359a2568f98041ba0728dd0d6981"
              "e97e7aec1d4360c20a27afccfd9fae0b"
              "f91b65c5524733ab8f593dabcd62b357"
              "1639d624e65152ab8f530c359f0861d8"
              "07ca0dbf500d6a6156a38e088a22b65e"
              "52bc514d16ccf806818ce91ab7793736"
              "5af90bbf74a35be6b40b8eedf2785e42"
              "874d");

    /* Round-trip: decrypting the ciphertext restores the plaintext. */
    chacha20_encrypt(key, 1, nonce, ct, ptlen, rt);
    if (memcmp(rt, pt, ptlen) != 0) {
        printf("FAIL: chacha20 encrypt/decrypt round-trip\n");
        g_failures++;
    } else {
        printf("PASS: chacha20 encrypt/decrypt round-trip\n");
    }
}

/* Poly1305 one-time authenticator — RFC 8439 §2.5.2 plus edge cases (empty,
 * exact block boundary, multi-block, all-zero, and r==0 -> tag==s). */
static void test_poly1305(void) {
    uint8_t key[32];
    uint8_t tag[16];

    /* §2.5.2: 34-byte message = two full blocks + a 2-byte partial. */
    {
        const char *msg = "Cryptographic Forum Research Group";
        from_hex("85d6be7857556d337f4452fe42d506a80103808afb0db2fd4abff6af4149f51b", key, 32);
        poly1305_mac(key, (const uint8_t *)msg, strlen(msg), tag);
        check_hex("poly1305 (RFC 8439 2.5.2)", tag, 16, "a8061dc1305136c6c22b8baf0c0127a9");
    }

    /* Empty message: h stays 0, so tag == s (the HIGH 16 key bytes, 0103...f51b). */
    poly1305_mac(key, (const uint8_t *)"", 0, tag);
    check_hex("poly1305 (empty message)", tag, 16, "0103808afb0db2fd4abff6af4149f51b");

    /* Exactly one full block (no partial tail). */
    poly1305_mac(key, (const uint8_t *)"AAAAAAAAAAAAAAAA", 16, tag);
    check_hex("poly1305 (16-byte exact block)", tag, 16, "de2bee86006afbcacfa53531f0e8a349");

    /* Three full blocks. */
    {
        uint8_t m48[48];
        memset(m48, 'A', sizeof(m48));
        poly1305_mac(key, m48, sizeof(m48), tag);
        check_hex("poly1305 (48-byte, 3 blocks)", tag, 16, "15ca928ec41e68d06bc625c742b0c956");
    }

    /* RFC 8439 A.3 #1: all-zero key + 64-byte zero message -> all-zero tag. */
    {
        uint8_t zk[32] = { 0 };
        uint8_t zm[64] = { 0 };
        poly1305_mac(zk, zm, sizeof(zm), tag);
        check_hex("poly1305 (A.3 #1, all zero)", tag, 16, "00000000000000000000000000000000");
    }

    /* r == 0 (clamped) -> accumulator stays 0 -> tag == s. Exercises the case
     * where no reduction subtraction is needed. */
    {
        const char *msg = "any message here, r is zero so tag=s";
        from_hex("0000000000000000000000000000000036e5f6b5c5e06070f0efca96227a863e", key, 32);
        poly1305_mac(key, (const uint8_t *)msg, strlen(msg), tag);
        check_hex("poly1305 (r=0 -> tag=s)", tag, 16, "36e5f6b5c5e06070f0efca96227a863e");
    }
}

/* ChaCha20-Poly1305 AEAD — RFC 8439 §2.8.2 KAT, a seal/open round-trip, and
 * tamper-detection on the ciphertext, tag, and AAD. */
static void test_chacha20poly1305(void) {
    uint8_t key[32];
    uint8_t nonce[12];
    uint8_t aad[12];
    const char *pt = "Ladies and Gentlemen of the class of '99: If I could offer you "
                     "only one tip for the future, sunscreen would be it.";
    size_t pt_len = strlen(pt);
    uint8_t ct[114];
    uint8_t tag[16];
    uint8_t out[114];
    int i;

    if (pt_len != sizeof(ct)) {
        printf("FAIL: aead test plaintext is %zu bytes, expected %zu\n", pt_len, sizeof(ct));
        g_failures++;
        return;
    }

    for (i = 0; i < 32; i++) key[i] = (uint8_t)(0x80 + i);        /* key = 0x80..0x9f */
    from_hex("070000004041424344454647", nonce, 12);
    from_hex("50515253c0c1c2c3c4c5c6c7", aad, 12);

    /* Seal and check against the RFC vector. */
    if (!chacha20poly1305_seal(key, nonce, aad, sizeof(aad), (const uint8_t *)pt, pt_len, ct, tag)) {
        printf("FAIL: chacha20poly1305_seal returned 0\n");
        g_failures++;
        return;
    }
    check_hex("chacha20poly1305 seal ciphertext (RFC 8439 2.8.2)", ct, pt_len,
              "d31a8d34648e60db7b86afbc53ef7ec2a4aded51296e08fea9e2b5a736ee62d6"
              "3dbea45e8ca9671282fafb69da92728b1a71de0a9e060b2905d6a5b67ecd3b36"
              "92ddbd7f2d778b8c9803aee328091b58fab324e4fad675945585808b4831d7bc"
              "3ff4def08e4b7a9de576d26586cec64b6116");
    check_hex("chacha20poly1305 seal tag (RFC 8439 2.8.2)", tag, 16,
              "1ae10b594f09e26a7e902ecbd0600691");

    /* Open round-trip: authentic input decrypts to the original plaintext. */
    if (chacha20poly1305_open(key, nonce, aad, sizeof(aad), ct, pt_len, tag, out) != 1 ||
        memcmp(out, pt, pt_len) != 0) {
        printf("FAIL: chacha20poly1305 open round-trip\n");
        g_failures++;
    } else {
        printf("PASS: chacha20poly1305 open round-trip\n");
    }

    /* Tamper detection: a single-bit change in ct, tag, or aad must be rejected,
     * and the plaintext buffer must not be touched. */
    {
        uint8_t bad[114];
        uint8_t btag[16];
        uint8_t baad[12];

        memcpy(bad, ct, pt_len); bad[0] ^= 0x01;
        memset(out, 0xEE, sizeof(out));
        if (chacha20poly1305_open(key, nonce, aad, sizeof(aad), bad, pt_len, tag, out) != 0) {
            printf("FAIL: aead accepted tampered ciphertext\n"); g_failures++;
        } else {
            printf("PASS: aead rejects tampered ciphertext\n");
        }

        memcpy(btag, tag, 16); btag[15] ^= 0x80;
        if (chacha20poly1305_open(key, nonce, aad, sizeof(aad), ct, pt_len, btag, out) != 0) {
            printf("FAIL: aead accepted tampered tag\n"); g_failures++;
        } else {
            printf("PASS: aead rejects tampered tag\n");
        }

        memcpy(baad, aad, sizeof(baad)); baad[0] ^= 0x01;
        if (chacha20poly1305_open(key, nonce, baad, sizeof(baad), ct, pt_len, tag, out) != 0) {
            printf("FAIL: aead accepted tampered AAD\n"); g_failures++;
        } else {
            printf("PASS: aead rejects tampered AAD\n");
        }
    }
}
#endif /* WEBLIB_TLS */

int main(void) {
#ifndef WEBLIB_TLS
    printf("FAIL: test_tls_crypto built without WEBLIB_TLS defined\n");
    return 1;
#else
    test_chacha20_block();
    test_chacha20_encrypt();
    test_poly1305();
    test_chacha20poly1305();

    if (g_failures == 0) {
        printf("All TLS crypto KATs passed.\n");
    }
    return g_failures == 0 ? 0 : 1;
#endif
}
