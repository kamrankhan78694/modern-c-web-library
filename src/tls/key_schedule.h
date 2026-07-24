/*
 * key_schedule.h — TLS 1.3 key schedule (RFC 8446 §7.1). EXPERIMENTAL / UNAUDITED.
 *
 * Part of the experimental pure-C TLS layer; compiled only under
 * -DWEBLIB_ENABLE_TLS=ON. The primitives the handshake composes to derive every
 * secret and traffic key from the (EC)DHE shared secret and the running
 * transcript hash: HKDF-Extract, Derive-Secret, and the "key"/"iv"/"finished"
 * expansions — all over HKDF-SHA256 (hkdf.h) and SHA-256 (crypto/sha256.h).
 *
 * The transcript management and the ordering of the derivation chain belong to
 * the handshake state machine; this module is the stateless key-schedule math,
 * verified against the RFC 8448 trace in tests/test_tls_crypto.c.
 */
#ifndef WEBLIB_TLS_KEY_SCHEDULE_H
#define WEBLIB_TLS_KEY_SCHEDULE_H

#ifdef WEBLIB_TLS

#include <stddef.h>
#include <stdint.h>

#define TLS13_SECRET_LEN 32   /* SHA-256 output = the key schedule's secret size */
#define TLS13_IV_LEN     12   /* AEAD record nonce length (RFC 8446 §5.3) */

/*
 * HKDF-Extract (RFC 5869 §2.2) as used at each level of the schedule:
 * PRK = HMAC-SHA256(salt, ikm). Pass salt NULL / salt_len 0 for the all-zero
 * salt (the "no PSK" Early Secret uses a zero salt and a zero IKM).
 */
void tls13_extract(const uint8_t *salt, size_t salt_len,
                   const uint8_t *ikm, size_t ikm_len,
                   uint8_t prk[TLS13_SECRET_LEN]);

/* SHA-256 of the empty transcript — the context for Derive-Secret(., ., ""). */
void tls13_empty_transcript_hash(uint8_t out[TLS13_SECRET_LEN]);

/*
 * Derive-Secret(secret, label, transcript_hash) (RFC 8446 §7.1) =
 * HKDF-Expand-Label(secret, label, transcript_hash, 32). `transcript_hash` is the
 * 32-byte hash of the handshake messages named by the label (or the empty-string
 * hash for the "derived" steps). Returns 1 on success, 0 on failure.
 */
int tls13_derive_secret(const uint8_t secret[TLS13_SECRET_LEN], const char *label,
                        const uint8_t transcript_hash[TLS13_SECRET_LEN],
                        uint8_t out[TLS13_SECRET_LEN]);

/*
 * Per-connection traffic key and IV from a *_traffic_secret (RFC 8446 §7.3):
 *   key = HKDF-Expand-Label(secret, "key", "", key_len)   (32 for ChaCha20)
 *   iv  = HKDF-Expand-Label(secret, "iv",  "", 12)
 * Returns 1 on success, 0 on failure.
 */
int tls13_traffic_keys(const uint8_t secret[TLS13_SECRET_LEN],
                       uint8_t *key, size_t key_len,
                       uint8_t iv[TLS13_IV_LEN]);

/*
 * Finished-message MAC key = HKDF-Expand-Label(base_key, "finished", "", 32),
 * where base_key is a handshake traffic secret (RFC 8446 §4.4.4). Returns 1/0.
 */
int tls13_finished_key(const uint8_t base_key[TLS13_SECRET_LEN],
                       uint8_t out[TLS13_SECRET_LEN]);

#endif /* WEBLIB_TLS */
#endif /* WEBLIB_TLS_KEY_SCHEDULE_H */
