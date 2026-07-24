/*
 * der.c — minimal, bounds-checked DER (ASN.1) reader. EXPERIMENTAL / UNAUDITED.
 *
 * Compiled only under -DWEBLIB_ENABLE_TLS=ON. See der.h for the contract. The
 * guiding rule here is that the input is untrusted: no code path may read past
 * the cursor's remaining bytes, and every length is validated (definite +
 * minimal) before its value bytes are exposed. Malformed input is always a clean
 * -1, never a crash.
 *
 * The cursor carries a remaining-byte count, not an end pointer, so all bounds
 * are size_t comparisons — no pointer subtraction or relational comparison is
 * ever performed (which would be undefined behaviour on a NULL/empty buffer).
 * `pos` is only offset or dereferenced after the count proves the bytes exist.
 */
#include "der.h"

#ifdef WEBLIB_TLS

void der_init(der_reader_t *r, const uint8_t *data, size_t len) {
    /* A NULL buffer has nothing readable; forcing len to 0 means `pos` (NULL) is
     * never dereferenced or offset, since every read checks the count first. */
    if (data == NULL) {
        len = 0;
    }
    r->pos = data;
    r->len = len;
}

size_t der_remaining(const der_reader_t *r) {
    return r->len;
}

int der_at_end(const der_reader_t *r) {
    return r->len == 0;
}

int der_read_tlv(der_reader_t *r, uint8_t *tag, const uint8_t **val, size_t *val_len) {
    const uint8_t *p = r->pos;
    size_t avail = r->len;
    size_t hdr;        /* bytes consumed by tag + length octets */
    size_t len;        /* decoded value length */
    uint8_t t, lb;

    /* Identifier octet. */
    if (avail < 1) {
        return -1;
    }
    t = p[0];
    /* Reject high-tag-number form (low 5 bits all set): not needed for the
     * structures TLS parses, and it would require multi-byte tag decoding. */
    if ((t & 0x1f) == 0x1f) {
        return -1;
    }

    /* First length octet. */
    if (avail < 2) {
        return -1;
    }
    lb = p[1];
    hdr = 2;

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
        /* Need nbytes length octets after the two header bytes read so far.
         * avail >= 2 == hdr here, so `avail - hdr` does not underflow. */
        if (avail - hdr < nbytes) {
            return -1;
        }
        /* DER requires minimal length encoding: no leading zero octet ... */
        if (p[hdr] == 0x00) {
            return -1;
        }
        len = 0;
        for (i = 0; i < nbytes; i++) {
            len = (len << 8) | p[hdr + i];
        }
        hdr += nbytes;
        /* ... and long form must actually be needed (value >= 128). */
        if (len < 0x80) {
            return -1;
        }
    }

    /* The value must fit in the bytes remaining after the header. `avail >= hdr`
     * holds on every path above, so the subtraction cannot underflow. */
    if (len > avail - hdr) {
        return -1;
    }

    *tag = t;
    *val = p + hdr;
    *val_len = len;
    r->pos = p + hdr + len;
    r->len = avail - hdr - len;
    return 0;
}

int der_expect(der_reader_t *r, uint8_t want_tag, const uint8_t **val, size_t *val_len) {
    uint8_t tag;
    if (der_read_tlv(r, &tag, val, val_len) != 0) {
        return -1;
    }
    if (tag != want_tag) {
        return -1;
    }
    return 0;
}

int der_enter(der_reader_t *r, uint8_t want_tag, der_reader_t *child) {
    const uint8_t *val;
    size_t val_len;
    if (der_expect(r, want_tag, &val, &val_len) != 0) {
        return -1;
    }
    der_init(child, val, val_len);
    return 0;
}

#endif /* WEBLIB_TLS */
