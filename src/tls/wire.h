/*
 * wire.h — bounded TLS wire codec (RFC 8446 §3). EXPERIMENTAL / UNAUDITED.
 *
 * Part of the experimental pure-C TLS layer; compiled only under
 * -DWEBLIB_ENABLE_TLS=ON. The reader/writer the handshake layer parses and builds
 * every message on: big-endian u8/u16/u24 integers and length-prefixed vectors
 * (RFC 8446 §3.4). The reader is the security-critical half — it parses an
 * untrusted ClientHello — so every read is bounds-checked and never touches
 * memory outside the caller's buffer. The writer tracks a sticky overflow flag so
 * a whole message can be built without checking each field, with one final
 * success test. Exercised by tests/test_tls_parse.c.
 */
#ifndef WEBLIB_TLS_WIRE_H
#define WEBLIB_TLS_WIRE_H

#ifdef WEBLIB_TLS

#include <stddef.h>
#include <stdint.h>

/* ---- reader ------------------------------------------------------------ */

/* A read cursor: `pos` is the next unread byte, `len` the bytes remaining.
 * Internal struct — use the functions below; do not touch the fields. */
typedef struct {
    const uint8_t *pos;
    size_t len;
} tls_reader_t;

/* Initialize over [data, data+len). A NULL `data` is treated as empty. */
void tls_reader_init(tls_reader_t *r, const uint8_t *data, size_t len);

/* Bytes not yet consumed. */
size_t tls_reader_remaining(const tls_reader_t *r);

/* 1 if the cursor is fully consumed (no trailing bytes), else 0. */
int tls_reader_eof(const tls_reader_t *r);

/* Read a big-endian integer, advancing the cursor. Returns 1 on success, 0 if
 * fewer than the needed bytes remain (the cursor is then left unchanged). */
int tls_read_u8(tls_reader_t *r, uint8_t *out);
int tls_read_u16(tls_reader_t *r, uint16_t *out);
int tls_read_u24(tls_reader_t *r, uint32_t *out);

/* Borrow exactly `n` bytes: point *out at them and advance. 0 if fewer remain. */
int tls_read_bytes(tls_reader_t *r, const uint8_t **out, size_t n);

/* Read a length-prefixed vector (RFC 8446 §3.4): a `len_bytes` (1, 2, or 3)
 * big-endian length L followed by L bytes; opens `body` over those L bytes and
 * advances `r` past them. Returns 0 on a bad `len_bytes`, a truncated length, or
 * a body that runs past the buffer. */
int tls_read_vector(tls_reader_t *r, int len_bytes, tls_reader_t *body);

/* ---- writer ------------------------------------------------------------ */

/* An append cursor over a fixed buffer. `ok` is sticky: it clears the first time
 * a write (or a vector length) would not fit, and every later write is a no-op,
 * so a whole message can be assembled and validated once at the end. */
typedef struct {
    uint8_t *buf;
    size_t cap;
    size_t len;
    int ok;
} tls_writer_t;

/* Initialize over the output buffer [buf, buf+cap). */
void tls_writer_init(tls_writer_t *w, uint8_t *buf, size_t cap);

/* Append a big-endian integer / raw bytes. Return the writer's `ok` state after
 * the write (0 once overflowed). u24 also fails if v > 0xFFFFFF. */
int tls_write_u8(tls_writer_t *w, uint8_t v);
int tls_write_u16(tls_writer_t *w, uint16_t v);
int tls_write_u24(tls_writer_t *w, uint32_t v);
int tls_write_bytes(tls_writer_t *w, const uint8_t *data, size_t n);

/*
 * Length-prefixed vector: reserve a `len_bytes` (1, 2, or 3) length placeholder
 * and return a marker; write the body with the normal tls_write_* calls; then
 * pass the marker to tls_writer_close_vector, which backfills the length. Closing
 * fails (clears `ok`) if the body does not fit the prefix width.
 */
size_t tls_writer_open_vector(tls_writer_t *w, int len_bytes);
void tls_writer_close_vector(tls_writer_t *w, size_t marker, int len_bytes);

/* On success sets *out_len (if non-NULL) to the bytes written and returns 1;
 * returns 0 if any write overflowed or a vector length did not fit. */
int tls_writer_finish(const tls_writer_t *w, size_t *out_len);

/* The writer's current success state: 1 while every write has fit, 0 once one
 * has overflowed (or a vector failed to close). */
int tls_writer_ok(const tls_writer_t *w);

#endif /* WEBLIB_TLS */
#endif /* WEBLIB_TLS_WIRE_H */
