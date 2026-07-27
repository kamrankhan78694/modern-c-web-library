/*
 * handshake_auth.h — TLS 1.3 handshake authentication crypto (RFC 8446 §4.4).
 * EXPERIMENTAL / UNAUDITED.
 *
 * Part of the experimental pure-C TLS layer; compiled only under
 * -DWEBLIB_ENABLE_TLS=ON. The three handshake-specific computations the state
 * machine performs over the running transcript: the transcript hash itself
 * (§4.4.1), the Finished MAC (§4.4.4), and the server CertificateVerify signature
 * (§4.4.3). Built on SHA-256/HMAC (crypto/sha256) and Ed25519 (ed25519).
 * Verified against an independent reference in tests/test_tls_crypto.c.
 */
#ifndef WEBLIB_TLS_HANDSHAKE_AUTH_H
#define WEBLIB_TLS_HANDSHAKE_AUTH_H

#ifdef WEBLIB_TLS

#include <stddef.h>
#include <stdint.h>
#include "crypto/sha256.h"   /* sha256_ctx_t */

/*
 * Running handshake transcript hash (RFC 8446 §4.4.1): a SHA-256 over the
 * concatenation of the handshake messages exchanged so far, each including its
 * 4-byte header.
 */
typedef struct {
    sha256_ctx_t sha;
} tls_transcript_t;

void tls_transcript_init(tls_transcript_t *t);

/* Absorb one complete handshake message (header included). */
void tls_transcript_update(tls_transcript_t *t, const uint8_t *msg, size_t len);

/* Write the transcript hash of the messages absorbed so far to `out` (32 bytes),
 * without disturbing the running state — more messages may follow. */
void tls_transcript_current(const tls_transcript_t *t, uint8_t out[32]);

/*
 * Re-initialize a transcript for the HelloRetryRequest case (RFC 8446 §4.4.1).
 * When the server sends a HelloRetryRequest, the first ClientHello is *replaced*
 * in the transcript by a synthetic "message_hash" handshake message:
 *
 *     Hash(message_hash(254) || uint24(Hash.length) || Hash(ClientHello1) || ...)
 *
 * This resets `t` and absorbs exactly that synthetic message from `ch1_hash` =
 * Hash(ClientHello1). The caller then absorbs the HelloRetryRequest, the second
 * ClientHello, and the rest of the flight as usual. Getting this rewrite wrong is
 * a classic TLS-1.3 pitfall, so it is isolated here and pinned by a known-answer
 * test against an independent oracle.
 */
void tls_transcript_reinit_after_hrr(tls_transcript_t *t, const uint8_t ch1_hash[32]);

/*
 * Finished.verify_data (RFC 8446 §4.4.4) = HMAC-SHA256(finished_key,
 * transcript_hash), where finished_key is derived from a Base traffic secret via
 * the key schedule. Writes 32 bytes.
 */
void tls_finished_verify_data(const uint8_t finished_key[32],
                              const uint8_t transcript_hash[32],
                              uint8_t out[32]);

/*
 * Sign a server CertificateVerify (RFC 8446 §4.4.3): Ed25519 over the content
 *   0x20 * 64 || "TLS 1.3, server CertificateVerify" || 0x00 || transcript_hash
 * with the server's Ed25519 key (`seed` + its `pubkey`); writes the 64-byte
 * signature to `sig`.
 */
void tls_sign_server_cert_verify(const uint8_t seed[32], const uint8_t pubkey[32],
                                 const uint8_t transcript_hash[32], uint8_t sig[64]);

/* Verify a server CertificateVerify signature. Returns 1 if valid, else 0. */
int tls_verify_server_cert_verify(const uint8_t pubkey[32],
                                  const uint8_t transcript_hash[32],
                                  const uint8_t sig[64]);

#endif /* WEBLIB_TLS */
#endif /* WEBLIB_TLS_HANDSHAKE_AUTH_H */
