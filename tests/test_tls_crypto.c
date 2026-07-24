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
#include "hkdf.h"
#include "x25519.h"
#include "sha512.h"
#include "ed25519.h"
#include "key_schedule.h"
#include "crypto/sha256.h"
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

/* Boolean expectation (for verify accept/reject outcomes). */
static void check_true(const char *label, int cond) {
    if (cond) {
        printf("PASS: %s\n", label);
    } else {
        printf("FAIL: %s\n", label);
        g_failures++;
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

/* Assert open() both rejected the input (returned 0) AND left the output buffer
 * untouched (still the 0xEE sentinel it was pre-filled with) — i.e. no plaintext
 * was released on an authentication failure. */
static void check_aead_rejected(const char *label, int open_result,
                                const uint8_t *out, size_t out_len) {
    size_t i;
    int untouched = 1;
    for (i = 0; i < out_len; i++) {
        if (out[i] != 0xEE) { untouched = 0; break; }
    }
    if (open_result != 0) {
        printf("FAIL: %s: open() accepted tampered input\n", label);
        g_failures++;
    } else if (!untouched) {
        printf("FAIL: %s: open() wrote to the output buffer on failure\n", label);
        g_failures++;
    } else {
        printf("PASS: %s\n", label);
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
     * and the output buffer must be left untouched (no plaintext released). Each
     * case pre-fills `out` with a 0xEE sentinel that check_aead_rejected verifies. */
    {
        uint8_t bad[114];
        uint8_t btag[16];
        uint8_t baad[12];
        int r;

        memcpy(bad, ct, pt_len); bad[0] ^= 0x01;
        memset(out, 0xEE, sizeof(out));
        r = chacha20poly1305_open(key, nonce, aad, sizeof(aad), bad, pt_len, tag, out);
        check_aead_rejected("aead rejects tampered ciphertext", r, out, sizeof(out));

        memcpy(btag, tag, 16); btag[15] ^= 0x80;
        memset(out, 0xEE, sizeof(out));
        r = chacha20poly1305_open(key, nonce, aad, sizeof(aad), ct, pt_len, btag, out);
        check_aead_rejected("aead rejects tampered tag", r, out, sizeof(out));

        memcpy(baad, aad, sizeof(baad)); baad[0] ^= 0x01;
        memset(out, 0xEE, sizeof(out));
        r = chacha20poly1305_open(key, nonce, baad, sizeof(baad), ct, pt_len, tag, out);
        check_aead_rejected("aead rejects tampered AAD", r, out, sizeof(out));
    }
}

/* HKDF-SHA256 (RFC 5869) + TLS 1.3 HKDF-Expand-Label (RFC 8446), checked against
 * RFC 5869 §A.1/A.3 and the RFC 8448 TLS 1.3 trace (early_secret / derived). */
static void test_hkdf(void) {
    uint8_t prk[32];
    uint8_t okm[64];

    /* RFC 5869 A.1: Extract + Expand (L=42 spans two HMAC blocks). */
    {
        uint8_t ikm[22];
        uint8_t salt[13];
        uint8_t info[10];
        memset(ikm, 0x0b, sizeof(ikm));
        from_hex("000102030405060708090a0b0c", salt, sizeof(salt));
        from_hex("f0f1f2f3f4f5f6f7f8f9", info, sizeof(info));

        hkdf_extract(salt, sizeof(salt), ikm, sizeof(ikm), prk);
        check_hex("hkdf_extract (RFC 5869 A.1 PRK)", prk, 32,
                  "077709362c2e32df0ddc3f0dc47bba6390b6c73bb50f9c3122ec844ad7c2b3e5");
        if (!hkdf_expand(prk, info, sizeof(info), okm, 42)) {
            printf("FAIL: hkdf_expand A.1 returned 0\n"); g_failures++;
        } else {
            check_hex("hkdf_expand (RFC 5869 A.1 OKM)", okm, 42,
                      "3cb25f25faacd57a90434f64d0362f2a2d2d0a90cf1a5a4c5db02d56e"
                      "cc4c5bf34007208d5b887185865");
        }
    }

    /* RFC 5869 A.3: zero-length salt and info (empty salt == HashLen zeros). */
    {
        uint8_t ikm[22];
        memset(ikm, 0x0b, sizeof(ikm));

        hkdf_extract(NULL, 0, ikm, sizeof(ikm), prk);
        check_hex("hkdf_extract (RFC 5869 A.3 PRK, empty salt)", prk, 32,
                  "19ef24a32c717b167f33a91d6f648bdf96596776afdb6377ac434c1c293ccb04");
        if (!hkdf_expand(prk, NULL, 0, okm, 42)) {
            printf("FAIL: hkdf_expand A.3 returned 0\n"); g_failures++;
        } else {
            check_hex("hkdf_expand (RFC 5869 A.3 OKM, empty info)", okm, 42,
                      "8da4e775a563c18f715f802a063c5a31b8a11f5c5ee1879ec3454e5f"
                      "3c738d2d9d201395faa4b61a96c8");
        }
    }

    /* RFC 8448 TLS 1.3: early_secret = HKDF-Extract(0^32, 0^32);
     * derived = HKDF-Expand-Label(early_secret, "derived", SHA256(""), 32). */
    {
        uint8_t zeros[32] = { 0 };
        uint8_t early[32];
        uint8_t empty_hash[32];
        uint8_t derived[32];

        hkdf_extract(zeros, sizeof(zeros), zeros, sizeof(zeros), early);
        check_hex("hkdf early_secret (RFC 8448 Extract)", early, 32,
                  "33ad0a1c607ec03b09e6cd9893680ce210adf300aa1f2660e1b22e10f170f92a");

        from_hex("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
                 empty_hash, 32);   /* SHA256("") */
        if (!hkdf_expand_label(early, "derived", 7, empty_hash, 32, derived, 32)) {
            printf("FAIL: hkdf_expand_label returned 0\n"); g_failures++;
        } else {
            check_hex("hkdf_expand_label \"derived\" (RFC 8448)", derived, 32,
                      "6f2615a108c702c5678f54fc9dbab69716c076189c48250cebeac3576c3611ba");
        }
    }

    /* Invalid-input guards: a NULL pointer paired with a non-zero length must be
     * rejected (returning 0) rather than dereferenced. */
    {
        uint8_t junk[32];
        if (hkdf_expand(prk, NULL, 5, junk, 32) != 0) {
            printf("FAIL: hkdf_expand accepted NULL info with non-zero length\n"); g_failures++;
        } else {
            printf("PASS: hkdf_expand rejects NULL info + non-zero length\n");
        }
        if (hkdf_expand_label(prk, NULL, 5, NULL, 0, junk, 32) != 0) {
            printf("FAIL: hkdf_expand_label accepted NULL label with non-zero length\n"); g_failures++;
        } else {
            printf("PASS: hkdf_expand_label rejects NULL label + non-zero length\n");
        }
        if (hkdf_expand_label(prk, "x", 1, NULL, 5, junk, 32) != 0) {
            printf("FAIL: hkdf_expand_label accepted NULL context with non-zero length\n"); g_failures++;
        } else {
            printf("PASS: hkdf_expand_label rejects NULL context + non-zero length\n");
        }
    }
}

/* SHA-512 (FIPS 180-4) known-answer tests, incl. a multi-block message and a
 * streaming-equals-one-shot check. */
static void test_sha512(void) {
    uint8_t d[SHA512_DIGEST_SIZE];

    sha512((const uint8_t *)"abc", 3, d);
    check_hex("sha512 (\"abc\")", d, 64,
              "ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a"
              "2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f");

    sha512((const uint8_t *)"", 0, d);
    check_hex("sha512 (empty)", d, 64,
              "cf83e1357eefb8bdf1542850d66d8007d620e4050b5715dc83f4a921d36ce9ce"
              "47d0d13c5d85f2b0ff8318d2877eec2f63b931bd47417a81a538327af927da3e");

    /* 112-byte (two-block) NIST vector. */
    sha512((const uint8_t *)"abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmno"
                            "ijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu", 112, d);
    check_hex("sha512 (896-bit, 2 blocks)", d, 64,
              "8e959b75dae313da8cf4f72814fc143f8f7779c6eb9f7fa17299aeadb6889018"
              "501d289e4900f7e4331b99dec4b5433ac7d329eeb6dd26545e96e55b874be909");

    /* Streaming (odd chunk sizes over a 1000-byte message) must equal one-shot. */
    {
        uint8_t big[1000];
        uint8_t one[SHA512_DIGEST_SIZE];
        sha512_ctx_t ctx;
        size_t off = 0;
        size_t chunk = 1;
        memset(big, 'a', sizeof(big));
        sha512(big, sizeof(big), one);
        sha512_init(&ctx);
        while (off < sizeof(big)) {
            size_t n = (sizeof(big) - off < chunk) ? (sizeof(big) - off) : chunk;
            sha512_update(&ctx, big + off, n);
            off += n;
            chunk = chunk * 2 + 1;   /* 1,3,7,15,... spans multiple blocks */
        }
        sha512_final(&ctx, d);
        if (memcmp(d, one, sizeof(d)) != 0) {
            printf("FAIL: sha512 streaming != one-shot\n"); g_failures++;
        } else {
            check_hex("sha512 (1000 'a', streaming==one-shot)", d, 64,
                      "67ba5535a46e3f86dbfbed8cbbaf0125c76ed549ff8b0b9e03e0c88cf90fa634"
                      "fa7b12b47d77b694de488ace8d9a65967dc96df599727d3292a8d9d447709c97");
        }
    }

    /* Input sensitivity: a single-bit change in the message must change the
     * digest. A committed guard against a degenerate/stub or constant-returning
     * implementation (complements the dev-time known-answer negative controls). */
    {
        uint8_t da[SHA512_DIGEST_SIZE];
        uint8_t db[SHA512_DIGEST_SIZE];
        sha512((const uint8_t *)"abc", 3, da);
        sha512((const uint8_t *)"abd", 3, db);   /* 'c' ^ 0x07 = 'd' */
        if (memcmp(da, db, sizeof(da)) == 0) {
            printf("FAIL: sha512 not input-sensitive (digest unchanged)\n"); g_failures++;
        } else {
            printf("PASS: sha512 input-sensitive (1-byte change alters digest)\n");
        }
    }
}

/* X25519 ECDH (RFC 7748) — §5.2 scalar-mult vectors, §6.1 base point + agreement,
 * and the iterated test (1 and 1000 rounds exercise the ladder end to end). */
static void test_x25519(void) {
    uint8_t k[32];
    uint8_t u[32];
    uint8_t out[32];

    /* §5.2 Test 1. */
    from_hex("a546e36bf0527c9d3b16154b82465edd62144c0ac1fc5a18506a2244ba449ac4", k, 32);
    from_hex("e6db6867583030db3594c1a424b15f7c726624ec26b3353b10a903a6d0ab1c4c", u, 32);
    x25519(out, k, u);
    check_hex("x25519 (RFC 7748 5.2 #1)", out, 32,
              "c3da55379de9c6908e94ea4df28d084f32eccf03491c71f754b4075577a28552");

    /* §5.2 Test 2. */
    from_hex("4b66e9d4d1b4673c5ad22691957d6af5c11b6421e0ea01d42ca4169e7918ba0d", k, 32);
    from_hex("e5210f12786811d3f4b7959d0538ae2c31dbe7106fc03c3efc4cd549c715a493", u, 32);
    x25519(out, k, u);
    check_hex("x25519 (RFC 7748 5.2 #2)", out, 32,
              "95cbde9476e8907d7aade45cb4b873f88b595a68799fa152e6f8f7647aac7957");

    /* §6.1: derive Alice's public key from her private scalar via the base point. */
    from_hex("77076d0a7318a57d3c16c17251b26645df4c2f87ebc0992ab177fba51db92c2a", k, 32);
    x25519_base(out, k);
    check_hex("x25519_base (RFC 7748 6.1 Alice pubkey)", out, 32,
              "8520f0098930a754748b7ddcb43ef75a0dbf3a0d26381af4eba4a98eaa9b4e6a");

    /* §6.1: full ECDH — Alice and Bob compute the same shared secret. */
    {
        uint8_t apriv[32], bpriv[32], apub[32], bpub[32], ss1[32], ss2[32];
        from_hex("77076d0a7318a57d3c16c17251b26645df4c2f87ebc0992ab177fba51db92c2a", apriv, 32);
        from_hex("5dab087e624a8a4b79e17f8b83800ee66f3bb1292618b6fd1c2f8b27ff88e0eb", bpriv, 32);
        x25519_base(apub, apriv);
        x25519_base(bpub, bpriv);
        check_hex("x25519_base (RFC 7748 6.1 Bob pubkey)", bpub, 32,
                  "de9edb7d7b7dc1b4d35b61c2ece435373f8343c85b78674dadfc7e146f882b4f");
        x25519(ss1, apriv, bpub);   /* Alice's view */
        x25519(ss2, bpriv, apub);   /* Bob's view */
        if (memcmp(ss1, ss2, 32) != 0) {
            printf("FAIL: x25519 ECDH shared secrets differ\n"); g_failures++;
        } else {
            check_hex("x25519 ECDH shared secret (RFC 7748 6.1)", ss1, 32,
                      "4a5d9d5ba4ce2de1728e3bf480350f25e07e21c947d19e3376f09b3c1e161742");
        }
    }

    /* Iterated test (RFC 7748 §5.2): k=u=basepoint, then k = X25519(k,u), u = old k. */
    {
        uint8_t kk[32], uu[32], r[32];
        int it;
        memset(kk, 0, sizeof(kk));
        kk[0] = 9;
        memcpy(uu, kk, sizeof(uu));
        for (it = 1; it <= 1000; it++) {
            x25519(r, kk, uu);
            memcpy(uu, kk, sizeof(uu));
            memcpy(kk, r, sizeof(kk));
            if (it == 1) {
                check_hex("x25519 iterated (1 round)", kk, 32,
                          "422c8e7a6227d7bca1350b3e2bb7279f7897b87bb6854b783c60e80311ae3079");
            }
            if (it == 1000) {
                check_hex("x25519 iterated (1000 rounds)", kk, 32,
                          "684cf59ba83309552800ef566f2f4d3c1c3887c49360e3875f2eb94d99532c51");
            }
        }
    }
}

/* RFC 8032 §7.1 — Ed25519 detached sign/verify known-answer tests.
 * The RFC TEST2/TEST3 vectors and all constants were cross-checked against an
 * independent from-scratch reference before being hardcoded here. */
static void test_ed25519(void) {
    uint8_t seed[32], pk[32], sig[64], got_pk[32], got_sig[64];
    uint8_t msg[64];
    int i;

    /* --- RFC 8032 TEST 2: 1-byte message 0x72 (sign + verify). --- */
    from_hex("4ccd089b28ff96da9db6c346ec114e0f5b8a319f35aba624da8cf6ed4fb8a6fb", seed, 32);
    msg[0] = 0x72;
    ed25519_public_key(got_pk, seed);
    check_hex("ed25519 public key (RFC 8032 TEST2)", got_pk, 32,
              "3d4017c3e843895a92b70aa74d1b7ebc9c982ccf2ec4968cc0cd55f12af4660c");
    from_hex("3d4017c3e843895a92b70aa74d1b7ebc9c982ccf2ec4968cc0cd55f12af4660c", pk, 32);
    ed25519_sign(got_sig, msg, 1, seed, pk);
    check_hex("ed25519 sign (RFC 8032 TEST2)", got_sig, 64,
              "92a009a9f0d4cab8720e820b5f642540a2b27b5416503f8fb3762223ebdb69da"
              "085ac1e43e15996e458f3613d0f11d8c387b2eaeb4302aeeb00d291612bb0c00");
    check_true("ed25519 verify accepts valid (TEST2)",
               ed25519_verify(got_sig, msg, 1, pk) == 1);

    /* --- RFC 8032 TEST 3: 2-byte message af82 (sign + verify + tamper). --- */
    from_hex("c5aa8df43f9f837bedb7442f31dcb7b166d38535076f094b85ce3a2e0b4458f7", seed, 32);
    msg[0] = 0xaf; msg[1] = 0x82;
    ed25519_public_key(got_pk, seed);
    check_hex("ed25519 public key (RFC 8032 TEST3)", got_pk, 32,
              "fc51cd8e6218a1a38da47ed00230f0580816ed13ba3303ac5deb911548908025");
    from_hex("fc51cd8e6218a1a38da47ed00230f0580816ed13ba3303ac5deb911548908025", pk, 32);
    ed25519_sign(got_sig, msg, 2, seed, pk);
    check_hex("ed25519 sign (RFC 8032 TEST3)", got_sig, 64,
              "6291d657deec24024827e69c3abe01a30ce548a284743a445e3680d7db5ac3ac"
              "18ff9b538d16f290ae67f760984dc6594a7c15e9716ed28dc027beceea1ec40a");
    check_true("ed25519 verify accepts valid (TEST3)",
               ed25519_verify(got_sig, msg, 2, pk) == 1);

    /* Negative controls: tampered signature, tampered message, wrong key. */
    got_sig[10] ^= 0x01;
    check_true("ed25519 verify rejects tampered signature",
               ed25519_verify(got_sig, msg, 2, pk) == 0);
    got_sig[10] ^= 0x01;   /* restore valid signature */
    msg[0] ^= 0x01;
    check_true("ed25519 verify rejects tampered message",
               ed25519_verify(got_sig, msg, 2, pk) == 0);
    msg[0] ^= 0x01;        /* restore message */
    {
        uint8_t bad_pk[32];
        memcpy(bad_pk, pk, 32);
        bad_pk[0] ^= 0x01;
        check_true("ed25519 verify rejects wrong public key",
                   ed25519_verify(got_sig, msg, 2, bad_pk) == 0);
    }

    /* --- RFC 8032 TEST 1: empty message, verify-only against the published
     * public key + signature (exercises the n == 0 / NULL-message path). --- */
    from_hex("d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a", pk, 32);
    from_hex("e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e065224901555fb8821590a33bacc61e39701cf9b46bd25bf5f0595bbe24655141438e7a100b", sig, 64);
    check_true("ed25519 verify accepts valid (RFC 8032 TEST1, empty message)",
               ed25519_verify(sig, NULL, 0, pk) == 1);

    /* --- 64-byte message (reference-impl-generated vector, cross-validated)
     * to exercise a multi-word message length end to end. --- */
    from_hex("c5aa8df43f9f837bedb7442f31dcb7b166d38535076f094b85ce3a2e0b4458f7", seed, 32);
    for (i = 0; i < 64; i++) msg[i] = (uint8_t)i;
    ed25519_public_key(got_pk, seed);
    from_hex("fc51cd8e6218a1a38da47ed00230f0580816ed13ba3303ac5deb911548908025", pk, 32);
    ed25519_sign(got_sig, msg, 64, seed, pk);
    check_hex("ed25519 sign (64-byte message)", got_sig, 64,
              "c7e0ff26d1d24bb61a5ce86aeb8a12438f2332e388842db63cccf86d6e334114"
              "d2ad120d14f47f7afe3110e8c9ba00d79ba40cfcf6b4741c212ecdb387444006");
    check_true("ed25519 verify accepts valid (64-byte message)",
               ed25519_verify(got_sig, msg, 64, pk) == 1);

    /* Robustness: a NULL message with n > 0 is an invalid call and must fail
     * deterministically (not dereference NULL) — sign zeroes the signature and
     * verify returns 0 (Copilot review). seed/pk from the 64-byte case above. */
    {
        uint8_t bad[64];
        int k, allzero = 1;
        memset(bad, 0xEE, sizeof bad);
        ed25519_sign(bad, NULL, 5, seed, pk);
        for (k = 0; k < 64; k++) {
            if (bad[k] != 0) allzero = 0;
        }
        check_true("ed25519 sign (NULL msg, n>0) yields zeroed signature", allzero);
        check_true("ed25519 verify (NULL msg, n>0) rejects",
                   ed25519_verify(got_sig, NULL, 5, pk) == 0);
    }
}

/* RFC 8446 §7.1 key schedule. early_secret and derived are the authoritative
 * RFC 8448 trace anchors; the rest of the chain is exercised with a fixed ECDHE
 * (the RFC 7748 §6.1 shared secret) and synthetic transcript hashes (SHA-256 of
 * marker strings — the key-schedule math is independent of the transcript's
 * content), all cross-checked against an independent HKDF-SHA256 reference. */
static void test_key_schedule(void) {
    uint8_t zero[32] = {0};
    uint8_t early[32], empty_hash[32], derived[32], ecdhe[32], hs[32];
    uint8_t th_ch_sh[32], c_hs[32], s_hs[32], derived_hs[32], master[32];
    uint8_t th_ch_sf[32], s_ap[32];
    uint8_t key[32], iv[12], fin[32];
    const char *m_ch_sh = "ClientHello||ServerHello";
    const char *m_ch_sf = "ClientHello..server Finished";

    /* Early Secret = HKDF-Extract(0, 0). */
    tls13_extract(zero, 32, zero, 32, early);
    check_hex("ks early_secret (RFC 8448)", early, 32,
              "33ad0a1c607ec03b09e6cd9893680ce210adf300aa1f2660e1b22e10f170f92a");

    /* derived = Derive-Secret(Early, "derived", ""). */
    tls13_empty_transcript_hash(empty_hash);
    check_hex("ks empty transcript hash", empty_hash, 32,
              "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    check_true("ks derive_secret(derived) ok",
               tls13_derive_secret(early, "derived", empty_hash, derived) == 1);
    check_hex("ks derived (RFC 8448)", derived, 32,
              "6f2615a108c702c5678f54fc9dbab69716c076189c48250cebeac3576c3611ba");

    /* Handshake Secret = HKDF-Extract(derived, ECDHE). */
    from_hex("4a5d9d5ba4ce2de1728e3bf480350f25e07e21c947d19e3376f09b3c1e161742", ecdhe, 32);
    tls13_extract(derived, 32, ecdhe, 32, hs);
    check_hex("ks handshake_secret", hs, 32,
              "e4e520f8ca639e6562121a8d006bbce3e012f049744806f283e99c54cab713f3");

    /* Handshake traffic secrets over the ClientHello..ServerHello transcript. */
    sha256((const uint8_t *)m_ch_sh, strlen(m_ch_sh), th_ch_sh);
    tls13_derive_secret(hs, "c hs traffic", th_ch_sh, c_hs);
    check_hex("ks c_hs_traffic", c_hs, 32,
              "cb24193a3801afad533babae440f2c5ea5d4a0a667c7811d45ec9b89441facaf");
    tls13_derive_secret(hs, "s hs traffic", th_ch_sh, s_hs);
    check_hex("ks s_hs_traffic", s_hs, 32,
              "6186d5e4a6913a51e3d93c674e957c47ab3a8e06ae6f1568f9a53ab0072430ba");

    /* Master Secret = HKDF-Extract(Derive-Secret(HS, "derived", ""), 0). */
    tls13_derive_secret(hs, "derived", empty_hash, derived_hs);
    check_hex("ks derived_hs", derived_hs, 32,
              "b47c8bd24c77a2a9fbf501b9b592afa8fe7c248b16b6db86f209f47aa2de64d4");
    tls13_extract(derived_hs, 32, zero, 32, master);
    check_hex("ks master_secret", master, 32,
              "34f6eb660fafe5471a480c287f29ec6f688153423a8c35ad4c15e072c576875a");

    /* Application traffic secret over the ClientHello..server Finished transcript. */
    sha256((const uint8_t *)m_ch_sf, strlen(m_ch_sf), th_ch_sf);
    tls13_derive_secret(master, "s ap traffic", th_ch_sf, s_ap);
    check_hex("ks s_ap_traffic", s_ap, 32,
              "345773668b61ec576ebf34546f30121824a5c01da76ca6277bbaefd28a3ab9b1");

    /* Traffic key/IV (ChaCha20: 32/12) + Finished key from s_hs_traffic. */
    check_true("ks traffic_keys ok", tls13_traffic_keys(s_hs, key, 32, iv) == 1);
    check_hex("ks s_hs write_key", key, 32,
              "65b1acab64981f4a389f7cbb61960e188cba36c394347b6f2dec27815679627e");
    check_hex("ks s_hs write_iv", iv, 12, "49a58cdfb9fb269bfc6d10a7");
    check_true("ks finished_key ok", tls13_finished_key(s_hs, fin) == 1);
    check_hex("ks s_hs finished_key", fin, 32,
              "3517ba1c0647b82fe6db82add2ed8314b1ea4acb2c821f1dbcc1ffd557a975f6");
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
    test_hkdf();
    test_sha512();
    test_x25519();
    test_ed25519();
    test_key_schedule();

    if (g_failures == 0) {
        printf("All TLS crypto KATs passed.\n");
    }
    return g_failures == 0 ? 0 : 1;
#endif
}
