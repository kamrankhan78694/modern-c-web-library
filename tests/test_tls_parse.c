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
#include "wire.h"
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
    static const uint8_t spki_alg_params[] = {   /* AlgId carries a NULL param */
        0x30, 0x2c, 0x30, 0x07, 0x06, 0x03, 0x2b, 0x65, 0x70, 0x05, 0x00, 0x03,
        0x21, 0x00, 0x3d, 0x40, 0x17, 0xc3, 0xe8, 0x43, 0x89, 0x5a, 0x92, 0xb7,
        0x0a, 0xa7, 0x4d, 0x1b, 0x7e, 0xbc, 0x9c, 0x98, 0x2c, 0xcf, 0x2e, 0xc4,
        0x96, 0x8c, 0xc0, 0xcd, 0x55, 0xf1, 0x2a, 0xf4, 0x66, 0x0c
    };
    uint8_t got_seed[32], got_pub[32], derived[32];
    int pk_ok, sp_ok;

    /* Parse both keys once, then gate the cross-derivation checks on success so
     * a parse failure surfaces as one clear failure, not cascading ones. */
    pk_ok = (ed25519_parse_pkcs8(pkcs8, sizeof pkcs8, got_seed) == 0);
    sp_ok = (ed25519_parse_spki(spki, sizeof spki, got_pub) == 0);

    check_true("ed25519_key: PKCS#8 -> seed",
               pk_ok && memcmp(got_seed, seed, 32) == 0);
    check_true("ed25519_key: SPKI -> public key",
               sp_ok && memcmp(got_pub, pub, 32) == 0);

    if (pk_ok) {
        /* End to end: the extracted seed must derive the expected public key. */
        ed25519_public_key(derived, got_seed);
        check_true("ed25519_key: PKCS#8 seed derives the expected public key",
                   memcmp(derived, pub, 32) == 0);
        if (sp_ok) {
            check_true("ed25519_key: PKCS#8-derived key matches SPKI key",
                       memcmp(derived, got_pub, 32) == 0);
        }
    }

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

    /* Strict structure: reject an AlgorithmIdentifier with parameters (RFC 8410
     * requires them absent) and any trailing data after the key structure. */
    check_true("ed25519_key: SPKI with AlgId parameters rejected",
               ed25519_parse_spki(spki_alg_params, sizeof spki_alg_params, got_pub) == -1);
    {
        uint8_t buf[sizeof spki + 1];
        memcpy(buf, spki, sizeof spki);
        buf[sizeof spki] = 0x2a;
        check_true("ed25519_key: SPKI with trailing data rejected",
                   ed25519_parse_spki(buf, sizeof buf, got_pub) == -1);
    }
    {
        uint8_t buf[sizeof pkcs8 + 1];
        memcpy(buf, pkcs8, sizeof pkcs8);
        buf[sizeof pkcs8] = 0x2a;
        check_true("ed25519_key: PKCS#8 with trailing data rejected",
                   ed25519_parse_pkcs8(buf, sizeof buf, got_seed) == -1);
    }

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

/* Bounded TLS wire codec: big-endian integers + length-prefixed vectors. The
 * writer's output is constructed to equal the reader's input, so the two are
 * cross-checked against each other, and malformed reads / writer overflow are
 * rejected. */
static void test_wire(void) {
    /* This byte string is both the reader's input and the writer's expected
     * output: u8 0x2a | u16 0x0103 | u24 0x000200 | u16-vector{aa,bb,cc} | u8 0xff. */
    static const uint8_t framed[] = {
        0x2a, 0x01, 0x03, 0x00, 0x02, 0x00, 0x00, 0x03, 0xaa, 0xbb, 0xcc, 0xff
    };

    /* --- reader: read every field back --- */
    {
        tls_reader r, body;
        uint8_t u8 = 0;
        uint16_t u16 = 0;
        uint32_t u24 = 0;
        const uint8_t *p = NULL;
        tls_reader_init(&r, framed, sizeof framed);
        check_true("wire read u8", tls_read_u8(&r, &u8) && u8 == 0x2a);
        check_true("wire read u16", tls_read_u16(&r, &u16) && u16 == 0x0103);
        check_true("wire read u24", tls_read_u24(&r, &u24) && u24 == 0x000200u);
        check_true("wire read u16 vector",
                   tls_read_vector(&r, 2, &body) && tls_reader_remaining(&body) == 3
                   && tls_read_bytes(&body, &p, 3) && p[0] == 0xaa && p[2] == 0xcc
                   && tls_reader_eof(&body));
        check_true("wire read trailing u8", tls_read_u8(&r, &u8) && u8 == 0xff);
        check_true("wire reader reaches eof", tls_reader_eof(&r));
        check_true("wire read past eof fails", tls_read_u8(&r, &u8) == 0);
    }

    /* --- reader rejections --- */
    {
        static const uint8_t trunc[] = { 0x01 };            /* u16 needs 2 bytes */
        tls_reader r, body;
        uint16_t u16 = 0;
        tls_reader_init(&r, trunc, sizeof trunc);
        check_true("wire read u16 truncated fails", tls_read_u16(&r, &u16) == 0);
        (void)body;
    }
    {
        static const uint8_t badvec[] = { 0x00, 0x05, 0xaa };  /* vec len 5, 1 byte body */
        tls_reader r, body;
        tls_reader_init(&r, badvec, sizeof badvec);
        check_true("wire vector overrun fails", tls_read_vector(&r, 2, &body) == 0);
        tls_reader_init(&r, badvec, sizeof badvec);
        check_true("wire vector bad len_bytes fails", tls_read_vector(&r, 4, &body) == 0);
    }

    /* --- writer: build the same framing and compare to `framed` --- */
    {
        uint8_t buf[64];
        tls_writer w;
        size_t out_len = 0, marker;
        static const uint8_t vbody[] = { 0xaa, 0xbb, 0xcc };
        tls_writer_init(&w, buf, sizeof buf);
        tls_write_u8(&w, 0x2a);
        tls_write_u16(&w, 0x0103);
        tls_write_u24(&w, 0x000200u);
        marker = tls_writer_open_vector(&w, 2);
        tls_write_bytes(&w, vbody, sizeof vbody);
        tls_writer_close_vector(&w, marker, 2);
        tls_write_u8(&w, 0xff);
        check_true("wire writer output matches reader input",
                   tls_writer_finish(&w, &out_len) == 1
                   && out_len == sizeof framed
                   && memcmp(buf, framed, sizeof framed) == 0);
    }

    /* --- writer overflow + u24 range --- */
    {
        uint8_t buf[3];
        tls_writer w;
        tls_writer_init(&w, buf, sizeof buf);
        tls_write_u16(&w, 0x1234);      /* 2 of 3 bytes */
        tls_write_u16(&w, 0x5678);      /* needs 2 more, only 1 left -> overflow */
        check_true("wire writer overflow clears ok", tls_writer_finish(&w, NULL) == 0);

        tls_writer_init(&w, buf, sizeof buf);
        check_true("wire write u24 over 0xFFFFFF fails", tls_write_u24(&w, 0x1000000u) == 0);
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
    test_wire();

    if (g_failures == 0) {
        printf("All TLS parse tests passed.\n");
    }
    return g_failures == 0 ? 0 : 1;
#endif
}
