/*
 * test_tls_parse.c — tests for the experimental TLS parsing layer (DER/ASN.1;
 * PEM and X.509 will join here). Built only with -DWEBLIB_ENABLE_TLS=ON.
 *
 * The DER reader parses untrusted input, so malformed-input rejection is
 * weighted as heavily as correct decoding: truncation, indefinite and
 * non-minimal lengths, multi-byte tags, and out-of-bounds values must each be
 * rejected cleanly (return -1) — never crash or over-read.
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>

#ifdef WEBLIB_TLS
#include "der.h"
#include "pem.h"
#include "ed25519_key.h"
#include "ed25519.h"
#endif

static int g_failures = 0;

static void check_true(const char *label, int cond) {
    if (cond) {
        printf("PASS: %s\n", label);
    } else {
        printf("FAIL: %s\n", label);
        g_failures++;
    }
}

#ifdef WEBLIB_TLS

/* Valid: SEQUENCE { INTEGER 5, OCTET STRING 'A' } — walk it end to end. */
static void test_der_valid_sequence(void) {
    static const uint8_t in[] = {
        0x30, 0x06,             /* SEQUENCE, length 6 */
        0x02, 0x01, 0x05,       /*   INTEGER 5 */
        0x04, 0x01, 0x41        /*   OCTET STRING "A" */
    };
    der_reader_t r, seq;
    const uint8_t *val;
    size_t vlen;

    der_init(&r, in, sizeof in);
    check_true("der: enter SEQUENCE", der_enter(&r, DER_TAG_SEQUENCE, &seq) == 0);
    check_true("der: outer fully consumed", der_at_end(&r));

    check_true("der: read INTEGER value",
               der_expect(&seq, DER_TAG_INTEGER, &val, &vlen) == 0
               && vlen == 1 && val[0] == 0x05);
    check_true("der: read OCTET STRING value",
               der_expect(&seq, DER_TAG_OCTET_STRING, &val, &vlen) == 0
               && vlen == 1 && val[0] == 0x41);
    check_true("der: inner fully consumed", der_at_end(&seq));
}

/* Valid: long-form length (a 200-byte OCTET STRING: 0x04 0x81 0xC8 <200>). */
static void test_der_long_form_length(void) {
    uint8_t in[3 + 200];
    der_reader_t r;
    const uint8_t *val;
    size_t vlen;
    int i;

    in[0] = 0x04;       /* OCTET STRING */
    in[1] = 0x81;       /* long form, one length octet follows */
    in[2] = 0xC8;       /* 200 */
    for (i = 0; i < 200; i++) {
        in[3 + i] = (uint8_t)i;
    }

    der_init(&r, in, sizeof in);
    check_true("der: long-form length decodes",
               der_expect(&r, DER_TAG_OCTET_STRING, &val, &vlen) == 0
               && vlen == 200 && val[0] == 0 && val[199] == 199
               && der_at_end(&r));
}

/* Each malformed encoding must be rejected by der_read_tlv (-1). */
static void test_der_rejects_malformed(void) {
    uint8_t tag;
    const uint8_t *val;
    size_t vlen;
    der_reader_t r;

    der_init(&r, NULL, 0);
    check_true("der: rejects empty input",
               der_read_tlv(&r, &tag, &val, &vlen) == -1);

    /* A NULL buffer claiming a non-zero length must be handled as empty (no
     * pointer arithmetic/comparison on NULL), not crash or over-read. */
    der_init(&r, NULL, 5);
    check_true("der: NULL buffer with nonzero len treated as empty",
               der_read_tlv(&r, &tag, &val, &vlen) == -1 && der_remaining(&r) == 0);

    {   /* value runs past the buffer: claims length 5, only 2 value bytes */
        static const uint8_t b[] = { 0x04, 0x05, 0x41, 0x42 };
        der_init(&r, b, sizeof b);
        check_true("der: rejects truncated value",
                   der_read_tlv(&r, &tag, &val, &vlen) == -1);
    }
    {   /* long form 0x82 promises two length octets, only one present */
        static const uint8_t b[] = { 0x04, 0x82, 0x01 };
        der_init(&r, b, sizeof b);
        check_true("der: rejects truncated length field",
                   der_read_tlv(&r, &tag, &val, &vlen) == -1);
    }
    {   /* indefinite length (a BER-only construct) */
        static const uint8_t b[] = { 0x30, 0x80, 0x00, 0x00 };
        der_init(&r, b, sizeof b);
        check_true("der: rejects indefinite length",
                   der_read_tlv(&r, &tag, &val, &vlen) == -1);
    }
    {   /* non-minimal: length 5 in long form when short form fits */
        static const uint8_t b[] = { 0x02, 0x81, 0x05, 0, 0, 0, 0, 0 };
        der_init(&r, b, sizeof b);
        check_true("der: rejects non-minimal long-form length",
                   der_read_tlv(&r, &tag, &val, &vlen) == -1);
    }
    {   /* non-minimal: leading zero octet in the length */
        static const uint8_t b[] = { 0x02, 0x82, 0x00, 0x80, 0 };
        der_init(&r, b, sizeof b);
        check_true("der: rejects leading-zero length octet",
                   der_read_tlv(&r, &tag, &val, &vlen) == -1);
    }
    {   /* high-tag-number identifier (low 5 bits all set) */
        static const uint8_t b[] = { 0x1f, 0x20, 0x01, 0x00 };
        der_init(&r, b, sizeof b);
        check_true("der: rejects multi-byte tag",
                   der_read_tlv(&r, &tag, &val, &vlen) == -1);
    }
    {   /* length field wider than a size_t can hold (0x89 => 9 octets) */
        static const uint8_t b[] = { 0x02, 0x89, 1, 2, 3, 4, 5, 6, 7, 8, 9 };
        der_init(&r, b, sizeof b);
        check_true("der: rejects oversized length field",
                   der_read_tlv(&r, &tag, &val, &vlen) == -1);
    }
}

