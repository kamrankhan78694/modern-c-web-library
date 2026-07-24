/*
 * der.c — minimal, bounds-checked DER (ASN.1) reader. EXPERIMENTAL / UNAUDITED.
 *
 * Compiled only under -DWEBLIB_ENABLE_TLS=ON. See der.h for the contract. The
 * guiding rule here is that the input is untrusted: no code path may read past
 * [pos, end), and every length is validated (definite + minimal) before its
 * value bytes are exposed. Malformed input is always a clean -1, never a crash.
 */
#include "der.h"

#ifdef WEBLIB_TLS

void der_init(der_reader *r, const uint8_t *data, size_t len) {
    r->pos = data;
    /* Avoid forming data+len when data is NULL (undefined pointer arithmetic);
     * a NULL/empty range simply has nothing to read. */
    r->end = (data != NULL) ? data + len : data;
}

size_t der_remaining(const der_reader *r) {
    return (size_t)(r->end - r->pos);
}

int der_at_end(const der_reader *r) {
    return r->pos == r->end;
}

int der_read_tlv(der_reader *r, uint8_t *tag, const uint8_t **val, size_t *val_len) {
    const uint8_t *p = r->pos;
    uint8_t t, lb;
    size_t len;

    /* Identifier octet. */
    if (p >= r->end) {
        return -1;
    }
    t = *p++;
    /* Reject high-tag-number form (low 5 bits all set): not needed for the
     * structures TLS parses, and it would require multi-byte tag decoding. */
    if ((t & 0x1f) == 0x1f) {
        return -1;
    }

    /* Length octet(s). */
    if (p >= r->end) {
        return -1;
    }
    lb = *p++;
    if (lb < 0x80) {
        /* Short form: the length is the octet itself. */
        len = lb;
    } else if (lb == 0x80) {
        /* Indefinite length is a BER construct, forbidden in DER. */
        return -1;
    } else {
        size_t nbytes = (size_t)(lb & 0x7f);
        size_t i;
        /* A length that needs more octets than a size_t can hold cannot be
         * represented (and certainly cannot fit the buffer). */
        if (nbytes > sizeof(size_t)) {
            return -1;
        }
        if ((size_t)(r->end - p) < nbytes) {
            return -1;
        }
        /* DER requires minimal length encoding: no leading zero octet ... */
        if (p[0] == 0x00) {
            return -1;
        }
        len = 0;
        for (i = 0; i < nbytes; i++) {
            len = (len << 8) | p[i];
        }
        p += nbytes;
        /* ... and long form must actually be needed (value >= 128). */
        if (len < 0x80) {
            return -1;
        }
    }

    /* The value must lie entirely within the buffer. Compare via subtraction of
     * the already-validated pointers so nothing overflows. */
    if (len > (size_t)(r->end - p)) {
        return -1;
    }

    *tag = t;
    *val = p;
    *val_len = len;
    r->pos = p + len;
    return 0;
}

int der_expect(der_reader *r, uint8_t want_tag, const uint8_t **val, size_t *val_len) {
    uint8_t tag;
    if (der_read_tlv(r, &tag, val, val_len) != 0) {
        return -1;
    }
    if (tag != want_tag) {
        return -1;
    }
    return 0;
}

int der_enter(der_reader *r, uint8_t want_tag, der_reader *child) {
    const uint8_t *val;
    size_t val_len;
    if (der_expect(r, want_tag, &val, &val_len) != 0) {
        return -1;
    }
    der_init(child, val, val_len);
    return 0;
}

#endif /* WEBLIB_TLS */
