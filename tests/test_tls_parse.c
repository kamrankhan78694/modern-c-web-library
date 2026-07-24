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
    test_client_hello();
    test_hs_build();

    if (g_failures == 0) {
        printf("All TLS parse tests passed.\n");
    }
    return g_failures == 0 ? 0 : 1;
#endif
}