/* der_expect / der_enter must enforce the requested tag. */
static void test_der_tag_enforcement(void) {
    static const uint8_t in[] = { 0x02, 0x01, 0x05 };   /* INTEGER 5 */
    der_reader_t r, child;
    const uint8_t *val;
    size_t vlen;

    der_init(&r, in, sizeof in);
    check_true("der: der_expect rejects wrong tag",
               der_expect(&r, DER_TAG_OCTET_STRING, &val, &vlen) == -1);

    der_init(&r, in, sizeof in);
    check_true("der: der_enter rejects wrong tag",
               der_enter(&r, DER_TAG_SEQUENCE, &child) == -1);
}

/* PEM decoding: extract the DER payload of a BEGIN/END block. The vector is a
 * SEQUENCE{INTEGER 5, OCTET STRING "PEM"} = 30 08 02 01 05 04 03 50 45 4d,
 * Base64 "MAgCAQUEA1BFTQ==" (cross-checked with an independent encoder). */
static void test_pem_decode(void) {
    static const char lf_pem[] =
        "-----BEGIN CERTIFICATE-----\n"
        "MAgCAQUEA1BFTQ==\n"
        "-----END CERTIFICATE-----\n";
    static const char crlf_pem[] =
        "-----BEGIN CERTIFICATE-----\r\n"
        "MAgCAQUEA1BFTQ==\r\n"
        "-----END CERTIFICATE-----\r\n";
    static const unsigned char expect[] = {
        0x30, 0x08, 0x02, 0x01, 0x05, 0x04, 0x03, 0x50, 0x45, 0x4d
    };
    unsigned char out[64];
    size_t out_len = 0;

    check_true("pem: decodes a CERTIFICATE block (LF)",
               pem_decode("CERTIFICATE", lf_pem, strlen(lf_pem), out, sizeof out, &out_len) == 0
               && out_len == sizeof expect && memcmp(out, expect, sizeof expect) == 0);

    out_len = 0;
    check_true("pem: decodes a CERTIFICATE block (CRLF)",
               pem_decode("CERTIFICATE", crlf_pem, strlen(crlf_pem), out, sizeof out, &out_len) == 0
               && out_len == sizeof expect && memcmp(out, expect, sizeof expect) == 0);

    {   /* Unpadded body (Base64 length is a multiple of 4 with no '='): decoding
         * must stop at the END marker, not rely on a padding byte. */
        static const char unpadded[] =
            "-----BEGIN PRIVATE KEY-----\n"
            "MAcEBWFiY2Rl\n"
            "-----END PRIVATE KEY-----\n";
        static const unsigned char want[] = {
            0x30, 0x07, 0x04, 0x05, 0x61, 0x62, 0x63, 0x64, 0x65
        };
        out_len = 0;
        check_true("pem: decodes an unpadded PRIVATE KEY block",
                   pem_decode("PRIVATE KEY", unpadded, strlen(unpadded), out, sizeof out, &out_len) == 0
                   && out_len == sizeof want && memcmp(out, want, sizeof want) == 0);
    }

    check_true("pem: mismatched label rejected",
               pem_decode("PRIVATE KEY", lf_pem, strlen(lf_pem), out, sizeof out, &out_len) == -1);

    {   /* no END marker */
        static const char s[] = "-----BEGIN CERTIFICATE-----\nMAgCAQUEA1BFTQ==\n";
        check_true("pem: missing END marker rejected",
                   pem_decode("CERTIFICATE", s, strlen(s), out, sizeof out, &out_len) == -1);
    }
    {   /* no BEGIN marker */
        static const char s[] = "MAgCAQUEA1BFTQ==\n-----END CERTIFICATE-----\n";
        check_true("pem: missing BEGIN marker rejected",
                   pem_decode("CERTIFICATE", s, strlen(s), out, sizeof out, &out_len) == -1);
    }
    {   /* empty body between markers */
        static const char s[] = "-----BEGIN CERTIFICATE-----\n-----END CERTIFICATE-----\n";
        check_true("pem: empty body rejected",
                   pem_decode("CERTIFICATE", s, strlen(s), out, sizeof out, &out_len) == -1);
    }
    {   /* non-whitespace data after the Base64 padding must be rejected, not
         * silently truncated at the first '=' */
        static const char s[] =
            "-----BEGIN CERTIFICATE-----\n"
            "MAgCAQUEA1BFTQ==GARBAGE\n"
            "-----END CERTIFICATE-----\n";
        check_true("pem: trailing data after padding rejected",
                   pem_decode("CERTIFICATE", s, strlen(s), out, sizeof out, &out_len) == -1);
    }
    check_true("pem: garbage input rejected",
               pem_decode("CERTIFICATE", "not a pem file at all", 21, out, sizeof out, &out_len) == -1);

    check_true("pem: too-small output rejected",
               pem_decode("CERTIFICATE", lf_pem, strlen(lf_pem), out, 4, &out_len) == -1);

    {   /* label longer than PEM_MAX_LABEL */
        char big[64];
        memset(big, 'A', sizeof big);
        big[50] = '\0';
        check_true("pem: over-long label rejected",
                   pem_decode(big, lf_pem, strlen(lf_pem), out, sizeof out, &out_len) == -1);
    }
    check_true("pem: NULL arguments rejected",
               pem_decode(NULL, lf_pem, strlen(lf_pem), out, sizeof out, &out_len) == -1
               && pem_decode("CERTIFICATE", NULL, 10, out, sizeof out, &out_len) == -1);
}

