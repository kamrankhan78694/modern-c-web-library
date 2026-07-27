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
#include "handshake.h"
#include "record.h"
#include "server_handshake.h"
#include "tls_khannection.h"
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
        tls_reader_t r, body;
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
        tls_reader_t r, body;
        uint16_t u16 = 0;
        tls_reader_init(&r, trunc, sizeof trunc);
        check_true("wire read u16 truncated fails", tls_read_u16(&r, &u16) == 0);
        (void)body;
    }
    {
        static const uint8_t badvec[] = { 0x00, 0x05, 0xaa };  /* vec len 5, 1 byte body */
        tls_reader_t r, body;
        tls_reader_init(&r, badvec, sizeof badvec);
        check_true("wire vector overrun fails", tls_read_vector(&r, 2, &body) == 0);
        tls_reader_init(&r, badvec, sizeof badvec);
        check_true("wire vector bad len_bytes fails", tls_read_vector(&r, 4, &body) == 0);
    }

    /* --- writer: build the same framing and compare to `framed` --- */
    {
        uint8_t buf[64];
        tls_writer_t w;
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
        tls_writer_t w;
        tls_writer_init(&w, buf, sizeof buf);
        tls_write_u16(&w, 0x1234);      /* 2 of 3 bytes */
        tls_write_u16(&w, 0x5678);      /* needs 2 more, only 1 left -> overflow */
        check_true("wire writer overflow clears ok", tls_writer_finish(&w, NULL) == 0);

        tls_writer_init(&w, buf, sizeof buf);
        check_true("wire write u24 over 0xFFFFFF fails", tls_write_u24(&w, 0x1000000u) == 0);
    }

    /* --- invalid vector widths fail fast --- */
    {
        uint8_t buf[16];
        tls_writer_t w;
        size_t m;
        tls_writer_init(&w, buf, sizeof buf);
        (void)tls_writer_open_vector(&w, 0);            /* width must be 1..3 */
        check_true("wire open_vector invalid width clears ok",
                   tls_writer_finish(&w, NULL) == 0);

        tls_writer_init(&w, buf, sizeof buf);
        m = tls_writer_open_vector(&w, 2);
        tls_write_u8(&w, 0xaa);
        tls_writer_close_vector(&w, m, 4);              /* invalid close width */
        check_true("wire close_vector invalid width clears ok",
                   tls_writer_finish(&w, NULL) == 0);
    }
}

/* ClientHello parsing against a real OpenSSL TLS 1.3 ClientHello handshake
 * message (captured from `openssl s_client -tls1_3 -ciphersuites
 * TLS_CHACHA20_POLY1305_SHA256 -groups X25519 -sigalgs ed25519 -servername
 * example.com`). The expected field values were cross-checked with an
 * independent Python parser. */
static void test_client_hello(void) {
    static const uint8_t ch[] = {
        0x01, 0x00, 0x00, 0xb8, 0x03, 0x03, 0xdf, 0x9c, 0xd1, 0x6c, 0x49, 0xbe,
        0x7b, 0xcf, 0x8f, 0x45, 0x32, 0x1e, 0x93, 0xad, 0xee, 0xc5, 0xe4, 0x55,
        0x5c, 0xc1, 0xd4, 0x16, 0x82, 0xcc, 0xf5, 0xfc, 0x51, 0x7f, 0xe4, 0x8e,
        0x5c, 0x73, 0x20, 0x96, 0xa6, 0xdc, 0xca, 0xa0, 0x02, 0x90, 0x2c, 0xc4,
        0x6a, 0x51, 0xdd, 0x0d, 0x03, 0x06, 0x00, 0x73, 0xa2, 0x20, 0xb2, 0x99,
        0x9d, 0x04, 0x12, 0x73, 0x82, 0x69, 0x38, 0x59, 0xcc, 0x4b, 0x8f, 0x00,
        0x02, 0x13, 0x03, 0x01, 0x00, 0x00, 0x6d, 0x00, 0x00, 0x00, 0x10, 0x00,
        0x0e, 0x00, 0x00, 0x0b, 0x65, 0x78, 0x61, 0x6d, 0x70, 0x6c, 0x65, 0x2e,
        0x63, 0x6f, 0x6d, 0x00, 0x0b, 0x00, 0x02, 0x01, 0x00, 0x00, 0x0a, 0x00,
        0x04, 0x00, 0x02, 0x00, 0x1d, 0x00, 0x23, 0x00, 0x00, 0x00, 0x16, 0x00,
        0x00, 0x00, 0x17, 0x00, 0x00, 0x00, 0x0d, 0x00, 0x04, 0x00, 0x02, 0x08,
        0x07, 0x00, 0x2b, 0x00, 0x03, 0x02, 0x03, 0x04, 0x00, 0x2d, 0x00, 0x02,
        0x01, 0x01, 0x00, 0x33, 0x00, 0x26, 0x00, 0x24, 0x00, 0x1d, 0x00, 0x20,
        0x4a, 0x42, 0x55, 0xf2, 0xac, 0xb5, 0x7b, 0x94, 0xfb, 0xa0, 0xa4, 0x99,
        0xa0, 0x2f, 0x7e, 0x09, 0x07, 0x3b, 0x0e, 0x34, 0x6b, 0x16, 0x76, 0x13,
        0xca, 0xc8, 0xd7, 0x42, 0x47, 0xcf, 0x39, 0x19
    };
    static const uint8_t expect_ks[32] = {
        0x4a, 0x42, 0x55, 0xf2, 0xac, 0xb5, 0x7b, 0x94, 0xfb, 0xa0, 0xa4, 0x99,
        0xa0, 0x2f, 0x7e, 0x09, 0x07, 0x3b, 0x0e, 0x34, 0x6b, 0x16, 0x76, 0x13,
        0xca, 0xc8, 0xd7, 0x42, 0x47, 0xcf, 0x39, 0x19
    };
    tls_client_hello_t hello;

    check_true("clienthello: parses a real OpenSSL ClientHello",
               tls_parse_client_hello(ch, sizeof ch, &hello) == 1);
    check_true("clienthello: offers TLS1.3 + x25519 + chacha20 + ed25519",
               hello.offers_tls13 && hello.offers_x25519
               && hello.offers_chacha20_poly1305 && hello.offers_ed25519);
    check_true("clienthello: extracts the X25519 key share",
               hello.x25519_key_share != NULL
               && memcmp(hello.x25519_key_share, expect_ks, 32) == 0);
    check_true("clienthello: extracts SNI host_name",
               hello.server_name != NULL && hello.server_name_len == 11
               && memcmp(hello.server_name, "example.com", 11) == 0);
    check_true("clienthello: session_id present (32 bytes)",
               hello.session_id != NULL && hello.session_id_len == 32);

    /* Rejections. */
    check_true("clienthello: rejects wrong handshake type",
               tls_parse_client_hello((const uint8_t *)"\x02\x00\x00\x00", 4, &hello) == 0);
    check_true("clienthello: rejects truncated message",
               tls_parse_client_hello(ch, 50, &hello) == 0);
    {
        static const uint8_t bad[16] = { 0x01, 0x00, 0x00, 0xff, 0x03, 0x03 };
        check_true("clienthello: rejects body-length mismatch",
                   tls_parse_client_hello(bad, sizeof bad, &hello) == 0);
    }
    {
        uint8_t withjunk[sizeof ch + 1];
        memcpy(withjunk, ch, sizeof ch);
        withjunk[sizeof ch] = 0xff;
        check_true("clienthello: rejects trailing data",
                   tls_parse_client_hello(withjunk, sizeof withjunk, &hello) == 0);
    }
    check_true("clienthello: rejects NULL args",
               tls_parse_client_hello(NULL, 10, &hello) == 0
               && tls_parse_client_hello(ch, sizeof ch, NULL) == 0);

    /* Malformed *known* extensions must be rejected, not silently tolerated
     * (structural strictness). These vectors were generated with correct outer
     * framing around a single bad extension. */
    {
        static const uint8_t odd_versions[] = {   /* supported_versions: odd list */
            0x01, 0x00, 0x00, 0x33, 0x03, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x02, 0x13, 0x03, 0x01, 0x00, 0x00, 0x08, 0x00,
            0x2b, 0x00, 0x04, 0x03, 0x03, 0x04, 0x03
        };
        check_true("clienthello: rejects odd-length supported_versions",
                   tls_parse_client_hello(odd_versions, sizeof odd_versions, &hello) == 0);
    }
    {
        static const uint8_t ks_trailing[] = {   /* key_share: 1 trailing byte */
            0x01, 0x00, 0x00, 0x56, 0x03, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x02, 0x13, 0x03, 0x01, 0x00, 0x00, 0x2b, 0x00,
            0x33, 0x00, 0x27, 0x00, 0x25, 0x00, 0x1d, 0x00, 0x20, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0xff
        };
        check_true("clienthello: rejects key_share with trailing bytes",
                   tls_parse_client_hello(ks_trailing, sizeof ks_trailing, &hello) == 0);
    }
    {
        static const uint8_t sni_trunc[] = {   /* server_name: entry truncated */
            0x01, 0x00, 0x00, 0x32, 0x03, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x02, 0x13, 0x03, 0x01, 0x00, 0x00, 0x07, 0x00,
            0x00, 0x00, 0x03, 0x00, 0x01, 0x00
        };
        check_true("clienthello: rejects truncated server_name entry",
                   tls_parse_client_hello(sni_trunc, sizeof sni_trunc, &hello) == 0);
    }
    {
        static const uint8_t grp_trailing[] = {   /* supported_groups: ext trailing */
            0x01, 0x00, 0x00, 0x34, 0x03, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x02, 0x13, 0x03, 0x01, 0x00, 0x00, 0x09, 0x00,
            0x0a, 0x00, 0x05, 0x00, 0x02, 0x00, 0x1d, 0xff
        };
        check_true("clienthello: rejects trailing bytes in a known extension",
                   tls_parse_client_hello(grp_trailing, sizeof grp_trailing, &hello) == 0);
    }
}

