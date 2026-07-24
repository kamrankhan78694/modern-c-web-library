/*
 * key_schedule.c — TLS 1.3 key schedule (RFC 8446 §7.1). EXPERIMENTAL / UNAUDITED.
 * See key_schedule.h.
 *
 * Compiled only under -DWEBLIB_ENABLE_TLS=ON. Thin, stateless composition of the
 * already-verified HKDF-SHA256 primitives (hkdf.c, whose HKDF-Expand-Label
 * encoding is pinned to the RFC 8448 trace) into the specific extract /
 * derive-secret / traffic-key operations RFC 8446 defines. Verified against the
 * RFC 8448 key-schedule trace in tests/test_tls_crypto.c.
 */
#include "key_schedule.h"

#ifdef WEBLIB_TLS

#include "hkdf.h"
#include "crypto/sha256.h"
#include "kamran.k"       /* secure_zero */
#include <string.h>

void tls13_extract(const uint8_t *salt, size_t salt_len,
                   const uint8_t *ikm, size_t ikm_len,
                   uint8_t prk[TLS13_SECRET_LEN]) {
    hkdf_extract(salt, salt_len, ikm, ikm_len, prk);
}

void tls13_empty_transcript_hash(uint8_t out[TLS13_SECRET_LEN]) {
    sha256((const uint8_t *)"", 0, out);
}

int tls13_derive_secret(const uint8_t secret[TLS13_SECRET_LEN], const char *label,
                        const uint8_t transcript_hash[TLS13_SECRET_LEN],
                        uint8_t out[TLS13_SECRET_LEN]) {
    if (!hkdf_expand_label(secret, label, strlen(label),
                           transcript_hash, TLS13_SECRET_LEN,
                           out, TLS13_SECRET_LEN)) {
        secure_zero(out, TLS13_SECRET_LEN);   /* no partial secret on failure */
        return 0;
    }
    return 1;
}

int tls13_traffic_keys(const uint8_t secret[TLS13_SECRET_LEN],
                       uint8_t *key, size_t key_len,
                       uint8_t iv[TLS13_IV_LEN]) {
    if (!hkdf_expand_label(secret, "key", 3, NULL, 0, key, key_len) ||
        !hkdf_expand_label(secret, "iv", 2, NULL, 0, iv, TLS13_IV_LEN)) {
        /* Never leave a partially-derived key/IV in the caller's buffers: if the
         * IV step fails after the key was written, both are zeroed. */
        secure_zero(key, key_len);
        secure_zero(iv, TLS13_IV_LEN);
        return 0;
    }
    return 1;
}

int tls13_finished_key(const uint8_t base_key[TLS13_SECRET_LEN],
                       uint8_t out[TLS13_SECRET_LEN]) {
    if (!hkdf_expand_label(base_key, "finished", 8, NULL, 0, out, TLS13_SECRET_LEN)) {
        secure_zero(out, TLS13_SECRET_LEN);
        return 0;
    }
    return 1;
}

#endif /* WEBLIB_TLS */
