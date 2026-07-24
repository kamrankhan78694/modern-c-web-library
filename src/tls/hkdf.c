/*
 * hkdf.c — HKDF-SHA256 (RFC 5869) and TLS 1.3 HKDF-Expand-Label (RFC 8446 §7.1).
 * EXPERIMENTAL / UNAUDITED.
 *
 * Compiled only under -DWEBLIB_ENABLE_TLS=ON. Built on the shared SHA-256 /
 * HMAC-SHA256 primitive. Verified against RFC 5869 §A.1/A.3 and the RFC 8448
 * TLS 1.3 trace in tests/test_tls_crypto.c.
 */
#include "hkdf.h"

#ifdef WEBLIB_TLS

#include "crypto/sha256.h"   /* hmac_sha256, SHA256_DIGEST_SIZE */
#include "kamran.k"          /* secure_zero */
#include <stdlib.h>
#include <string.h>

void hkdf_extract(const uint8_t *salt, size_t salt_len,
                  const uint8_t *ikm, size_t ikm_len,
                  uint8_t prk[HKDF_SHA256_LEN]) {
    /* PRK = HMAC(salt-as-key, ikm-as-message). A NULL/empty salt is HMAC-padded to
     * a block of zeros, matching RFC 5869's "HashLen zero bytes" default. */
    hmac_sha256(salt, salt_len, ikm, ikm_len, prk);
}

int hkdf_expand(const uint8_t prk[HKDF_SHA256_LEN],
                const uint8_t *info, size_t info_len,
                uint8_t *okm, size_t okm_len) {
    uint8_t t[HKDF_SHA256_LEN];
    uint8_t *buf;
    size_t buf_len;
    size_t t_len = 0;   /* length of the previous T block (0 on the first round) */
    size_t done = 0;
    size_t i;

    /* A NULL info is only valid with info_len 0 (otherwise the memcpy below would
     * dereference NULL). */
    if (info == NULL && info_len > 0) {
        return 0;
    }
    /* RFC 5869: L <= 255 * HashLen. */
    if (okm_len > (size_t)255 * HKDF_SHA256_LEN) {
        return 0;
    }
    if (okm_len == 0) {
        return 1;
    }
    /* Guard the scratch-buffer size against size_t overflow (info_len is
     * caller-controlled); an overflow would under-size the malloc below. */
    if (info_len > SIZE_MAX - HKDF_SHA256_LEN - 1) {
        return 0;
    }

    /* Scratch for one HMAC input: T(prev) || info || counter. */
    buf_len = HKDF_SHA256_LEN + info_len + 1;
    buf = (uint8_t *)malloc(buf_len);
    if (buf == NULL) {
        return 0;
    }

    for (i = 1; done < okm_len; i++) {
        size_t off = 0;
        size_t n;

        if (t_len > 0) {
            memcpy(buf, t, t_len);
            off = t_len;
        }
        if (info_len > 0) {
            memcpy(buf + off, info, info_len);
            off += info_len;
        }
        buf[off++] = (uint8_t)i;   /* i is in 1..255 given the L bound above */

        hmac_sha256(prk, HKDF_SHA256_LEN, buf, off, t);

        n = (okm_len - done < HKDF_SHA256_LEN) ? (okm_len - done) : HKDF_SHA256_LEN;
        memcpy(okm + done, t, n);
        done += n;
        t_len = HKDF_SHA256_LEN;
    }

    /* Wipe intermediate key material. */
    secure_zero(buf, buf_len);
    secure_zero(t, sizeof(t));
    free(buf);
    return 1;
}

int hkdf_expand_label(const uint8_t secret[HKDF_SHA256_LEN],
                      const char *label, size_t label_len,
                      const uint8_t *context, size_t context_len,
                      uint8_t *out, size_t out_len) {
    static const char prefix[] = "tls13 ";   /* 6 bytes, no NUL */
    const size_t prefix_len = 6;
    size_t full_label_len = prefix_len + label_len;
    /* HkdfLabel = uint16 length || opaque label<7..255> || opaque context<0..255>. */
    uint8_t hkdf_label[2 + 1 + 255 + 1 + 255];
    size_t off = 0;

    if (out_len > 0xffff || full_label_len < 7 || full_label_len > 255 ||
        context_len > 255) {
        return 0;
    }
    /* A NULL label/context is only valid with a zero length. */
    if ((label == NULL && label_len > 0) || (context == NULL && context_len > 0)) {
        return 0;
    }

    hkdf_label[off++] = (uint8_t)(out_len >> 8);
    hkdf_label[off++] = (uint8_t)(out_len & 0xff);
    hkdf_label[off++] = (uint8_t)full_label_len;
    memcpy(hkdf_label + off, prefix, prefix_len);
    off += prefix_len;
    if (label_len > 0) {
        memcpy(hkdf_label + off, label, label_len);
        off += label_len;
    }
    hkdf_label[off++] = (uint8_t)context_len;
    if (context_len > 0) {
        memcpy(hkdf_label + off, context, context_len);
        off += context_len;
    }

    /* The HkdfLabel is public (label text + transcript-hash context), so no wipe. */
    return hkdf_expand(secret, hkdf_label, off, out, out_len);
}

#endif /* WEBLIB_TLS */
