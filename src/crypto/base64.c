/*
 * base64.c — Base64 decode (RFC 4648). See base64.h.
 *
 * Promoted from middleware_auth.c so the JWT/Basic-auth path and the TLS PEM
 * reader share one implementation. Behaviour is identical to the original for the
 * auth path, with one hardening: the whitespace skip uses an explicit ASCII test
 * rather than isspace(), so decoding is locale-independent — worthwhile now that
 * the same routine parses PEM/certificate material. Verified by the RFC 4648
 * known-answer tests in tests/test_weblib.c.
 */
#include "crypto/base64.h"

/* Reverse map for the standard alphabet; 64 marks a non-alphabet byte. */
static const unsigned char base64_decode_table[256] = {
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 62, 64, 64, 64, 63,
    52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 64, 64, 64, 64, 64, 64,
    64,  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14,
    15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 64, 64, 64, 64, 64,
    64, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
    41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 64, 64, 64, 64, 64,
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64
};

/* ASCII whitespace (space, tab, LF, VT, FF, CR) — the set isspace() matches in
 * the C locale, but computed directly so decoding never depends on the locale. */
static int b64_is_space(unsigned char c) {
    return c == ' ' || c == '\t' || c == '\n' ||
           c == '\v' || c == '\f' || c == '\r';
}

int base64_decode(const char *input, size_t input_len,
                  unsigned char *output, size_t output_len) {
    size_t i, j = 0;
    unsigned char buf[4];
    int buf_count = 0;

    if (!input || !output || input_len == 0) {
        return -1;
    }

    for (i = 0; i < input_len; i++) {
        unsigned char c = (unsigned char)input[i];

        /* Skip whitespace (PEM wraps its Base64 body across lines). */
        if (b64_is_space(c)) {
            continue;
        }

        /* Padding marks the end of data. */
        if (c == '=') {
            break;
        }

        {
            unsigned char val = base64_decode_table[c];
            if (val == 64) {
                return -1;   /* not a Base64 alphabet byte */
            }
            buf[buf_count++] = val;
        }

        if (buf_count == 4) {
            /* Overflow-safe capacity check: never form j + 3 (which could wrap
             * for a huge j). j <= output_len is an invariant, so subtract. */
            if (j > output_len || output_len - j < 3) {
                return -1;   /* output buffer too small */
            }
            output[j++] = (unsigned char)((buf[0] << 2) | (buf[1] >> 4));
            output[j++] = (unsigned char)((buf[1] << 4) | (buf[2] >> 2));
            output[j++] = (unsigned char)((buf[2] << 6) | buf[3]);
            buf_count = 0;
        }
    }

    /* A lone trailing symbol cannot encode a byte: reject it rather than
     * silently truncating (structurally invalid Base64). */
    if (buf_count == 1) {
        return -1;
    }

    /* A trailing 2- or 3-symbol group encodes 1 or 2 final bytes. */
    if (buf_count >= 2) {
        if (j > output_len || output_len - j < 1) {
            return -1;
        }
        output[j++] = (unsigned char)((buf[0] << 2) | (buf[1] >> 4));

        if (buf_count >= 3) {
            if (j > output_len || output_len - j < 1) {
                return -1;
            }
            output[j++] = (unsigned char)((buf[1] << 4) | (buf[2] >> 2));
        }
    }

    /* Decoding nothing (all whitespace, or padding only) is a malformed/empty
     * body, not success — callers never receive a 0-length result. */
    if (j == 0) {
        return -1;
    }

    return (int)j;
}
