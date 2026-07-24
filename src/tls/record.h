/*
 * record.h — TLS 1.3 record protection (RFC 8446 §5). EXPERIMENTAL / UNAUDITED.
 *
 * Part of the experimental pure-C TLS layer; compiled only under
 * -DWEBLIB_ENABLE_TLS=ON. Seals and opens a single TLSCiphertext record with the
 * TLS_CHACHA20_POLY1305_SHA256 AEAD: it constructs the TLSInnerPlaintext
 * (content || ContentType || zero-padding), the per-record nonce (write_iv XOR
 * the 64-bit sequence number, §5.3), and the additional data (the 5-byte record
 * header, §5.2), then calls the verified ChaCha20-Poly1305 AEAD.
 *
 * The key schedule (key_schedule.h) provides the write_key/write_iv; the
 * handshake state machine drives the sequence numbers and content types. Verified
 * against an independent ChaCha20-Poly1305 record reference in
 * tests/test_tls_crypto.c.
 */
#ifndef WEBLIB_TLS_RECORD_H
#define WEBLIB_TLS_RECORD_H

#ifdef WEBLIB_TLS

#include <stddef.h>
#include <stdint.h>

#define TLS_RECORD_HEADER_LEN 5
#define TLS_RECORD_TAG_LEN    16
#define TLS_RECORD_IV_LEN     12
#define TLS_RECORD_KEY_LEN    32   /* ChaCha20-Poly1305 */
/* RFC 8446 §5.2: TLSCiphertext.length (the encrypted_record) MUST NOT exceed
 * 2^14 + 256 bytes. */
#define TLS_RECORD_MAX_CIPHERTEXT (16384 + 256)

/* TLS 1.3 inner ContentType values (RFC 8446 §5.1). */
#define TLS_CONTENT_CHANGE_CIPHER_SPEC 20
#define TLS_CONTENT_ALERT              21
#define TLS_CONTENT_HANDSHAKE          22
#define TLS_CONTENT_APPLICATION_DATA   23

/*
 * Seal one record: encrypt `content` (length `content_len`) of inner type
 * `content_type` with `padding_len` trailing zero-padding bytes, writing the full
 * TLSCiphertext (5-byte header + encrypted_record) to `out`. On success returns 1
 * and sets *out_len to the total record size. Returns 0 on a NULL/oversized
 * argument, a record that would exceed the 2^14+256 limit, `out` too small, or an
 * AEAD failure (in which case `out` is zeroed).
 */
int tls_record_seal(const uint8_t key[TLS_RECORD_KEY_LEN],
                    const uint8_t iv[TLS_RECORD_IV_LEN], uint64_t seq,
                    uint8_t content_type, const uint8_t *content, size_t content_len,
                    size_t padding_len, uint8_t *out, size_t out_cap, size_t *out_len);

/*
 * Open one record: authenticate and decrypt the full TLSCiphertext in `record`,
 * writing the recovered content to `out`, the inner ContentType to *content_type,
 * and the content length to *content_len. Returns 1 on success, 0 on a malformed
 * header, an AEAD authentication failure (no plaintext is released), an all-zero
 * inner plaintext (no ContentType), or `out` too small. The trailing zero padding
 * is removed in constant time with respect to the padding length (§5.4).
 */
int tls_record_open(const uint8_t key[TLS_RECORD_KEY_LEN],
                    const uint8_t iv[TLS_RECORD_IV_LEN], uint64_t seq,
                    const uint8_t *record, size_t record_len,
                    uint8_t *out, size_t out_cap,
                    size_t *content_len, uint8_t *content_type);

#endif /* WEBLIB_TLS */
#endif /* WEBLIB_TLS_RECORD_H */
