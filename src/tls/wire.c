/*
 * wire.c — bounded TLS wire codec (RFC 8446 §3). EXPERIMENTAL / UNAUDITED.
 * See wire.h.
 *
 * Compiled only under -DWEBLIB_ENABLE_TLS=ON. The reader carries a remaining-byte
 * count (not an end pointer), so all bounds are size_t comparisons and no pointer
 * arithmetic is ever performed on an unproven range. The writer never writes past
 * its capacity: the first would-be overflow clears a sticky `ok` flag and all
 * subsequent writes become no-ops.
 */
#include "wire.h"

#ifdef WEBLIB_TLS

#include <string.h>

/* ---- reader ------------------------------------------------------------ */

void tls_reader_init(tls_reader *r, const uint8_t *data, size_t len) {
    if (data == NULL) {
        len = 0;
    }
    r->pos = data;
    r->len = len;
}

size_t tls_reader_remaining(const tls_reader *r) {
    return r->len;
}

int tls_reader_eof(const tls_reader *r) {
    return r->len == 0;
}

int tls_read_u8(tls_reader *r, uint8_t *out) {
    if (r->len < 1) {
        return 0;
    }
    *out = r->pos[0];
    r->pos += 1;
    r->len -= 1;
    return 1;
}

int tls_read_u16(tls_reader *r, uint16_t *out) {
    if (r->len < 2) {
        return 0;
    }
    *out = (uint16_t)(((uint16_t)r->pos[0] << 8) | r->pos[1]);
    r->pos += 2;
    r->len -= 2;
    return 1;
}

int tls_read_u24(tls_reader *r, uint32_t *out) {
    if (r->len < 3) {
        return 0;
    }
    *out = ((uint32_t)r->pos[0] << 16) | ((uint32_t)r->pos[1] << 8) | (uint32_t)r->pos[2];
    r->pos += 3;
    r->len -= 3;
    return 1;
}

int tls_read_bytes(tls_reader *r, const uint8_t **out, size_t n) {
    if (r->len < n) {
        return 0;
    }
    *out = r->pos;
    r->pos += n;
    r->len -= n;
    return 1;
}

int tls_read_vector(tls_reader *r, int len_bytes, tls_reader *body) {
    uint32_t vlen;
    const uint8_t *p;

    if (len_bytes == 1) {
        uint8_t v;
        if (!tls_read_u8(r, &v)) {
            return 0;
        }
        vlen = v;
    } else if (len_bytes == 2) {
        uint16_t v;
        if (!tls_read_u16(r, &v)) {
            return 0;
        }
        vlen = v;
    } else if (len_bytes == 3) {
        if (!tls_read_u24(r, &vlen)) {
            return 0;
        }
    } else {
        return 0;
    }

    if (!tls_read_bytes(r, &p, vlen)) {
        return 0;
    }
    tls_reader_init(body, p, vlen);
    return 1;
}

/* ---- writer ------------------------------------------------------------ */

void tls_writer_init(tls_writer *w, uint8_t *buf, size_t cap) {
    w->buf = buf;
    w->cap = cap;
    w->len = 0;
    w->ok = (buf != NULL);
}

/* Reserve room for `n` more bytes; clear `ok` and return 0 if it doesn't fit.
 * The invariant w->len <= w->cap makes `cap - len` safe. */
static int writer_room(tls_writer *w, size_t n) {
    if (!w->ok) {
        return 0;
    }
    if (n > w->cap - w->len) {
        w->ok = 0;
        return 0;
    }
    return 1;
}

int tls_write_u8(tls_writer *w, uint8_t v) {
    if (!writer_room(w, 1)) {
        return 0;
    }
    w->buf[w->len++] = v;
    return 1;
}

int tls_write_u16(tls_writer *w, uint16_t v) {
    if (!writer_room(w, 2)) {
        return 0;
    }
    w->buf[w->len++] = (uint8_t)(v >> 8);
    w->buf[w->len++] = (uint8_t)(v & 0xff);
    return 1;
}

int tls_write_u24(tls_writer *w, uint32_t v) {
    if (v > 0xFFFFFFu) {   /* not representable in 24 bits */
        w->ok = 0;
        return 0;
    }
    if (!writer_room(w, 3)) {
        return 0;
    }
    w->buf[w->len++] = (uint8_t)(v >> 16);
    w->buf[w->len++] = (uint8_t)(v >> 8);
    w->buf[w->len++] = (uint8_t)(v & 0xff);
    return 1;
}

int tls_write_bytes(tls_writer *w, const uint8_t *data, size_t n) {
    if (n == 0) {
        return w->ok;
    }
    if (data == NULL) {
        w->ok = 0;
        return 0;
    }
    if (!writer_room(w, n)) {
        return 0;
    }
    memcpy(w->buf + w->len, data, n);
    w->len += n;
    return 1;
}

size_t tls_writer_open_vector(tls_writer *w, int len_bytes) {
    size_t marker = w->len;
    int i;
    for (i = 0; i < len_bytes; i++) {
        tls_write_u8(w, 0x00);   /* placeholder; overflow handled via w->ok */
    }
    return marker;
}

void tls_writer_close_vector(tls_writer *w, size_t marker, int len_bytes) {
    size_t body_len, max_len;
    int i;

    if (!w->ok) {
        return;
    }
    /* The body is everything written after the reserved length prefix. */
    body_len = w->len - marker - (size_t)len_bytes;

    if (len_bytes < 1 || len_bytes > 3) {
        w->ok = 0;
        return;
    }
    max_len = ((size_t)1 << (8 * len_bytes)) - 1;   /* 0xFF, 0xFFFF, or 0xFFFFFF */
    if (body_len > max_len) {
        w->ok = 0;
        return;
    }
    for (i = 0; i < len_bytes; i++) {
        w->buf[marker + (size_t)i] = (uint8_t)(body_len >> (8 * (len_bytes - 1 - i)));
    }
}

int tls_writer_finish(const tls_writer *w, size_t *out_len) {
    if (!w->ok) {
        return 0;
    }
    if (out_len != NULL) {
        *out_len = w->len;
    }
    return 1;
}

#endif /* WEBLIB_TLS */
