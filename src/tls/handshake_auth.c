/*
 * handshake_auth.c — TLS 1.3 handshake authentication crypto (RFC 8446 §4.4).
 * EXPERIMENTAL / UNAUDITED. See handshake_auth.h.
 *
 * Compiled only under -DWEBLIB_ENABLE_TLS=ON. Thin composition of verified
 * primitives: the transcript hash clones a running SHA-256 so the digest of the
 * messages so far can be taken without ending the stream; the Finished MAC is
 * HMAC-SHA256; the CertificateVerify signature is Ed25519 over the RFC-defined
 * content preamble.
 */
#include "handshake_auth.h"

#ifdef WEBLIB_TLS

#include "crypto/sha256.h"
#include "ed25519.h"
#include <string.h>

/* Server CertificateVerify content preamble (RFC 8446 §4.4.3). Lengths are
 * derived from the label literal and the hash size so the layout can't silently
 * drift if either changes. */
#define CV_CONTEXT     "TLS 1.3, server CertificateVerify"
#define CV_CONTEXT_LEN (sizeof(CV_CONTEXT) - 1)             /* label length, sans NUL */
#define CV_PAD_LEN     64                                   /* octet 0x20 * 64 (fixed) */
#define CV_CONTENT_LEN (CV_PAD_LEN + CV_CONTEXT_LEN + 1 + SHA256_DIGEST_SIZE)  /* = 130 */

void tls_transcript_init(tls_transcript_t *t) {
    sha256_init(&t->sha);
}

void tls_transcript_update(tls_transcript_t *t, const uint8_t *msg, size_t len) {
    if (len > 0) {
        sha256_update(&t->sha, msg, len);
    }
}

void tls_transcript_current(const tls_transcript_t *t, uint8_t out[32]) {
    /* Clone the running context and finalize the copy, so the caller's transcript
     * keeps accumulating further messages. */
    sha256_ctx_t snapshot = t->sha;
    sha256_final(&snapshot, out);
}

void tls_transcript_reinit_after_hrr(tls_transcript_t *t, const uint8_t ch1_hash[32]) {
    /* message_hash(254) || 00 00 20 (uint24 length = SHA-256 size) || Hash(CH1). */
    uint8_t synthetic[4 + SHA256_DIGEST_SIZE];
    if (t == NULL || ch1_hash == NULL) {
        return;
    }
    synthetic[0] = 254;                    /* HandshakeType message_hash */
    synthetic[1] = 0x00;
    synthetic[2] = 0x00;
    synthetic[3] = (uint8_t)SHA256_DIGEST_SIZE;   /* 32 */
    memcpy(synthetic + 4, ch1_hash, SHA256_DIGEST_SIZE);
    tls_transcript_init(t);
    tls_transcript_update(t, synthetic, sizeof synthetic);
}

void tls_finished_verify_data(const uint8_t finished_key[32],
                              const uint8_t transcript_hash[32],
                              uint8_t out[32]) {
    hmac_sha256(finished_key, 32, transcript_hash, 32, out);
}

/* Build the 130-byte content signed/verified in a server CertificateVerify. The
 * bytes are public (derived from the transcript), so no wiping is needed. */
static void cert_verify_content(const uint8_t transcript_hash[32],
                                uint8_t content[CV_CONTENT_LEN]) {
    memset(content, 0x20, CV_PAD_LEN);
    memcpy(content + CV_PAD_LEN, CV_CONTEXT, CV_CONTEXT_LEN);
    content[CV_PAD_LEN + CV_CONTEXT_LEN] = 0x00;
    memcpy(content + CV_PAD_LEN + CV_CONTEXT_LEN + 1, transcript_hash, SHA256_DIGEST_SIZE);
}

void tls_sign_server_cert_verify(const uint8_t seed[32], const uint8_t pubkey[32],
                                 const uint8_t transcript_hash[32], uint8_t sig[64]) {
    uint8_t content[CV_CONTENT_LEN];
    cert_verify_content(transcript_hash, content);
    ed25519_sign(sig, content, sizeof content, seed, pubkey);
}

int tls_verify_server_cert_verify(const uint8_t pubkey[32],
                                  const uint8_t transcript_hash[32],
                                  const uint8_t sig[64]) {
    uint8_t content[CV_CONTENT_LEN];
    cert_verify_content(transcript_hash, content);
    return ed25519_verify(sig, content, sizeof content, pubkey);
}

#endif /* WEBLIB_TLS */