/* Ed25519 key parsing: PKCS#8 private key + SubjectPublicKeyInfo public key.
 * The vectors are the RFC 8032 TEST2 keypair wrapped in the standard DER
 * templates, cross-checked with OpenSSL (which parses them as valid Ed25519 keys
 * and derives the same public key from the private seed). */
static void test_ed25519_key(void) {
    static const uint8_t pkcs8[] = {
        0x30, 0x2e, 0x02, 0x01, 0x00, 0x30, 0x05, 0x06, 0x03, 0x2b, 0x65, 0x70,
        0x04, 0x22, 0x04, 0x20, 0x4c, 0xcd, 0x08, 0x9b, 0x28, 0xff, 0x96, 0xda,
        0x9d, 0xb6, 0xc3, 0x46, 0xec, 0x11, 0x4e, 0x0f, 0x5b, 0x8a, 0x31, 0x9f,
        0x35, 0xab, 0xa6, 0x24, 0xda, 0x8c, 0xf6, 0xed, 0x4f, 0xb8, 0xa6, 0xfb
    };
    static const uint8_t spki[] = {
        0x30, 0x2a, 0x30, 0x05, 0x06, 0x03, 0x2b, 0x65, 0x70, 0x03, 0x21, 0x00,
        0x3d, 0x40, 0x17, 0xc3, 0xe8, 0x43, 0x89, 0x5a, 0x92, 0xb7, 0x0a, 0xa7,
        0x4d, 0x1b, 0x7e, 0xbc, 0x9c, 0x98, 0x2c, 0xcf, 0x2e, 0xc4, 0x96, 0x8c,
        0xc0, 0xcd, 0x55, 0xf1, 0x2a, 0xf4, 0x66, 0x0c
    };
    static const uint8_t seed[32] = {
        0x4c, 0xcd, 0x08, 0x9b, 0x28, 0xff, 0x96, 0xda, 0x9d, 0xb6, 0xc3, 0x46,
        0xec, 0x11, 0x4e, 0x0f, 0x5b, 0x8a, 0x31, 0x9f, 0x35, 0xab, 0xa6, 0x24,
        0xda, 0x8c, 0xf6, 0xed, 0x4f, 0xb8, 0xa6, 0xfb
    };
    static const uint8_t pub[32] = {
        0x3d, 0x40, 0x17, 0xc3, 0xe8, 0x43, 0x89, 0x5a, 0x92, 0xb7, 0x0a, 0xa7,
        0x4d, 0x1b, 0x7e, 0xbc, 0x9c, 0x98, 0x2c, 0xcf, 0x2e, 0xc4, 0x96, 0x8c,
        0xc0, 0xcd, 0x55, 0xf1, 0x2a, 0xf4, 0x66, 0x0c
    };
    static const uint8_t spki_bad_oid[] = {   /* OID last byte 0x70 -> 0x71 */
        0x30, 0x2a, 0x30, 0x05, 0x06, 0x03, 0x2b, 0x65, 0x71, 0x03, 0x21, 0x00,
        0x3d, 0x40, 0x17, 0xc3, 0xe8, 0x43, 0x89, 0x5a, 0x92, 0xb7, 0x0a, 0xa7,
        0x4d, 0x1b, 0x7e, 0xbc, 0x9c, 0x98, 0x2c, 0xcf, 0x2e, 0xc4, 0x96, 0x8c,
        0xc0, 0xcd, 0x55, 0xf1, 0x2a, 0xf4, 0x66, 0x0c
    };
    static const uint8_t garbage[] = { 0x30, 0x03, 0x99, 0x88, 0x77 };
    uint8_t got_seed[32], got_pub[32], derived[32];

    check_true("ed25519_key: PKCS#8 -> seed",
               ed25519_parse_pkcs8(pkcs8, sizeof pkcs8, got_seed) == 0
               && memcmp(got_seed, seed, 32) == 0);

    /* End to end: the extracted seed must derive the expected public key. */
    ed25519_public_key(derived, got_seed);
    check_true("ed25519_key: PKCS#8 seed derives the expected public key",
               memcmp(derived, pub, 32) == 0);

    check_true("ed25519_key: SPKI -> public key",
               ed25519_parse_spki(spki, sizeof spki, got_pub) == 0
               && memcmp(got_pub, pub, 32) == 0);

    check_true("ed25519_key: PKCS#8-derived key matches SPKI key",
               memcmp(derived, got_pub, 32) == 0);

    /* Rejections. */
    check_true("ed25519_key: SPKI wrong OID rejected",
               ed25519_parse_spki(spki_bad_oid, sizeof spki_bad_oid, got_pub) == -1);
    check_true("ed25519_key: truncated PKCS#8 rejected",
               ed25519_parse_pkcs8(pkcs8, sizeof pkcs8 - 5, got_seed) == -1);
    check_true("ed25519_key: truncated SPKI rejected",
               ed25519_parse_spki(spki, sizeof spki - 3, got_pub) == -1);
    check_true("ed25519_key: garbage PKCS#8 rejected",
               ed25519_parse_pkcs8(garbage, sizeof garbage, got_seed) == -1);
    check_true("ed25519_key: NULL arguments rejected",
               ed25519_parse_pkcs8(NULL, 10, got_seed) == -1
               && ed25519_parse_spki(spki, sizeof spki, NULL) == -1);

    /* End-to-end server key-loading flow: a PEM "PRIVATE KEY" file decodes to
     * DER (pem_decode) which parses to the same seed (ed25519_parse_pkcs8). The
     * PEM was verified with OpenSSL. */
    {
        static const char key_pem[] =
            "-----BEGIN PRIVATE KEY-----\n"
            "MC4CAQAwBQYDK2VwBCIEIEzNCJso/5banbbDRuwRTg9bijGfNaumJNqM9u1PuKb7\n"
            "-----END PRIVATE KEY-----\n";
        unsigned char der[128];
        size_t der_len = 0;
        check_true("ed25519_key: PEM PRIVATE KEY -> DER -> seed (end to end)",
                   pem_decode("PRIVATE KEY", key_pem, strlen(key_pem), der, sizeof der, &der_len) == 0
                   && ed25519_parse_pkcs8(der, der_len, got_seed) == 0
                   && memcmp(got_seed, seed, 32) == 0);
    }
}
#endif /* WEBLIB_TLS */

int main(void) {
#ifndef WEBLIB_TLS
    printf("FAIL: test_tls_parse built without WEBLIB_TLS defined\n");
    return 1;
#else
    test_der_valid_sequence();
    test_der_long_form_length();
    test_der_rejects_malformed();
    test_der_tag_enforcement();
    test_pem_decode();
    test_ed25519_key();

    if (g_failures == 0) {
        printf("All TLS parse tests passed.\n");
    }
    return g_failures == 0 ? 0 : 1;
#endif
}
