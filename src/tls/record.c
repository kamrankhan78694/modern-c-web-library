/*
 * record.c — TLS 1.3 record protection (RFC 8446 §5). EXPERIMENTAL / UNAUDITED.
 * See record.h.
 *
 * Compiled only under -DWEBLIB_ENABLE_TLS=ON. Thin framing over the verified
 * ChaCha20-Poly1305 AEAD: the novel logic here is the per-record nonce (§5.3),
 * the additional-data header (§5.2), the TLSInnerPlaintext construction and its
 * constant-time de-padding (§5.4). The AEAD itself never releases plaintext on an
 * authentication failure. Verified against an independent record reference.
 */
#include "record.h"

#ifdef WEBLIB_TLS

#include "chacha20poly1305.h"
#include "kamran.k"       /* secure_zero */
#include <string.h>

/* Per-record nonce (RFC 8446 §5.3): the 64-bit sequence number, left-padded with
 * four zero bytes to the IV length, XORed with the static write_iv. */
static void record_nonce(const uint8_t iv[TLS_RECORD_IV_LEN], uint64_t seq,
                         uint8_t nonce[TLS_RECORD_IV_LEN]) {
    uint8_t seq_be[TLS_RECORD_IV_LEN];
    int i;
    memset(seq_be, 0, 4);
    for (i = 0; i < 8; i++) {
        seq_be[4 + i] = (uint8_t)(seq >> (56 - 8 * i));
    }
    for (i = 0; i < TLS_RECORD_IV_LEN; i++) {
        nonce[i] = iv[i] ^ seq_be[i];
    }
}

/* The inner ContentType of a TLS 1.3 protected record is alert, handshake, or
 * application_data. change_cipher_spec is only ever an unprotected record, and
 * other values are invalid — reject them either way. */
static int is_valid_inner_type(uint8_t t) {
    return t == TLS_CONTENT_ALERT ||
           t == TLS_CONTENT_HANDSHAKE ||
           t == TLS_CONTENT_APPLICATION_DATA;
}

int tls_record_seal(const uint8_t key[TLS_RECORD_KEY_LEN],
                    const uint8_t iv[TLS_RECORD_IV_LEN], uint64_t seq,
                    uint8_t content_type, const uint8_t *content, size_t content_len,
                    size_t padding_len, uint8_t *out, size_t out_cap, size_t *out_len) {
    uint8_t nonce[TLS_RECORD_IV_LEN];
    size_t inner_len, enc_len, total;

    if (key == NULL || iv == NULL || out == NULL || out_len == NULL) {
        return 0;
    }
    if (content == NULL && content_len != 0) {
        return 0;
    }
    if (!is_valid_inner_type(content_type)) {
        return 0;
    }
    /* RFC 8446 §5.1 caps the plaintext content at 2^14; bound padding too so the
     * additions below cannot overflow, then enforce the §5.2 ciphertext limit. */
    if (content_len > TLS_RECORD_MAX_PLAINTEXT || padding_len > TLS_RECORD_MAX_CIPHERTEXT) {
        return 0;
    }
    inner_len = content_len + 1 + padding_len;         /* content || type || zeros */
    enc_len = inner_len + TLS_RECORD_TAG_LEN;
    if (enc_len > TLS_RECORD_MAX_CIPHERTEXT) {
        return 0;
    }
    total = TLS_RECORD_HEADER_LEN + enc_len;
    if (total > out_cap) {
        return 0;
    }

    /* Record header, which is also the AEAD additional data:
     * opaque_type(23) || legacy_record_version(0x0303) || length. */
    out[0] = 0x17;
    out[1] = 0x03;
    out[2] = 0x03;
    out[3] = (uint8_t)(enc_len >> 8);
    out[4] = (uint8_t)(enc_len & 0xff);

    /* Build TLSInnerPlaintext in place where the ciphertext will go, then seal it
     * in place (the AEAD's ChaCha20 layer supports pt == ct). */
    if (content_len) {
        memcpy(out + TLS_RECORD_HEADER_LEN, content, content_len);
    }
    out[TLS_RECORD_HEADER_LEN + content_len] = content_type;
    if (padding_len) {
        memset(out + TLS_RECORD_HEADER_LEN + content_len + 1, 0, padding_len);
    }

    record_nonce(iv, seq, nonce);
    if (!chacha20poly1305_seal(key, nonce, out, TLS_RECORD_HEADER_LEN,
                               out + TLS_RECORD_HEADER_LEN, inner_len,
                               out + TLS_RECORD_HEADER_LEN,
                               out + TLS_RECORD_HEADER_LEN + inner_len)) {
        secure_zero(out, total);           /* leave no partial plaintext/ciphertext */
        secure_zero(nonce, sizeof nonce);
        return 0;
    }
    secure_zero(nonce, sizeof nonce);
    *out_len = total;
    return 1;
}

