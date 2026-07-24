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
#endif

static int g_failures = 0;

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
#endif /* WEBLIB_TLS */

int main(void) {
#ifndef WEBLIB_TLS
    printf("FAIL: test_tls_crypto built without WEBLIB_TLS defined\n");
    return 1;
#else
    test_chacha20_block();
    test_chacha20_encrypt();

    if (g_failures == 0) {
        printf("All TLS crypto KATs passed.\n");
    }
    return g_failures == 0 ? 0 : 1;
#endif
}
