/*
 * der.h — minimal, bounds-checked DER (ASN.1) reader. EXPERIMENTAL / UNAUDITED.
 *
 * Part of the experimental pure-C TLS layer; compiled only under
 * -DWEBLIB_ENABLE_TLS=ON. Just enough of ITU-T X.690 DER to walk the structures
 * TLS needs — PKCS#8 private keys and SubjectPublicKeyInfo public keys — with a
 * hard security requirement: it parses attacker-controlled input, so every read is bounds- and
 * well-formedness-checked and never reads outside the caller's buffer.
 *
 * The server certificate is NOT parsed here: its PEM body is base64-decoded to DER
 * and sent opaquely, so there is no X.509 certificate-field parsing and no chain
 * validation anywhere in this layer.
 *
 * This is a *reader* (decode only), not an encoder. It enforces DER's canonical
 * rules that matter for safety and identity: definite, minimal-length encoding
 * only (indefinite and non-minimal lengths are rejected), and single-byte tags.
 * Exercised by tests/test_tls_parse.c.
 */
#ifndef WEBLIB_TLS_DER_H
#define WEBLIB_TLS_DER_H

#ifdef WEBLIB_TLS

#include <stddef.h>
#include <stdint.h>

/* Universal ASN.1 tag identifier octets used by X.509 / PKCS#8. The 0x20 bit of
 * SEQUENCE/SET marks them constructed; matching the exact octet also enforces
 * the primitive/constructed distinction. */
#define DER_TAG_BOOLEAN       0x01
#define DER_TAG_INTEGER       0x02
#define DER_TAG_BIT_STRING    0x03
#define DER_TAG_OCTET_STRING  0x04
#define DER_TAG_NULL          0x05
#define DER_TAG_OID           0x06
#define DER_TAG_SEQUENCE      0x30
#define DER_TAG_SET           0x31
/* Context-specific constructed tag [n], e.g. [0] EXPLICIT is DER_TAG_CONTEXT(0). */
#define DER_TAG_CONTEXT(n)    ((uint8_t)(0xA0 | (n)))

/*
 * A read cursor over a DER byte range: `pos` is the next unread byte and `len`
 * is the number of bytes remaining from `pos`. Representing the extent as a
 * length (rather than an end pointer) keeps every bound a size_t comparison, so
 * no pointer arithmetic or relational comparison is ever performed on a possibly
 * NULL/empty buffer. This is an internal struct, exposed only so callers can hold
 * one by value — do not read or write its fields directly; use the API below.
 */
typedef struct {
    const uint8_t *pos;
    size_t len;
} der_reader_t;

/* Initialize a reader over [data, data+len). A NULL `data` is treated as empty
 * (len forced to 0), so no read ever dereferences or offsets a NULL pointer. */
void der_init(der_reader_t *r, const uint8_t *data, size_t len);

/* Bytes not yet consumed. */
size_t der_remaining(const der_reader_t *r);

/* 1 if the cursor is exactly at the end (no trailing bytes), else 0. */
int der_at_end(const der_reader_t *r);

/*
 * Read one TLV. On success returns 0, writes the identifier octet to *tag, and
 * points [*val, *val + *val_len) at the value; the cursor advances past the
 * value. Returns -1 on any malformation — truncation, indefinite length,
 * non-minimal length, multi-byte tag, or a value that runs past the buffer — and
 * leaves the cursor unchanged (callers must stop reading on error).
 */
int der_read_tlv(der_reader_t *r, uint8_t *tag, const uint8_t **val, size_t *val_len);

/* Like der_read_tlv but also requires the tag to equal want_tag. 0 / -1. */
int der_expect(der_reader_t *r, uint8_t want_tag, const uint8_t **val, size_t *val_len);

/*
 * Read a TLV whose tag equals want_tag (typically a constructed type) and open
 * `child` over its contents so the caller can iterate the members. 0 / -1.
 */
int der_enter(der_reader_t *r, uint8_t want_tag, der_reader_t *child);

#endif /* WEBLIB_TLS */
#endif /* WEBLIB_TLS_DER_H */
