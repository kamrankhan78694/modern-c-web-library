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

    if (g_failures == 0) {
        printf("All TLS parse tests passed.\n");
    }
    return g_failures == 0 ? 0 : 1;
#endif
}