/* Server handshake message builders. Each is built with fixed inputs and its
 * wire bytes compared to an independent Python builder (hs_build_ref.py). */
static void test_hs_build(void) {
    static const uint8_t expect_sh[] = {
        0x02, 0x00, 0x00, 0x76, 0x03, 0x03, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06,
        0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11, 0x12,
        0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e,
        0x1f, 0x20, 0x20, 0xa0, 0xa1, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7, 0xa8,
        0xa9, 0xaa, 0xab, 0xac, 0xad, 0xae, 0xaf, 0xb0, 0xb1, 0xb2, 0xb3, 0xb4,
        0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xbb, 0xbc, 0xbd, 0xbe, 0xbf, 0x13,
        0x03, 0x00, 0x00, 0x2e, 0x00, 0x2b, 0x00, 0x02, 0x03, 0x04, 0x00, 0x33,
        0x00, 0x24, 0x00, 0x1d, 0x00, 0x20, 0x40, 0x41, 0x42, 0x43, 0x44, 0x45,
        0x46, 0x47, 0x48, 0x49, 0x4a, 0x4b, 0x4c, 0x4d, 0x4e, 0x4f, 0x50, 0x51,
        0x52, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5a, 0x5b, 0x5c, 0x5d,
        0x5e, 0x5f
    };
    static const uint8_t expect_ee[] = { 0x08, 0x00, 0x00, 0x02, 0x00, 0x00 };
    static const uint8_t expect_cert[] = {
        0x0b, 0x00, 0x00, 0x0e, 0x00, 0x00, 0x00, 0x0a, 0x00, 0x00, 0x05, 0x30,
        0x03, 0x02, 0x01, 0x2a, 0x00, 0x00
    };
    static const uint8_t expect_cv[] = {
        0x0f, 0x00, 0x00, 0x44, 0x08, 0x07, 0x00, 0x40, 0x60, 0x61, 0x62, 0x63,
        0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6a, 0x6b, 0x6c, 0x6d, 0x6e, 0x6f,
        0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7a, 0x7b,
        0x7c, 0x7d, 0x7e, 0x7f, 0x80, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87,
        0x88, 0x89, 0x8a, 0x8b, 0x8c, 0x8d, 0x8e, 0x8f, 0x90, 0x91, 0x92, 0x93,
        0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9a, 0x9b, 0x9c, 0x9d, 0x9e, 0x9f
    };
    static const uint8_t expect_fin[] = {
        0x14, 0x00, 0x00, 0x20, 0xc0, 0xc1, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7,
        0xc8, 0xc9, 0xca, 0xcb, 0xcc, 0xcd, 0xce, 0xcf, 0xd0, 0xd1, 0xd2, 0xd3,
        0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda, 0xdb, 0xdc, 0xdd, 0xde, 0xdf
    };
    static const uint8_t cert[] = { 0x30, 0x03, 0x02, 0x01, 0x2a };
    uint8_t random[32], sid[32], pub[32], sig[64], verify[32], buf[256];
    tls_writer_t w;
    size_t out_len = 0, i;

    for (i = 0; i < 32; i++) {
        random[i] = (uint8_t)(0x01 + i);
        sid[i]    = (uint8_t)(0xa0 + i);
        pub[i]    = (uint8_t)(0x40 + i);
        verify[i] = (uint8_t)(0xc0 + i);
    }
    for (i = 0; i < 64; i++) {
        sig[i] = (uint8_t)(0x60 + i);
    }

    tls_writer_init(&w, buf, sizeof buf);
    check_true("hs build: ServerHello wire (independent ref)",
               tls_build_server_hello(&w, random, sid, 32, pub) == 1
               && tls_writer_finish(&w, &out_len) == 1
               && out_len == sizeof expect_sh && memcmp(buf, expect_sh, out_len) == 0);

    tls_writer_init(&w, buf, sizeof buf);
    check_true("hs build: EncryptedExtensions wire",
               tls_build_encrypted_extensions(&w) == 1
               && tls_writer_finish(&w, &out_len) == 1
               && out_len == sizeof expect_ee && memcmp(buf, expect_ee, out_len) == 0);

    tls_writer_init(&w, buf, sizeof buf);
    check_true("hs build: Certificate wire",
               tls_build_certificate(&w, cert, sizeof cert) == 1
               && tls_writer_finish(&w, &out_len) == 1
               && out_len == sizeof expect_cert && memcmp(buf, expect_cert, out_len) == 0);

    tls_writer_init(&w, buf, sizeof buf);
    check_true("hs build: CertificateVerify wire",
               tls_build_certificate_verify(&w, sig) == 1
               && tls_writer_finish(&w, &out_len) == 1
               && out_len == sizeof expect_cv && memcmp(buf, expect_cv, out_len) == 0);

    tls_writer_init(&w, buf, sizeof buf);
    check_true("hs build: Finished wire",
               tls_build_finished(&w, verify) == 1
               && tls_writer_finish(&w, &out_len) == 1
               && out_len == sizeof expect_fin && memcmp(buf, expect_fin, out_len) == 0);

    /* A too-small buffer makes the builder fail (sticky overflow). */
    {
        uint8_t small[8];
        tls_writer_init(&w, small, sizeof small);
        check_true("hs build: ServerHello into small buffer fails",
                   tls_build_server_hello(&w, random, sid, 32, pub) == 0);
    }
    /* session_id longer than 32 is rejected. */
    tls_writer_init(&w, buf, sizeof buf);
    check_true("hs build: over-long session_id rejected",
               tls_build_server_hello(&w, random, sid, 33, pub) == 0);
}

/*
 * Server handshake state machine. The known-answer vectors below (KAT_*) are
 * produced by an independent Python (`cryptography`) TLS-1.3 client oracle from a
 * set of fixed inputs — see the commit for scratchpad/server_hs_oracle.py. That
 * oracle re-derives the whole key schedule (pure-Python HKDF + the RFC labels) and
 * flight, and separately opens and verifies this C server's actual output. Pinning
 * its results here means the assertions below are checked against a *different*
 * implementation, not merely against ourselves.
 */
