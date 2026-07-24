/*
 * pem.c — minimal PEM (RFC 7468) reader. EXPERIMENTAL / UNAUDITED. See pem.h.
 *
 * Compiled only under -DWEBLIB_ENABLE_TLS=ON. Locates a single BEGIN/END block
 * by exact marker match and Base64-decodes the body between the markers. The
 * marker strings never appear inside a valid Base64 body (which has no '-'), so
 * scanning for END within the body is unambiguous. All scanning is bounds-checked
 * against pem_len; the input may be non-NUL-terminated and hold arbitrary bytes.
 */
#include "pem.h"

#ifdef WEBLIB_TLS

#include "crypto/base64.h"
#include <stdio.h>
#include <string.h>

/* Bounded substring search: first occurrence of [needle, needle_len) within
 * [hay, hay_len), or NULL. O(hay_len * needle_len), fine for PEM-sized input. */
static const char *mem_find(const char *hay, size_t hay_len,
                            const char *needle, size_t needle_len) {
    size_t i, last;
    if (needle_len == 0 || needle_len > hay_len) {
        return NULL;
    }
    last = hay_len - needle_len;
    for (i = 0; i <= last; i++) {
        if (hay[i] == needle[0] && memcmp(hay + i, needle, needle_len) == 0) {
            return hay + i;
        }
    }
    return NULL;
}

int pem_decode(const char *label, const char *pem, size_t pem_len,
               unsigned char *out, size_t out_cap, size_t *out_len) {
    char begin[64], end[64];
    size_t label_len, begin_len, end_len, body_len;
    const char *begin_at, *body, *end_at;
    int n, decoded;

    if (!label || !pem || !out || !out_len) {
        return -1;
    }
    label_len = strlen(label);
    if (label_len == 0 || label_len > PEM_MAX_LABEL) {
        return -1;
    }

    /* Build the exact marker lines. The buffers hold "-----BEGIN " (11) +
     * PEM_MAX_LABEL + "-----" (5) + NUL comfortably. */
    n = snprintf(begin, sizeof begin, "-----BEGIN %s-----", label);
    if (n < 0 || (size_t)n >= sizeof begin) {
        return -1;
    }
    begin_len = (size_t)n;
    n = snprintf(end, sizeof end, "-----END %s-----", label);
    if (n < 0 || (size_t)n >= sizeof end) {
        return -1;
    }
    end_len = (size_t)n;

    /* Locate the BEGIN marker; the body starts immediately after it. Any leading
     * newline/whitespace is skipped by the Base64 decoder. */
    begin_at = mem_find(pem, pem_len, begin, begin_len);
    if (begin_at == NULL) {
        return -1;
    }
    body = begin_at + begin_len;

    /* Locate the END marker within whatever follows the BEGIN marker. */
    end_at = mem_find(body, (size_t)(pem + pem_len - body), end, end_len);
    if (end_at == NULL) {
        return -1;
    }
    body_len = (size_t)(end_at - body);

    /* Base64-decode the body (whitespace between the markers is skipped, empty
     * or malformed bodies are rejected by base64_decode). */
    decoded = base64_decode(body, body_len, out, out_cap);
    if (decoded < 0) {
        return -1;
    }

    *out_len = (size_t)decoded;
    return 0;
}

#endif /* WEBLIB_TLS */