int tls_record_open(const uint8_t key[TLS_RECORD_KEY_LEN],
                    const uint8_t iv[TLS_RECORD_IV_LEN], uint64_t seq,
                    const uint8_t *record, size_t record_len,
                    uint8_t *out, size_t out_cap,
                    size_t *content_len, uint8_t *content_type) {
    uint8_t nonce[TLS_RECORD_IV_LEN];
    size_t enc_len, inner_len, i, type_pos;

    if (key == NULL || iv == NULL || record == NULL || out == NULL ||
        content_len == NULL || content_type == NULL) {
        return 0;
    }
    /* A record is a 5-byte header plus an encrypted_record that holds at least the
     * 16-byte tag. */
    if (record_len < TLS_RECORD_HEADER_LEN + TLS_RECORD_TAG_LEN) {
        return 0;
    }
    enc_len = record_len - TLS_RECORD_HEADER_LEN;
    /* Header: opaque_type(23), legacy_record_version(0x0303), and a length that
     * matches the actual encrypted_record. */
    if (record[0] != 0x17 || record[1] != 0x03 || record[2] != 0x03) {
        return 0;
    }
    if ((((size_t)record[3] << 8) | record[4]) != enc_len) {
        return 0;
    }
    if (enc_len > TLS_RECORD_MAX_CIPHERTEXT) {
        return 0;
    }

    inner_len = enc_len - TLS_RECORD_TAG_LEN;
    if (inner_len > out_cap) {
        return 0;                          /* need room for the whole inner plaintext */
    }

    record_nonce(iv, seq, nonce);
    /* Authenticate over header || ciphertext and, only if authentic, decrypt into
     * `out`. The AEAD releases no plaintext when the tag does not verify. */
    if (!chacha20poly1305_open(key, nonce, record, TLS_RECORD_HEADER_LEN,
                               record + TLS_RECORD_HEADER_LEN, inner_len,
                               record + TLS_RECORD_HEADER_LEN + inner_len, out)) {
        secure_zero(nonce, sizeof nonce);
        return 0;
    }
    secure_zero(nonce, sizeof nonce);

    /* De-pad (RFC 8446 §5.4): the ContentType is the last non-zero octet. Scan all
     * bytes without an early exit so the timing does not reveal the padding
     * length. type_pos ends as the 1-based index of the last non-zero byte, 0 if
     * the inner plaintext is entirely zero. */
    type_pos = 0;
    for (i = 0; i < inner_len; i++) {
        uint8_t b = out[i];
        /* nz = 1 if b != 0, else 0 (branchless). */
        size_t nz = (size_t)((b | (uint8_t)(0u - b)) >> 7);
        size_t mask = (size_t)0 - nz;      /* all ones if nz, else 0 */
        type_pos = (type_pos & ~mask) | ((i + 1) & mask);
    }
    if (type_pos == 0) {
        secure_zero(out, inner_len);       /* all-zero inner: no ContentType */
        return 0;
    }

    {
        uint8_t type = out[type_pos - 1];
        size_t len = type_pos - 1;         /* content precedes the type byte */
        /* Reject a malformed inner plaintext: an illegal ContentType or content
         * exceeding the 2^14 plaintext limit. Outputs are set only on success. */
        if (!is_valid_inner_type(type) || len > TLS_RECORD_MAX_PLAINTEXT) {
            secure_zero(out, inner_len);
            return 0;
        }
        *content_type = type;
        *content_len = len;                /* content is in place at out[0..len) */
    }
    return 1;
}

#endif /* WEBLIB_TLS */