static const uint8_t KAT_CH[144] = {
    0x01, 0x00, 0x00, 0x8c, 0x03, 0x03, 0xc1, 0xc1, 0xc1, 0xc1, 0xc1, 0xc1,
    0xc1, 0xc1, 0xc1, 0xc1, 0xc1, 0xc1, 0xc1, 0xc1, 0xc1, 0xc1, 0xc1, 0xc1,
    0xc1, 0xc1, 0xc1, 0xc1, 0xc1, 0xc1, 0xc1, 0xc1, 0xc1, 0xc1, 0xc1, 0xc1,
    0xc1, 0xc1, 0x20, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
    0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11, 0x12, 0x13, 0x14,
    0x15, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f, 0x00,
    0x02, 0x13, 0x03, 0x01, 0x00, 0x00, 0x41, 0x00, 0x2b, 0x00, 0x03, 0x02,
    0x03, 0x04, 0x00, 0x0a, 0x00, 0x04, 0x00, 0x02, 0x00, 0x1d, 0x00, 0x0d,
    0x00, 0x04, 0x00, 0x02, 0x08, 0x07, 0x00, 0x33, 0x00, 0x26, 0x00, 0x24,
    0x00, 0x1d, 0x00, 0x20, 0x79, 0xa6, 0x31, 0xee, 0xde, 0x1b, 0xf9, 0xc9,
    0x8f, 0x12, 0x03, 0x2c, 0xde, 0xad, 0xd0, 0xe7, 0xa0, 0x79, 0x39, 0x8f,
    0xc7, 0x86, 0xb8, 0x8c, 0xc8, 0x46, 0xec, 0x89, 0xaf, 0x85, 0xa5, 0x1a,
};
static const uint8_t KAT_CERT[96] = {
    0xa0, 0xa1, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7, 0xa8, 0xa9, 0xaa, 0xab,
    0xac, 0xad, 0xae, 0xaf, 0xb0, 0xb1, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7,
    0xb8, 0xb9, 0xba, 0xbb, 0xbc, 0xbd, 0xbe, 0xbf, 0xa0, 0xa1, 0xa2, 0xa3,
    0xa4, 0xa5, 0xa6, 0xa7, 0xa8, 0xa9, 0xaa, 0xab, 0xac, 0xad, 0xae, 0xaf,
    0xb0, 0xb1, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xbb,
    0xbc, 0xbd, 0xbe, 0xbf, 0xa0, 0xa1, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7,
    0xa8, 0xa9, 0xaa, 0xab, 0xac, 0xad, 0xae, 0xaf, 0xb0, 0xb1, 0xb2, 0xb3,
    0xb4, 0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xbb, 0xbc, 0xbd, 0xbe, 0xbf,
};
static const uint8_t KAT_ED_SEED[32] = {
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c,
    0x0d, 0x0e, 0x0f, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18,
    0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f, 0x20,
};
static const uint8_t KAT_ED_PUB[32] = {
    0x79, 0xb5, 0x56, 0x2e, 0x8f, 0xe6, 0x54, 0xf9, 0x40, 0x78, 0xb1, 0x12,
    0xe8, 0xa9, 0x8b, 0xa7, 0x90, 0x1f, 0x85, 0x3a, 0xe6, 0x95, 0xbe, 0xd7,
    0xe0, 0xe3, 0x91, 0x0b, 0xad, 0x04, 0x96, 0x64,
};
static const uint8_t KAT_SERVER_EPH[32] = {
    0x60, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6a, 0x6b,
    0x6c, 0x6d, 0x6e, 0x6f, 0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77,
    0x78, 0x79, 0x7a, 0x7b, 0x7c, 0x7d, 0x7e, 0x7f,
};
static const uint8_t KAT_SERVER_RND[32] = {
    0x5e, 0x5e, 0x5e, 0x5e, 0x5e, 0x5e, 0x5e, 0x5e, 0x5e, 0x5e, 0x5e, 0x5e,
    0x5e, 0x5e, 0x5e, 0x5e, 0x5e, 0x5e, 0x5e, 0x5e, 0x5e, 0x5e, 0x5e, 0x5e,
    0x5e, 0x5e, 0x5e, 0x5e, 0x5e, 0x5e, 0x5e, 0x5e,
};
static const uint8_t KAT_SERVER_HS_KEY[32] = {
    0x41, 0xc1, 0xe2, 0xdb, 0x19, 0x3b, 0xc8, 0x84, 0x1e, 0xb5, 0xc6, 0x4a,
    0xe3, 0x08, 0x09, 0x30, 0x6e, 0x8a, 0xc8, 0x2e, 0x33, 0xc7, 0xd5, 0xfe,
    0x64, 0xce, 0x20, 0x6f, 0x2f, 0x32, 0xd6, 0xa4,
};
static const uint8_t KAT_SERVER_HS_IV[12] = {
    0x38, 0x9c, 0xa9, 0x76, 0x72, 0x06, 0x7e, 0x0c, 0x73, 0x31, 0xf0, 0x64,
};
static const uint8_t KAT_CLIENT_HS_KEY[32] = {
    0x5d, 0x09, 0xb3, 0x22, 0x93, 0x47, 0xca, 0x9f, 0x67, 0x0f, 0x6b, 0x98,
    0x8d, 0x30, 0xdc, 0xa3, 0x4f, 0xc4, 0x45, 0x77, 0x05, 0xf4, 0x5d, 0x30,
    0xd2, 0xb5, 0x19, 0x39, 0x5f, 0x0b, 0x85, 0xd2,
};
static const uint8_t KAT_CLIENT_HS_IV[12] = {
    0xa3, 0x18, 0xa3, 0xeb, 0x91, 0x9e, 0xb2, 0x74, 0x50, 0x5a, 0x6c, 0x9b,
};
static const uint8_t KAT_CLIENT_FINISHED_VD[32] = {
    0xa3, 0xb6, 0x47, 0x2e, 0xd7, 0x60, 0xfd, 0xb0, 0xc3, 0x55, 0x79, 0xf0,
    0x8e, 0xdb, 0x75, 0xbc, 0x87, 0x59, 0x9e, 0xa8, 0x51, 0xd1, 0x93, 0x22,
    0x85, 0xce, 0x6f, 0x29, 0x0d, 0x0b, 0xf7, 0x4f,
};
static const uint8_t KAT_SERVER_AP_KEY[32] = {
    0x36, 0xe0, 0x70, 0xfc, 0x68, 0xdd, 0xab, 0x6d, 0xa2, 0x8f, 0xa6, 0x3b,
    0xe1, 0xac, 0x73, 0x6b, 0x01, 0x63, 0x57, 0xb7, 0xe2, 0x3c, 0x72, 0x8a,
    0x0e, 0xdd, 0x46, 0x05, 0xe7, 0xc3, 0xf4, 0x1d,
};
static const uint8_t KAT_SERVER_AP_IV[12] = {
    0x39, 0x6b, 0x0d, 0x59, 0xec, 0xb3, 0x89, 0x34, 0x53, 0x87, 0x4a, 0xa4,
};
static const uint8_t KAT_CLIENT_AP_KEY[32] = {
    0x28, 0xc8, 0x92, 0x97, 0x21, 0xfa, 0x74, 0x2e, 0xc8, 0x0a, 0x78, 0x1b,
    0xed, 0xa8, 0x12, 0x2d, 0x28, 0xad, 0x03, 0xc3, 0x04, 0xdc, 0x8d, 0x8b,
    0x07, 0xa6, 0xbe, 0x6a, 0x35, 0x76, 0x8e, 0xa4,
};
static const uint8_t KAT_CLIENT_AP_IV[12] = {
    0xa3, 0xd7, 0xd4, 0x06, 0x4c, 0x40, 0x49, 0xc6, 0xab, 0x8a, 0x76, 0xc5,
};
static const uint8_t KAT_FLIGHT_PLAIN[223] = {
    0x08, 0x00, 0x00, 0x02, 0x00, 0x00, 0x0b, 0x00, 0x00, 0x69, 0x00, 0x00,
    0x00, 0x65, 0x00, 0x00, 0x60, 0xa0, 0xa1, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6,
    0xa7, 0xa8, 0xa9, 0xaa, 0xab, 0xac, 0xad, 0xae, 0xaf, 0xb0, 0xb1, 0xb2,
    0xb3, 0xb4, 0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xbb, 0xbc, 0xbd, 0xbe,
    0xbf, 0xa0, 0xa1, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7, 0xa8, 0xa9, 0xaa,
    0xab, 0xac, 0xad, 0xae, 0xaf, 0xb0, 0xb1, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6,
    0xb7, 0xb8, 0xb9, 0xba, 0xbb, 0xbc, 0xbd, 0xbe, 0xbf, 0xa0, 0xa1, 0xa2,
    0xa3, 0xa4, 0xa5, 0xa6, 0xa7, 0xa8, 0xa9, 0xaa, 0xab, 0xac, 0xad, 0xae,
    0xaf, 0xb0, 0xb1, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba,
    0xbb, 0xbc, 0xbd, 0xbe, 0xbf, 0x00, 0x00, 0x0f, 0x00, 0x00, 0x44, 0x08,
    0x07, 0x00, 0x40, 0x77, 0x8d, 0xea, 0x16, 0xcf, 0xc9, 0x85, 0x9d, 0x19,
    0x57, 0x68, 0x5b, 0xd3, 0x1e, 0x13, 0x57, 0xe3, 0xae, 0x8b, 0xc5, 0xdb,
    0x43, 0xa6, 0xbd, 0x94, 0xd9, 0x7c, 0x49, 0x75, 0xfe, 0xc1, 0xdb, 0x73,
    0x0e, 0x2b, 0x2a, 0x9f, 0x57, 0x33, 0x83, 0x3d, 0x85, 0xe6, 0xb9, 0x81,
    0x0b, 0x2e, 0x4f, 0xa8, 0x2d, 0x95, 0xb7, 0xee, 0x28, 0x28, 0x60, 0xdf,
    0x11, 0x36, 0x16, 0xdd, 0x24, 0xc9, 0x0b, 0x14, 0x00, 0x00, 0x20, 0x20,
    0x8c, 0x72, 0xcc, 0xea, 0x25, 0xc2, 0x4c, 0x0a, 0xa9, 0x7f, 0x3d, 0x06,
    0x7f, 0xa3, 0x7b, 0x29, 0xcf, 0xb4, 0x6f, 0xfb, 0xe4, 0x0e, 0xa3, 0x02,
    0xc0, 0x68, 0xa1, 0x2d, 0xba, 0xb5, 0xe3,
};

/* ===== HelloRetryRequest flow, from the independent hrr_oracle.py =====
 * CH1 offers X25519 in supported_groups but a key_share for secp256r1 only, so the
 * server must answer with a HelloRetryRequest. CH2 is the same ClientHello (same
 * Random + session_id) now carrying the X25519 key_share. The keys below are
 * derived over the §4.4.1-rewritten transcript
 *   message_hash(Hash(CH1)) || HRR || CH2 || ServerHello || <flight...>
 * so matching them proves the C server's transcript rewrite is correct. */
static const uint8_t KAT_CH1[144] = {
    0x01, 0x00, 0x00, 0x8c, 0x03, 0x03, 0xc1, 0xc1, 0xc1, 0xc1, 0xc1, 0xc1,
    0xc1, 0xc1, 0xc1, 0xc1, 0xc1, 0xc1, 0xc1, 0xc1, 0xc1, 0xc1, 0xc1, 0xc1,
    0xc1, 0xc1, 0xc1, 0xc1, 0xc1, 0xc1, 0xc1, 0xc1, 0xc1, 0xc1, 0xc1, 0xc1,
    0xc1, 0xc1, 0x20, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
    0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11, 0x12, 0x13, 0x14,
    0x15, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f, 0x00,
    0x02, 0x13, 0x03, 0x01, 0x00, 0x00, 0x41, 0x00, 0x2b, 0x00, 0x03, 0x02,
    0x03, 0x04, 0x00, 0x0a, 0x00, 0x04, 0x00, 0x02, 0x00, 0x1d, 0x00, 0x0d,
    0x00, 0x04, 0x00, 0x02, 0x08, 0x07, 0x00, 0x33, 0x00, 0x26, 0x00, 0x24,
    0x00, 0x17, 0x00, 0x20, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab,
    0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab,
    0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab,
};
static const uint8_t KAT_CH2[144] = {
    0x01, 0x00, 0x00, 0x8c, 0x03, 0x03, 0xc1, 0xc1, 0xc1, 0xc1, 0xc1, 0xc1,
    0xc1, 0xc1, 0xc1, 0xc1, 0xc1, 0xc1, 0xc1, 0xc1, 0xc1, 0xc1, 0xc1, 0xc1,
    0xc1, 0xc1, 0xc1, 0xc1, 0xc1, 0xc1, 0xc1, 0xc1, 0xc1, 0xc1, 0xc1, 0xc1,
    0xc1, 0xc1, 0x20, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
    0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11, 0x12, 0x13, 0x14,
    0x15, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f, 0x00,
    0x02, 0x13, 0x03, 0x01, 0x00, 0x00, 0x41, 0x00, 0x2b, 0x00, 0x03, 0x02,
    0x03, 0x04, 0x00, 0x0a, 0x00, 0x04, 0x00, 0x02, 0x00, 0x1d, 0x00, 0x0d,
    0x00, 0x04, 0x00, 0x02, 0x08, 0x07, 0x00, 0x33, 0x00, 0x26, 0x00, 0x24,
    0x00, 0x1d, 0x00, 0x20, 0x79, 0xa6, 0x31, 0xee, 0xde, 0x1b, 0xf9, 0xc9,
    0x8f, 0x12, 0x03, 0x2c, 0xde, 0xad, 0xd0, 0xe7, 0xa0, 0x79, 0x39, 0x8f,
    0xc7, 0x86, 0xb8, 0x8c, 0xc8, 0x46, 0xec, 0x89, 0xaf, 0x85, 0xa5, 0x1a,
};
static const uint8_t KAT_HRR_RECORD[93] = {
    0x16, 0x03, 0x03, 0x00, 0x58, 0x02, 0x00, 0x00, 0x54, 0x03, 0x03, 0xcf,
    0x21, 0xad, 0x74, 0xe5, 0x9a, 0x61, 0x11, 0xbe, 0x1d, 0x8c, 0x02, 0x1e,
    0x65, 0xb8, 0x91, 0xc2, 0xa2, 0x11, 0x16, 0x7a, 0xbb, 0x8c, 0x5e, 0x07,
    0x9e, 0x09, 0xe2, 0xc8, 0xa8, 0x33, 0x9c, 0x20, 0x00, 0x01, 0x02, 0x03,
    0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
    0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b,
    0x1c, 0x1d, 0x1e, 0x1f, 0x13, 0x03, 0x00, 0x00, 0x0c, 0x00, 0x2b, 0x00,
    0x02, 0x03, 0x04, 0x00, 0x33, 0x00, 0x02, 0x00, 0x1d,
};
static const uint8_t KAT_HRR_CLIENT_HS_KEY[32] = {
    0x4e, 0x54, 0x60, 0x2c, 0x2a, 0xfd, 0x29, 0x53, 0x3a, 0x0e, 0xbe, 0x44,
    0xb7, 0x9d, 0x32, 0x6d, 0xb5, 0x8a, 0xd3, 0x1c, 0x71, 0x7b, 0x12, 0xe4,
    0xfb, 0x4d, 0x51, 0x71, 0x48, 0x09, 0x04, 0x4a,
};
static const uint8_t KAT_HRR_CLIENT_HS_IV[12] = {
    0xef, 0x8e, 0x55, 0x6a, 0x59, 0x11, 0x72, 0x4d, 0x37, 0x5f, 0x29, 0x0f,
};
static const uint8_t KAT_HRR_CLIENT_FINISHED_VD[32] = {
    0x57, 0xfb, 0xf1, 0x46, 0x76, 0xc1, 0xa3, 0xc2, 0xfa, 0xed, 0x3d, 0xdf,
    0xe8, 0xfd, 0x46, 0x1e, 0x33, 0x46, 0x90, 0xa8, 0x90, 0x8c, 0xb9, 0x44,
    0x50, 0x98, 0x56, 0x13, 0x99, 0xd0, 0x82, 0xc6,
};
static const uint8_t KAT_HRR_SERVER_AP_KEY[32] = {
    0x21, 0xfa, 0xcf, 0xaf, 0x91, 0x18, 0xfe, 0xcb, 0x3b, 0xee, 0x6e, 0x3e,
    0xe5, 0x1f, 0x3d, 0xd2, 0xa3, 0x14, 0x38, 0x6b, 0xee, 0xc5, 0x4f, 0xf8,
    0x00, 0x88, 0x6a, 0x77, 0x04, 0xc0, 0x5f, 0x25,
};
static const uint8_t KAT_HRR_SERVER_AP_IV[12] = {
    0x14, 0x4b, 0x17, 0x5a, 0x77, 0x70, 0x47, 0x77, 0x6e, 0xda, 0xd8, 0xd9,
};
static const uint8_t KAT_HRR_CLIENT_AP_KEY[32] = {
    0x6f, 0xf4, 0x9d, 0xd9, 0x89, 0xf0, 0x84, 0xc8, 0x91, 0xbf, 0xc0, 0xa2,
    0x4f, 0x25, 0xc2, 0x0c, 0xa6, 0x0d, 0x90, 0x18, 0x15, 0xf7, 0x43, 0xa5,
    0xcb, 0x2b, 0x37, 0x03, 0xc6, 0x27, 0x73, 0x96,
};
static const uint8_t KAT_HRR_CLIENT_AP_IV[12] = {
    0x29, 0x78, 0x47, 0x1c, 0x5e, 0x51, 0x8d, 0x97, 0xde, 0x56, 0x82, 0x66,
};

/* Find the first occurrence of `needle` in `hay` (portable; avoids memmem). */
static long find_bytes(const uint8_t *hay, size_t hn, const uint8_t *needle, size_t nn) {
    size_t i;
    if (nn == 0 || hn < nn) {
        return -1;
    }
    for (i = 0; i + nn <= hn; i++) {
        if (memcmp(hay + i, needle, nn) == 0) {
            return (long)i;
        }
    }
    return -1;
}

/* Seal a client Finished { type(20) || u24 len(32) || verify_data } record with
 * the given client handshake key/IV at sequence 0 (what a real client sends). */
static int seal_client_finished_with(const uint8_t *vd, const uint8_t *key,
                                      const uint8_t *iv, uint8_t *rec, size_t rec_cap,
                                      size_t *rec_len) {
    uint8_t msg[4 + 32];
    msg[0] = TLS_HS_FINISHED;
    msg[1] = 0x00;
    msg[2] = 0x00;
    msg[3] = 0x20;
    memcpy(msg + 4, vd, 32);
    return tls_record_seal(key, iv, 0, TLS_CONTENT_HANDSHAKE, msg, sizeof msg, 0,
                           rec, rec_cap, rec_len);
}

static int seal_client_finished(const uint8_t *vd, uint8_t *rec, size_t rec_cap,
                                size_t *rec_len) {
    return seal_client_finished_with(vd, KAT_CLIENT_HS_KEY, KAT_CLIENT_HS_IV,
                                     rec, rec_cap, rec_len);
}

/* Copy KAT_CH, overwrite `repl` at the offset of `pat`, feed it, and require the
 * handshake to reject it with `expect_alert` and latch FAILED. */
static void check_ch_reject(const char *label, const tls_server_config_t *cfg,
                            const uint8_t *pat, size_t patn, size_t rel,
                            const uint8_t *repl, size_t repln, uint8_t expect_alert) {
    uint8_t ch[sizeof KAT_CH];
    uint8_t out[2048];
    size_t out_len = 0;
    tls_server_hs_t hs;
    long off;

    memcpy(ch, KAT_CH, sizeof KAT_CH);
    off = find_bytes(ch, sizeof ch, pat, patn);
    if (off < 0 || (size_t)off + rel + repln > sizeof ch) {
        check_true(label, 0);   /* the anchor pattern must exist */
        return;
    }
    memcpy(ch + off + rel, repl, repln);
    tls_server_hs_init(&hs);
    check_true(label,
               tls_server_hs_read_client_hello(&hs, cfg, ch, sizeof ch,
                                                out, sizeof out, &out_len) == 0
               && tls_server_hs_alert(&hs) == expect_alert
               && tls_server_hs_phase(&hs) == TLS_SERVER_HS_FAILED
               && out_len == 0);
}

static void test_server_handshake(void) {
    tls_server_hs_t hs;
    tls_server_config_t cfg;
    uint8_t out[2048];
    size_t out_len = 0;
    size_t sh_len, rec_off;
    uint8_t flight[512];
    size_t flen = 0;
    uint8_t ctype = 0;
    uint8_t rec[128];
    size_t rec_len = 0;
    uint8_t sk[32], siv[12], ck[32], civ[12];

    memset(&cfg, 0, sizeof cfg);
    cfg.cert_der = KAT_CERT;
    cfg.cert_len = sizeof KAT_CERT;
    cfg.ed25519_seed = KAT_ED_SEED;
    cfg.ed25519_pub = KAT_ED_PUB;
    cfg.server_eph_sk = KAT_SERVER_EPH;
    cfg.server_random = KAT_SERVER_RND;

    /* ===== Part A: a full handshake, checked against the independent oracle ===== */
    tls_server_hs_init(&hs);
    check_true("srv hs: initial phase START",
               tls_server_hs_phase(&hs) == TLS_SERVER_HS_START);

    check_true("srv hs: ClientHello accepted",
               tls_server_hs_read_client_hello(&hs, &cfg, KAT_CH, sizeof KAT_CH,
                                               out, sizeof out, &out_len) == 1);
    check_true("srv hs: phase -> WAIT_FINISHED",
               tls_server_hs_phase(&hs) == TLS_SERVER_HS_WAIT_FINISHED);
    /* Authentication cannot be skipped: app keys are withheld until the client
     * Finished has been verified. */
    check_true("srv hs: app keys withheld before DONE",
               tls_server_hs_app_keys(&hs, sk, siv, ck, civ) == 0);

    check_true("srv hs: ServerHello record framed (type 22)",
               out_len > 5 && out[0] == 0x16 && out[1] == 0x03 && out[2] == 0x03);
    sh_len = ((size_t)out[3] << 8) | out[4];
    rec_off = 5 + sh_len;
    check_true("srv hs: protected flight record follows (type 23)",
               rec_off < out_len && out[rec_off] == 0x17);

    /* Open the flight with the INDEPENDENTLY-derived server handshake key. Success
     * proves our ECDH + key schedule + record sealing agree with a separate
     * implementation, and the plaintext must match byte-for-byte. */
    check_true("srv hs: flight opens under independent key schedule",
               tls_record_open(KAT_SERVER_HS_KEY, KAT_SERVER_HS_IV, 0,
                               out + rec_off, out_len - rec_off,
                               flight, sizeof flight, &flen, &ctype) == 1);
    check_true("srv hs: flight inner type handshake", ctype == TLS_CONTENT_HANDSHAKE);
    check_true("srv hs: flight bytes match independent prediction",
               flen == sizeof KAT_FLIGHT_PLAIN
               && memcmp(flight, KAT_FLIGHT_PLAIN, flen) == 0);

    /* Complete: the client returns its Finished (sealed with the independent client
     * handshake key). */
    check_true("srv hs: (harness) seal client Finished",
               seal_client_finished(KAT_CLIENT_FINISHED_VD, rec, sizeof rec, &rec_len) == 1);
    check_true("srv hs: client Finished verified -> handshake complete",
               tls_server_hs_read_client_finished(&hs, rec, rec_len) == 1);
    check_true("srv hs: phase -> DONE", tls_server_hs_phase(&hs) == TLS_SERVER_HS_DONE);

    check_true("srv hs: app keys released after DONE",
               tls_server_hs_app_keys(&hs, sk, siv, ck, civ) == 1);
    check_true("srv hs: server app key matches oracle", memcmp(sk, KAT_SERVER_AP_KEY, 32) == 0);
    check_true("srv hs: server app iv matches oracle", memcmp(siv, KAT_SERVER_AP_IV, 12) == 0);
    check_true("srv hs: client app key matches oracle", memcmp(ck, KAT_CLIENT_AP_KEY, 32) == 0);
    check_true("srv hs: client app iv matches oracle", memcmp(civ, KAT_CLIENT_AP_IV, 12) == 0);

    /* A second Finished after DONE is a protocol violation: fail-closed. */
    check_true("srv hs: second Finished after DONE rejected",
               tls_server_hs_read_client_finished(&hs, rec, rec_len) == 0
               && tls_server_hs_phase(&hs) == TLS_SERVER_HS_FAILED);
    check_true("srv hs: app keys withheld once FAILED",
               tls_server_hs_app_keys(&hs, sk, siv, ck, civ) == 0);

    /* ===== Part B: sequencing / state-machine defences ===== */
    /* B1: ClientHello replayed after we advanced -> unexpected_message + terminal. */
    tls_server_hs_init(&hs);
    tls_server_hs_read_client_hello(&hs, &cfg, KAT_CH, sizeof KAT_CH, out, sizeof out, &out_len);
    check_true("srv hs: replayed ClientHello rejected",
               tls_server_hs_read_client_hello(&hs, &cfg, KAT_CH, sizeof KAT_CH,
                                               out, sizeof out, &out_len) == 0
               && tls_server_hs_alert(&hs) == TLS_ALERT_UNEXPECTED_MESSAGE
               && tls_server_hs_phase(&hs) == TLS_SERVER_HS_FAILED);
    check_true("srv hs: FAILED is terminal",
               tls_server_hs_read_client_hello(&hs, &cfg, KAT_CH, sizeof KAT_CH,
                                               out, sizeof out, &out_len) == 0
               && tls_server_hs_phase(&hs) == TLS_SERVER_HS_FAILED);

    /* B2: client Finished before any ClientHello -> unexpected_message. */
    tls_server_hs_init(&hs);
    seal_client_finished(KAT_CLIENT_FINISHED_VD, rec, sizeof rec, &rec_len);
    check_true("srv hs: Finished before ClientHello rejected",
               tls_server_hs_read_client_finished(&hs, rec, rec_len) == 0
               && tls_server_hs_alert(&hs) == TLS_ALERT_UNEXPECTED_MESSAGE);

    /* B3: truncated ClientHello -> decode_error. */
    tls_server_hs_init(&hs);
    check_true("srv hs: truncated ClientHello -> decode_error",
               tls_server_hs_read_client_hello(&hs, &cfg, KAT_CH, 20,
                                               out, sizeof out, &out_len) == 0
               && tls_server_hs_alert(&hs) == TLS_ALERT_DECODE_ERROR);

    /* B4: undersized output buffer -> internal_error (fail-closed, not a crash). */
    tls_server_hs_init(&hs);
    check_true("srv hs: tiny out buffer -> internal_error",
               tls_server_hs_read_client_hello(&hs, &cfg, KAT_CH, sizeof KAT_CH,
                                               out, 10, &out_len) == 0
               && tls_server_hs_alert(&hs) == TLS_ALERT_INTERNAL_ERROR);

    /* B4b: a NULL out_len is a usage error (the caller must always learn the
     * response length) -> internal_error, never a "successful" unknown-length send. */
    tls_server_hs_init(&hs);
    check_true("srv hs: NULL out_len -> internal_error",
               tls_server_hs_read_client_hello(&hs, &cfg, KAT_CH, sizeof KAT_CH,
                                               out, sizeof out, NULL) == 0
               && tls_server_hs_alert(&hs) == TLS_ALERT_INTERNAL_ERROR);

    /* ===== Part C: parameter negotiation (correct alerts) ===== */
    {
        static const uint8_t p_ver[]  = {0x00,0x2b,0x00,0x03,0x02,0x03,0x04};
        static const uint8_t r_ver[]  = {0x03,0x03};   /* TLS 1.2, not 1.3 */
        static const uint8_t p_suite[]= {0x00,0x02,0x13,0x03,0x01,0x00};
        static const uint8_t r_suite[]= {0x13,0x01};   /* AES-128-GCM, not ChaCha20 */
        static const uint8_t p_sig[]  = {0x00,0x0d,0x00,0x04,0x00,0x02,0x08,0x07};
        static const uint8_t r_sig[]  = {0x08,0x08};   /* ed448, not ed25519 */
        /* Drop X25519 from supported_groups entirely: with no group left to retry
         * with, this is the terminal handshake_failure (not an HRR trigger). A
         * key_share for a non-X25519 group while X25519 is still *offered* triggers
         * an HRR instead — exercised in full by test_tls_hrr below. */
        static const uint8_t p_grp[]  = {0x00,0x0a,0x00,0x04,0x00,0x02,0x00,0x1d};
        static const uint8_t r_grp[]  = {0x00,0x1e};   /* x25519 -> unknown group */
        check_ch_reject("srv hs: no TLS 1.3 -> protocol_version", &cfg,
                        p_ver, sizeof p_ver, 5, r_ver, sizeof r_ver,
                        TLS_ALERT_PROTOCOL_VERSION);
        check_ch_reject("srv hs: no ChaCha20 suite -> handshake_failure", &cfg,
                        p_suite, sizeof p_suite, 2, r_suite, sizeof r_suite,
                        TLS_ALERT_HANDSHAKE_FAILURE);
        check_ch_reject("srv hs: no ed25519 -> handshake_failure", &cfg,
                        p_sig, sizeof p_sig, 6, r_sig, sizeof r_sig,
                        TLS_ALERT_HANDSHAKE_FAILURE);
        check_ch_reject("srv hs: no X25519 group at all -> handshake_failure", &cfg,
                        p_grp, sizeof p_grp, 6, r_grp, sizeof r_grp,
                        TLS_ALERT_HANDSHAKE_FAILURE);
    }

    /* ===== Part D: cryptographic defences ===== */
    /* D1: an all-zero (small-order) X25519 share yields an all-zero shared secret,
     * which RFC 8446 §7.4.2 requires we reject. */
    {
        static const uint8_t p_share[] = {0x00,0x1d,0x00,0x20};   /* group + key_exchange len */
        uint8_t ch[sizeof KAT_CH];
        long off;
        memcpy(ch, KAT_CH, sizeof KAT_CH);
        off = find_bytes(ch, sizeof ch, p_share, sizeof p_share);
        if (off < 0 || (size_t)off + 4 + 32 > sizeof ch) {
            /* The anchor must exist. Without this guard, a ClientHello-layout change
             * would leave the share unmodified and the test would fail with a
             * misleading alert mismatch instead of a clear "anchor missing". */
            check_true("srv hs: all-zero ECDH share test anchor located", 0);
        } else {
            memset(ch + off + 4, 0x00, 32);   /* all-zero key_exchange */
            tls_server_hs_init(&hs);
            check_true("srv hs: all-zero ECDH share -> illegal_parameter",
                       tls_server_hs_read_client_hello(&hs, &cfg, ch, sizeof ch,
                                                       out, sizeof out, &out_len) == 0
                       && tls_server_hs_alert(&hs) == TLS_ALERT_ILLEGAL_PARAMETER);
        }
    }

    /* D2: a Finished with the wrong verify_data (but valid AEAD) -> decrypt_error.
     * This is the key-confirmation check; it must actually compare. */
    {
        uint8_t bad[32];
        memcpy(bad, KAT_CLIENT_FINISHED_VD, 32);
        bad[0] ^= 0x01;
        tls_server_hs_init(&hs);
        tls_server_hs_read_client_hello(&hs, &cfg, KAT_CH, sizeof KAT_CH, out, sizeof out, &out_len);
        seal_client_finished(bad, rec, sizeof rec, &rec_len);
        check_true("srv hs: wrong verify_data -> decrypt_error",
                   tls_server_hs_read_client_finished(&hs, rec, rec_len) == 0
                   && tls_server_hs_alert(&hs) == TLS_ALERT_DECRYPT_ERROR
                   && tls_server_hs_phase(&hs) == TLS_SERVER_HS_FAILED);
    }

    /* D3: a corrupted Finished record (AEAD tag fails) -> bad_record_mac, no
     * plaintext released. */
    {
        tls_server_hs_init(&hs);
        tls_server_hs_read_client_hello(&hs, &cfg, KAT_CH, sizeof KAT_CH, out, sizeof out, &out_len);
        seal_client_finished(KAT_CLIENT_FINISHED_VD, rec, sizeof rec, &rec_len);
        rec[6] ^= 0x80;   /* flip a ciphertext byte (past the 5-byte header) */
        check_true("srv hs: corrupted Finished record -> bad_record_mac",
                   tls_server_hs_read_client_finished(&hs, rec, rec_len) == 0
                   && tls_server_hs_alert(&hs) == TLS_ALERT_BAD_RECORD_MAC);
    }
}

/*
 * TLS connection engine (tls_khannection). Reuses the independent-oracle KAT_* vectors
 * above: the ClientHello and client Finished are the same fixed inputs, and the
 * connection's application records are sealed/opened with the oracle-derived
 * application keys — so the crypto is checked against the separate derivation while
 * these cases add the new record-framing / state-dispatch surface.
 */
static void conn_set_cfg(tls_server_config_t *cfg) {
    memset(cfg, 0, sizeof *cfg);
    cfg->cert_der = KAT_CERT;
    cfg->cert_len = sizeof KAT_CERT;
    cfg->ed25519_seed = KAT_ED_SEED;
    cfg->ed25519_pub = KAT_ED_PUB;
    cfg->server_eph_sk = KAT_SERVER_EPH;
    cfg->server_random = KAT_SERVER_RND;
}

/* Frame arbitrary handshake-message bytes as one plaintext handshake record. */
static size_t frame_hs_record(uint8_t *rec, const uint8_t *msg, size_t n) {
    rec[0] = TLS_CONTENT_HANDSHAKE;
    rec[1] = 0x03;
    rec[2] = 0x03;
    rec[3] = (uint8_t)(n >> 8);
    rec[4] = (uint8_t)(n & 0xff);
    memcpy(rec + 5, msg, n);
    return 5 + n;
}

/* Frame KAT_CH as a plaintext handshake record. */
static size_t conn_make_ch_record(uint8_t *rec) {
    return frame_hs_record(rec, KAT_CH, sizeof KAT_CH);
}

/* Drive a fresh connection to ESTABLISHED (ClientHello then client Finished). */
static void conn_establish(tls_khannection_t *c, const tls_server_config_t *cfg) {
    uint8_t out[2048], app[64], ch[5 + sizeof KAT_CH], fin[128];
    size_t ol = 0, al = 0, chl, finl = 0;
    chl = conn_make_ch_record(ch);
    tls_khannection_init(c, cfg);
    tls_khannection_recv(c, ch, chl, out, sizeof out, &ol, app, sizeof app, &al);
    seal_client_finished(KAT_CLIENT_FINISHED_VD, fin, sizeof fin, &finl);
    tls_khannection_recv(c, fin, finl, out, sizeof out, &ol, app, sizeof app, &al);
}

/* HelloRetryRequest round trip (RFC 8446 §4.1.4), checked against hrr_oracle.py.
 * The synthetic-transcript rewrite (§4.4.1) is the highest-risk new logic, so the
 * app keys — which depend on the ENTIRE rewritten transcript — are matched against
 * the independent oracle: agreement proves the rewrite is byte-correct. */
static void test_tls_hrr(void) {
    tls_server_hs_t hs;
    tls_server_config_t cfg;
    uint8_t out[2048];
    size_t out_len = 0;
    uint8_t rec[128];
    size_t rec_len = 0;
    uint8_t sk[32], siv[12], ck[32], civ[12];

    memset(&cfg, 0, sizeof cfg);
    cfg.cert_der = KAT_CERT;
    cfg.cert_len = sizeof KAT_CERT;
    cfg.ed25519_seed = KAT_ED_SEED;
    cfg.ed25519_pub = KAT_ED_PUB;
    cfg.server_eph_sk = KAT_SERVER_EPH;
    cfg.server_random = KAT_SERVER_RND;

    /* ===== Part A: the happy path CH1 -> HRR -> CH2 -> flight -> DONE ===== */
    tls_server_hs_init(&hs);
    check_true("hrr: CH1 (no X25519 share) accepted",
               tls_server_hs_read_client_hello(&hs, &cfg, KAT_CH1, sizeof KAT_CH1,
                                               out, sizeof out, &out_len) == 1);
    check_true("hrr: phase -> WAIT_CH2",
               tls_server_hs_phase(&hs) == TLS_SERVER_HS_WAIT_CH2);
    check_true("hrr: HRR record matches independent oracle byte-for-byte",
               out_len == sizeof KAT_HRR_RECORD
               && memcmp(out, KAT_HRR_RECORD, out_len) == 0);
    check_true("hrr: app keys withheld in WAIT_CH2",
               tls_server_hs_app_keys(&hs, sk, siv, ck, civ) == 0);

    check_true("hrr: CH2 (with X25519 share) accepted",
               tls_server_hs_read_client_hello(&hs, &cfg, KAT_CH2, sizeof KAT_CH2,
                                               out, sizeof out, &out_len) == 1);
    check_true("hrr: phase -> WAIT_FINISHED",
               tls_server_hs_phase(&hs) == TLS_SERVER_HS_WAIT_FINISHED);
    check_true("hrr: flight ServerHello record framed (type 22)",
               out_len > 5 && out[0] == 0x16);

    check_true("hrr: (harness) seal client Finished over rewritten transcript",
               seal_client_finished_with(KAT_HRR_CLIENT_FINISHED_VD,
                                         KAT_HRR_CLIENT_HS_KEY, KAT_HRR_CLIENT_HS_IV,
                                         rec, sizeof rec, &rec_len) == 1);
    check_true("hrr: client Finished verified -> DONE",
               tls_server_hs_read_client_finished(&hs, rec, rec_len) == 1
               && tls_server_hs_phase(&hs) == TLS_SERVER_HS_DONE);
    check_true("hrr: app keys released after DONE",
               tls_server_hs_app_keys(&hs, sk, siv, ck, civ) == 1);
    check_true("hrr: server app key matches oracle", memcmp(sk, KAT_HRR_SERVER_AP_KEY, 32) == 0);
    check_true("hrr: server app iv matches oracle",  memcmp(siv, KAT_HRR_SERVER_AP_IV, 12) == 0);
    check_true("hrr: client app key matches oracle", memcmp(ck, KAT_HRR_CLIENT_AP_KEY, 32) == 0);
    check_true("hrr: client app iv matches oracle",  memcmp(civ, KAT_HRR_CLIENT_AP_IV, 12) == 0);

    /* ===== Part B: HRR-specific state-machine defences ===== */
    /* B1: a client that ignores the retry (CH2 still lacks an X25519 share) is
     * aborted with illegal_parameter — never answered with a second HRR. */
    tls_server_hs_init(&hs);
    tls_server_hs_read_client_hello(&hs, &cfg, KAT_CH1, sizeof KAT_CH1, out, sizeof out, &out_len);
    check_true("hrr: still-share-less CH2 -> illegal_parameter (no HRR loop)",
               tls_server_hs_read_client_hello(&hs, &cfg, KAT_CH1, sizeof KAT_CH1,
                                               out, sizeof out, &out_len) == 0
               && tls_server_hs_phase(&hs) == TLS_SERVER_HS_FAILED
               && tls_server_hs_alert(&hs) == TLS_ALERT_ILLEGAL_PARAMETER);

    /* B2: CH2 whose Random differs from CH1 violates RFC 8446 §4.1.2 -> abort. */
    {
        uint8_t ch2_bad[sizeof KAT_CH2];
        memcpy(ch2_bad, KAT_CH2, sizeof KAT_CH2);
        ch2_bad[6] ^= 0xff;   /* the 32-byte Random begins at offset 6 */
        tls_server_hs_init(&hs);
        tls_server_hs_read_client_hello(&hs, &cfg, KAT_CH1, sizeof KAT_CH1, out, sizeof out, &out_len);
        check_true("hrr: CH2 with altered Random -> illegal_parameter",
                   tls_server_hs_read_client_hello(&hs, &cfg, ch2_bad, sizeof ch2_bad,
                                                   out, sizeof out, &out_len) == 0
                   && tls_server_hs_phase(&hs) == TLS_SERVER_HS_FAILED
                   && tls_server_hs_alert(&hs) == TLS_ALERT_ILLEGAL_PARAMETER);
    }

    /* B3: after CH2 (WAIT_FINISHED) a further ClientHello is unexpected_message. */
    tls_server_hs_init(&hs);
    tls_server_hs_read_client_hello(&hs, &cfg, KAT_CH1, sizeof KAT_CH1, out, sizeof out, &out_len);
    tls_server_hs_read_client_hello(&hs, &cfg, KAT_CH2, sizeof KAT_CH2, out, sizeof out, &out_len);
    check_true("hrr: ClientHello after WAIT_FINISHED -> unexpected_message",
               tls_server_hs_read_client_hello(&hs, &cfg, KAT_CH2, sizeof KAT_CH2,
                                               out, sizeof out, &out_len) == 0
               && tls_server_hs_phase(&hs) == TLS_SERVER_HS_FAILED
               && tls_server_hs_alert(&hs) == TLS_ALERT_UNEXPECTED_MESSAGE);
}

static void test_tls_khannection(void) {
    static uint8_t out[40000];
    static uint8_t app[40000];
    tls_server_config_t cfg;
    tls_khannection_t c;
    uint8_t ch_rec[5 + sizeof KAT_CH];
    size_t ch_len, out_len = 0, app_len = 0;
    static const uint8_t ccs_rec[6] = { 0x14, 0x03, 0x03, 0x00, 0x01, 0x01 };
    tls_khannection_rc_t rc;

    conn_set_cfg(&cfg);
    ch_len = conn_make_ch_record(ch_rec);

    /* ===== Part A: full handshake + application exchange over the engine ===== */
    tls_khannection_init(&c, &cfg);
    check_true("conn: initial state HANDSHAKE", tls_khannection_state(&c) == TLS_KHANNECTION_HANDSHAKE);

    rc = tls_khannection_recv(&c, ch_rec, ch_len, out, sizeof out, &out_len, app, sizeof app, &app_len);
    check_true("conn: ClientHello accepted, flight emitted",
               rc == TLS_KHANNECTION_RC_OK && tls_khannection_state(&c) == TLS_KHANNECTION_HANDSHAKE
               && out_len > 0 && app_len == 0);
    {
        size_t sh_len = ((size_t)out[3] << 8) | out[4];
        size_t roff = 5 + sh_len;
        uint8_t fl[512];
        size_t fll = 0;
        uint8_t ct = 0;
        check_true("conn: SH plaintext then protected flight, flight matches oracle",
                   out[0] == 0x16 && roff < out_len && out[roff] == 0x17
                   && tls_record_open(KAT_SERVER_HS_KEY, KAT_SERVER_HS_IV, 0,
                                      out + roff, out_len - roff, fl, sizeof fl, &fll, &ct) == 1
                   && ct == TLS_CONTENT_HANDSHAKE
                   && fll == sizeof KAT_FLIGHT_PLAIN && memcmp(fl, KAT_FLIGHT_PLAIN, fll) == 0);
    }

    rc = tls_khannection_recv(&c, ccs_rec, sizeof ccs_rec, out, sizeof out, &out_len, app, sizeof app, &app_len);
    check_true("conn: client ChangeCipherSpec dropped",
               rc == TLS_KHANNECTION_RC_OK && tls_khannection_state(&c) == TLS_KHANNECTION_HANDSHAKE && out_len == 0);

    {
        uint8_t fin[128];
        size_t finl = 0;
        seal_client_finished(KAT_CLIENT_FINISHED_VD, fin, sizeof fin, &finl);
        rc = tls_khannection_recv(&c, fin, finl, out, sizeof out, &out_len, app, sizeof app, &app_len);
        check_true("conn: client Finished -> ESTABLISHED",
                   rc == TLS_KHANNECTION_RC_OK && tls_khannection_state(&c) == TLS_KHANNECTION_ESTABLISHED);
    }

    {
        static const uint8_t msg[] = "HTTP/1.1 200 OK\r\n\r\nhello";
        uint8_t pt[256];
        size_t ptl = 0;
        uint8_t ct = 0;
        rc = tls_khannection_send(&c, msg, sizeof msg - 1, out, sizeof out, &out_len);
        check_true("conn: server app data sealed, decrypts under oracle server_ap key",
                   rc == TLS_KHANNECTION_RC_OK && out_len > 0 && out[0] == 0x17
                   && tls_record_open(KAT_SERVER_AP_KEY, KAT_SERVER_AP_IV, 0,
                                      out, out_len, pt, sizeof pt, &ptl, &ct) == 1
                   && ct == TLS_CONTENT_APPLICATION_DATA
                   && ptl == sizeof msg - 1 && memcmp(pt, msg, ptl) == 0);
    }

    {
        static const uint8_t req[] = "ping";
        uint8_t rec[128];
        size_t recl = 0;
        tls_record_seal(KAT_CLIENT_AP_KEY, KAT_CLIENT_AP_IV, 0, TLS_CONTENT_APPLICATION_DATA,
                        req, sizeof req - 1, 0, rec, sizeof rec, &recl);
        rc = tls_khannection_recv(&c, rec, recl, out, sizeof out, &out_len, app, sizeof app, &app_len);
        check_true("conn: client app data decrypted",
                   rc == TLS_KHANNECTION_RC_OK && app_len == sizeof req - 1
                   && memcmp(app, req, app_len) == 0);
    }

    {
        uint8_t pt[64];
        size_t ptl = 0;
        uint8_t ct = 0;
        rc = tls_khannection_close_notify(&c, out, sizeof out, &out_len);
        /* send_seq advanced to 1 by the server app record above. */
        check_true("conn: close_notify emits an encrypted alert -> CLOSED",
                   rc == TLS_KHANNECTION_RC_CLOSED && tls_khannection_state(&c) == TLS_KHANNECTION_CLOSED
                   && tls_record_open(KAT_SERVER_AP_KEY, KAT_SERVER_AP_IV, 1,
                                      out, out_len, pt, sizeof pt, &ptl, &ct) == 1
                   && ct == TLS_CONTENT_ALERT && ptl == 2 && pt[0] == 1 && pt[1] == 0);
    }

    /* ===== Part B: record framing ===== */
    /* B1: a ClientHello delivered one byte per recv reassembles; no output until
     * the record is complete. */
    {
        size_t i;
        int frag_ok = 1;
        tls_khannection_init(&c, &cfg);
        for (i = 0; i < ch_len; i++) {
            rc = tls_khannection_recv(&c, ch_rec + i, 1, out, sizeof out, &out_len, app, sizeof app, &app_len);
            if (rc != TLS_KHANNECTION_RC_OK) { frag_ok = 0; break; }
            if (i + 1 < ch_len && out_len != 0) { frag_ok = 0; break; }
        }
        check_true("conn: ClientHello reassembled from 1-byte fragments",
                   frag_ok && out_len > 0 && tls_khannection_state(&c) == TLS_KHANNECTION_HANDSHAKE);
    }
    /* B2: CCS and Finished coalesced into a single recv both process. */
    {
        uint8_t combo[6 + 128];
        size_t finl = 0, cl;
        memcpy(combo, ccs_rec, sizeof ccs_rec);
        seal_client_finished(KAT_CLIENT_FINISHED_VD, combo + sizeof ccs_rec,
                             sizeof combo - sizeof ccs_rec, &finl);
        cl = sizeof ccs_rec + finl;
        rc = tls_khannection_recv(&c, combo, cl, out, sizeof out, &out_len, app, sizeof app, &app_len);
        check_true("conn: coalesced CCS+Finished in one recv -> ESTABLISHED",
                   rc == TLS_KHANNECTION_RC_OK && tls_khannection_state(&c) == TLS_KHANNECTION_ESTABLISHED);
    }
    /* B3: a declared record length beyond 2^14+256 is rejected before any body. */
    {
        static const uint8_t big_hdr[5] = { 0x17, 0x03, 0x03, 0xff, 0xff };
        tls_khannection_init(&c, &cfg);
        rc = tls_khannection_recv(&c, big_hdr, sizeof big_hdr, out, sizeof out, &out_len, app, sizeof app, &app_len);
        check_true("conn: over-long record -> record_overflow, FAILED",
                   rc == TLS_KHANNECTION_RC_ERROR && tls_khannection_alert(&c) == 22
                   && tls_khannection_state(&c) == TLS_KHANNECTION_FAILED);
    }

    /* ===== Part C: adversarial / state ===== */
    /* C1: a tampered application record fails AEAD -> bad_record_mac. */
    {
        static const uint8_t req[] = "ping";
        uint8_t rec[128];
        size_t recl = 0;
        conn_establish(&c, &cfg);
        tls_record_seal(KAT_CLIENT_AP_KEY, KAT_CLIENT_AP_IV, 0, TLS_CONTENT_APPLICATION_DATA,
                        req, sizeof req - 1, 0, rec, sizeof rec, &recl);
        rec[7] ^= 0x40;   /* flip a ciphertext byte */
        rc = tls_khannection_recv(&c, rec, recl, out, sizeof out, &out_len, app, sizeof app, &app_len);
        check_true("conn: tampered application record -> bad_record_mac",
                   rc == TLS_KHANNECTION_RC_ERROR && tls_khannection_alert(&c) == 20
                   && tls_khannection_state(&c) == TLS_KHANNECTION_FAILED);
    }
    /* C2: a non-application record after the handshake is unexpected. */
    {
        static const uint8_t bare_hs[9] = { 0x16, 0x03, 0x03, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00 };
        conn_establish(&c, &cfg);
        rc = tls_khannection_recv(&c, bare_hs, sizeof bare_hs, out, sizeof out, &out_len, app, sizeof app, &app_len);
        check_true("conn: plaintext record after handshake -> unexpected_message",
                   rc == TLS_KHANNECTION_RC_ERROR && tls_khannection_alert(&c) == 10);
    }
    /* C3: a malformed ChangeCipherSpec during the handshake is unexpected. */
    {
        static const uint8_t bad_ccs[6] = { 0x14, 0x03, 0x03, 0x00, 0x01, 0x02 };
        tls_khannection_init(&c, &cfg);
        tls_khannection_recv(&c, ch_rec, ch_len, out, sizeof out, &out_len, app, sizeof app, &app_len);
        rc = tls_khannection_recv(&c, bad_ccs, sizeof bad_ccs, out, sizeof out, &out_len, app, sizeof app, &app_len);
        check_true("conn: malformed CCS -> unexpected_message",
                   rc == TLS_KHANNECTION_RC_ERROR && tls_khannection_alert(&c) == 10);
    }
    /* C4: a peer close_notify (encrypted) is a graceful shutdown. */
    {
        uint8_t body[2] = { 1, 0 };
        uint8_t rec[128];
        size_t recl = 0;
        conn_establish(&c, &cfg);
        tls_record_seal(KAT_CLIENT_AP_KEY, KAT_CLIENT_AP_IV, 0, TLS_CONTENT_ALERT,
                        body, 2, 0, rec, sizeof rec, &recl);
        rc = tls_khannection_recv(&c, rec, recl, out, sizeof out, &out_len, app, sizeof app, &app_len);
        check_true("conn: peer close_notify -> CLOSED",
                   rc == TLS_KHANNECTION_RC_CLOSED && tls_khannection_state(&c) == TLS_KHANNECTION_CLOSED);
    }
    /* C5: sending before the handshake completes is refused. */
    {
        tls_khannection_init(&c, &cfg);
        rc = tls_khannection_send(&c, (const uint8_t *)"x", 1, out, sizeof out, &out_len);
        check_true("conn: send before ESTABLISHED refused",
                   rc == TLS_KHANNECTION_RC_ERROR && out_len == 0);
    }
    /* C6: a zero-length recv (a NULL buffer is allowed) is a clean no-op — never
     * NULL pointer arithmetic. */
    {
        tls_khannection_init(&c, &cfg);
        rc = tls_khannection_recv(&c, NULL, 0, out, sizeof out, &out_len, app, sizeof app, &app_len);
        check_true("conn: zero-length recv is a no-op",
                   rc == TLS_KHANNECTION_RC_OK && out_len == 0 && app_len == 0
                   && tls_khannection_state(&c) == TLS_KHANNECTION_HANDSHAKE);
    }
    /* C7: a too-small application buffer is a local capacity fault (internal_error),
     * distinct from a peer AEAD failure (bad_record_mac). */
    {
        static const uint8_t req[] = "hello-world-payload";
        uint8_t rec[128];
        uint8_t tiny[4];
        size_t recl = 0, tol = 0, tal = 0;
        conn_establish(&c, &cfg);
        tls_record_seal(KAT_CLIENT_AP_KEY, KAT_CLIENT_AP_IV, 0, TLS_CONTENT_APPLICATION_DATA,
                        req, sizeof req - 1, 0, rec, sizeof rec, &recl);
        rc = tls_khannection_recv(&c, rec, recl, out, sizeof out, &tol, tiny, sizeof tiny, &tal);
        check_true("conn: too-small app buffer -> internal_error",
                   rc == TLS_KHANNECTION_RC_ERROR && tls_khannection_alert(&c) == 80);
    }

    /* ===== Part D: >16 KiB response splits across records and reassembles ===== */
    {
        static uint8_t big[20000];
        static uint8_t pt[20000];
        static uint8_t scratch[TLS_RECORD_MAX_PLAINTEXT + 1];
        size_t i, off = 0, total = 0;
        uint64_t seq = 0;
        int ok = 1;
        for (i = 0; i < sizeof big; i++) {
            big[i] = (uint8_t)(i * 7 + 1);
        }
        conn_establish(&c, &cfg);
        rc = tls_khannection_send(&c, big, sizeof big, out, sizeof out, &out_len);
        while (off < out_len) {
            size_t rl = 5 + ((((size_t)out[off + 3]) << 8) | out[off + 4]);
            size_t pl = 0;
            uint8_t ct = 0;
            if (off + rl > out_len ||
                !tls_record_open(KAT_SERVER_AP_KEY, KAT_SERVER_AP_IV, seq,
                                 out + off, rl, scratch, sizeof scratch, &pl, &ct) ||
                ct != TLS_CONTENT_APPLICATION_DATA || total + pl > sizeof pt) {
                ok = 0;
                break;
            }
            memcpy(pt + total, scratch, pl);
            total += pl;
            off += rl;
            seq++;
        }
        check_true("conn: >16KiB response split into 2 records, reassembles in order",
                   rc == TLS_KHANNECTION_RC_OK && ok && seq == 2
                   && total == sizeof big && memcmp(pt, big, total) == 0);
    }
}
#endif /* WEBLIB_TLS */

/* HelloRetryRequest driven end-to-end through the connection engine: its
 * record-type dispatch must route CH1 -> HRR, tolerate a middlebox-compat CCS
 * between HRR and CH2, route CH2 -> flight, and the client Finished ->
 * ESTABLISHED, after which application data flows under the HRR-derived keys. */
static void test_tls_khannection_hrr(void) {
    static uint8_t out[2048], app[256];
    tls_server_config_t cfg;
    tls_khannection_t c;
    uint8_t ch1[5 + sizeof KAT_CH1], ch2[5 + sizeof KAT_CH2], fin[128];
    static const uint8_t ccs_rec[6] = { 0x14, 0x03, 0x03, 0x00, 0x01, 0x01 };
    size_t ch1l, ch2l, out_len = 0, app_len = 0, finl = 0;
    tls_khannection_rc_t rc;

    conn_set_cfg(&cfg);
    ch1l = frame_hs_record(ch1, KAT_CH1, sizeof KAT_CH1);
    ch2l = frame_hs_record(ch2, KAT_CH2, sizeof KAT_CH2);

    tls_khannection_init(&c, &cfg);
    rc = tls_khannection_recv(&c, ch1, ch1l, out, sizeof out, &out_len, app, sizeof app, &app_len);
    check_true("conn/hrr: CH1 -> HelloRetryRequest, still HANDSHAKE",
               rc == TLS_KHANNECTION_RC_OK && tls_khannection_state(&c) == TLS_KHANNECTION_HANDSHAKE
               && out_len == sizeof KAT_HRR_RECORD && memcmp(out, KAT_HRR_RECORD, out_len) == 0);

    rc = tls_khannection_recv(&c, ccs_rec, sizeof ccs_rec, out, sizeof out, &out_len, app, sizeof app, &app_len);
    check_true("conn/hrr: CCS between HRR and CH2 dropped",
               rc == TLS_KHANNECTION_RC_OK && out_len == 0
               && tls_khannection_state(&c) == TLS_KHANNECTION_HANDSHAKE);

    rc = tls_khannection_recv(&c, ch2, ch2l, out, sizeof out, &out_len, app, sizeof app, &app_len);
    check_true("conn/hrr: CH2 -> flight emitted",
               rc == TLS_KHANNECTION_RC_OK && out_len > 5 && out[0] == 0x16
               && tls_khannection_state(&c) == TLS_KHANNECTION_HANDSHAKE);

    seal_client_finished_with(KAT_HRR_CLIENT_FINISHED_VD, KAT_HRR_CLIENT_HS_KEY,
                              KAT_HRR_CLIENT_HS_IV, fin, sizeof fin, &finl);
    rc = tls_khannection_recv(&c, fin, finl, out, sizeof out, &out_len, app, sizeof app, &app_len);
    check_true("conn/hrr: client Finished -> ESTABLISHED",
               rc == TLS_KHANNECTION_RC_OK && tls_khannection_state(&c) == TLS_KHANNECTION_ESTABLISHED);

    {
        static const uint8_t req[] = "ping-after-hrr";
        uint8_t recq[128];
        size_t recql = 0;
        tls_record_seal(KAT_HRR_CLIENT_AP_KEY, KAT_HRR_CLIENT_AP_IV, 0,
                        TLS_CONTENT_APPLICATION_DATA, req, sizeof req - 1, 0,
                        recq, sizeof recq, &recql);
        rc = tls_khannection_recv(&c, recq, recql, out, sizeof out, &out_len, app, sizeof app, &app_len);
        check_true("conn/hrr: client app data decrypts under the HRR-derived key",
                   rc == TLS_KHANNECTION_RC_OK && app_len == sizeof req - 1
                   && memcmp(app, req, app_len) == 0);
    }
}

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
    test_client_hello();
    test_hs_build();
    test_server_handshake();
    test_tls_hrr();
    test_tls_khannection();
    test_tls_khannection_hrr();

    if (g_failures == 0) {
        printf("All TLS parse tests passed.\n");
    }
    return g_failures == 0 ? 0 : 1;
#endif
}
