/*
 * server_handshake.c — TLS 1.3 server handshake state machine (RFC 8446 §4).
 * EXPERIMENTAL / UNAUDITED. See server_handshake.h.
 *
 * Compiled only under -DWEBLIB_ENABLE_TLS=ON. This is the orchestration layer that
 * sequences the verified primitives into a live 1-RTT handshake. The security
 * properties it must uphold — none of which the primitives can enforce on their own
 * — are:
 *
 *   1. Sequencing: an explicit phase gates every entry point (a message is accepted
 *      in exactly one phase, then the phase advances and can never move back).
 *   2. Authentication: DONE is reached, and application keys released, only after
 *      the client Finished deprotects and its verify_data matches in constant time.
 *   3. Fail-closed: every failure latches a terminal FAILED phase, records the alert
 *      to send, and wipes all secret material before returning.
 *   4. Contributory behaviour: an all-zero X25519 shared secret is rejected
 *      (RFC 8446 §7.4.2).
 *
 * These are implemented directly below and exercised adversarially in the tests.
 */
#include "server_handshake.h"

#ifdef WEBLIB_TLS

#include "handshake.h"
#include "handshake_auth.h"
#include "key_schedule.h"
#include "record.h"
#include "wire.h"
#include "x25519.h"
#include "kamran.k"       /* secure_zero, secure_compare */
#include <string.h>

/*
 * Upper bound on the assembled server flight {EncryptedExtensions, Certificate,
 * CertificateVerify, Finished} in plaintext. The fixed messages total ~114 bytes;
 * the rest is the certificate. 4 KiB comfortably holds any single Ed25519 or RSA
 * certificate. A larger certificate is rejected (internal_error) rather than
 * fragmented across records — see the header's scope notes.
 */
#define TLS_SERVER_FLIGHT_CAP 4096

/* Largest client Finished record plaintext (Finished is 36 bytes; the slack
 * tolerates a reasonably padded record, RFC 8446 §5.4). */
#define TLS_CLIENT_FIN_PLAINTEXT_CAP 512

/* 1 (constant-time over the buffer) if every byte of `p` is zero. */
static int all_zero(const uint8_t *p, size_t n) {
    uint8_t acc = 0;
    size_t i;
    for (i = 0; i < n; i++) {
        acc |= p[i];
    }
    return acc == 0;
}

/* Latch the terminal FAILED phase, record the alert, wipe secrets. Returns 0 so
 * callers can `return fail(hs, alert);`. */
static int fail(tls_server_hs_t *hs, uint8_t alert) {
    tls_server_hs_wipe(hs);      /* wipes key material, sets phase FAILED, alert 0 */
    hs->alert = alert;
    return 0;
}

void tls_server_hs_init(tls_server_hs_t *hs) {
    if (hs == NULL) {
        return;
    }
    memset(hs, 0, sizeof *hs);
    hs->phase = TLS_SERVER_HS_START;
    hs->alert = 0;
}

int tls_server_hs_read_client_hello(tls_server_hs_t *hs,
                                     const tls_server_config_t *cfg,
                                     const uint8_t *ch_msg, size_t ch_len,
                                     uint8_t *out, size_t out_cap, size_t *out_len) {
    /* Secret scratch — all wiped at `done` regardless of outcome. */
    tls_transcript_t transcript;
    tls_client_hello_t ch;
    uint8_t ecdhe[32];
    uint8_t server_pub[32];                       /* public */
    uint8_t zero32[32];
    uint8_t empty_hash[32];                        /* public */
    uint8_t early_secret[32], derived[32], derived2[32];
    uint8_t handshake_secret[32], master_secret[32];
    uint8_t c_hs_secret[32], s_hs_secret[32];
    uint8_t server_hs_key[32], server_hs_iv[12];
    uint8_t s_finished_key[32], c_finished_key[32];
    uint8_t c_ap_secret[32], s_ap_secret[32];
    uint8_t th[32];                                /* public transcript hashes */
    uint8_t verify_data[32];                       /* server Finished (transmitted) */
    uint8_t flight[TLS_SERVER_FLIGHT_CAP];
    uint8_t cv_sig[64];

    tls_writer_t w;
    size_t sh_len = 0, out_used = 0, flight_len = 0, rec_len = 0, mlen = 0;
    uint8_t alert = TLS_ALERT_INTERNAL_ERROR;
    int ok = 0;

    if (out_len != NULL) {
        *out_len = 0;
    }
    if (hs == NULL) {
        return 0;
    }
    /* Phase gate: ClientHello is accepted only in START. Anything else (including a
     * replay after we have advanced, or a call on a FAILED handshake) is a protocol
     * violation. */
    if (hs->phase != TLS_SERVER_HS_START) {
        return fail(hs, TLS_ALERT_UNEXPECTED_MESSAGE);
    }
    if (cfg == NULL || ch_msg == NULL || out == NULL || out_len == NULL ||
        cfg->cert_der == NULL || cfg->ed25519_seed == NULL || cfg->ed25519_pub == NULL ||
        cfg->server_eph_sk == NULL || cfg->server_random == NULL) {
        /* out_len is required: a caller must always learn the response length so it
         * never sends an unknown/unbounded number of bytes (cf. tls_record_seal). */
        return fail(hs, TLS_ALERT_INTERNAL_ERROR);
    }

    memset(zero32, 0, sizeof zero32);

    /* 1. Parse the (attacker-controlled) ClientHello. */
    if (!tls_parse_client_hello(ch_msg, ch_len, &ch)) {
        alert = TLS_ALERT_DECODE_ERROR;
        goto done;
    }

    /* 2. Require the one profile we implement. Distinct alerts per RFC 8446 §6. */
    if (!ch.offers_tls13) {
        alert = TLS_ALERT_PROTOCOL_VERSION;   /* no TLS 1.3 in supported_versions */
        goto done;
    }
    if (!ch.offers_chacha20_poly1305 || !ch.offers_ed25519 ||
        !ch.offers_x25519 || ch.x25519_key_share == NULL) {
        /* No common cipher / signature / group, or (no HRR support) no X25519 share. */
        alert = TLS_ALERT_HANDSHAKE_FAILURE;
        goto done;
    }

    /* 3. X25519 key agreement, then the RFC 8446 §7.4.2 contributory-behaviour
     * check: reject an all-zero shared secret (a degenerate / small-order peer
     * key). Constant-time over the secret. */
    x25519(ecdhe, cfg->server_eph_sk, ch.x25519_key_share);
    if (all_zero(ecdhe, sizeof ecdhe)) {
        alert = TLS_ALERT_ILLEGAL_PARAMETER;
        goto done;
    }
    x25519_base(server_pub, cfg->server_eph_sk);

    /* 4. Transcript = ClientHello, then ServerHello. Build the ServerHello directly
     * into `out` behind a 5-byte record header, then backfill the header. */
    tls_transcript_init(&transcript);
    tls_transcript_update(&transcript, ch_msg, ch_len);

    if (out_cap < TLS_RECORD_HEADER_LEN) {
        goto done;   /* internal_error */
    }
    tls_writer_init(&w, out + TLS_RECORD_HEADER_LEN, out_cap - TLS_RECORD_HEADER_LEN);
    if (!tls_build_server_hello(&w, cfg->server_random, ch.session_id,
                                ch.session_id_len, server_pub) ||
        !tls_writer_finish(&w, &sh_len)) {
        goto done;   /* output buffer too small -> internal_error */
    }
    if (sh_len > TLS_RECORD_MAX_PLAINTEXT) {
        goto done;
    }
    out[0] = TLS_CONTENT_HANDSHAKE;             /* 22 */
    out[1] = 0x03;
    out[2] = 0x03;                              /* legacy_record_version */
    out[3] = (uint8_t)(sh_len >> 8);
    out[4] = (uint8_t)(sh_len & 0xff);
    tls_transcript_update(&transcript, out + TLS_RECORD_HEADER_LEN, sh_len);
    out_used = TLS_RECORD_HEADER_LEN + sh_len;

    /* 5. Key schedule through the handshake secret (RFC 8446 §7.1). */
    tls13_extract(NULL, 0, zero32, sizeof zero32, early_secret);   /* no PSK */
    tls13_empty_transcript_hash(empty_hash);
    if (!tls13_derive_secret(early_secret, "derived", empty_hash, derived)) {
        goto done;
    }
    tls13_extract(derived, sizeof derived, ecdhe, sizeof ecdhe, handshake_secret);

    tls_transcript_current(&transcript, th);   /* TH(ClientHello || ServerHello) */
    if (!tls13_derive_secret(handshake_secret, "s hs traffic", th, s_hs_secret) ||
        !tls13_derive_secret(handshake_secret, "c hs traffic", th, c_hs_secret) ||
        !tls13_traffic_keys(s_hs_secret, server_hs_key, sizeof server_hs_key, server_hs_iv) ||
        !tls13_traffic_keys(c_hs_secret, hs->client_hs_key, sizeof hs->client_hs_key,
                            hs->client_hs_iv) ||
        !tls13_finished_key(s_hs_secret, s_finished_key) ||
        !tls13_finished_key(c_hs_secret, c_finished_key)) {
        goto done;
    }

    /* 6. Assemble the server flight, absorbing each message into the transcript in
     * order. CertificateVerify signs the transcript through Certificate; the server
     * Finished MACs the transcript through CertificateVerify. Each message is built
     * with its own writer at a running offset so its exact bytes are known. */
    /* EncryptedExtensions */
    tls_writer_init(&w, flight, sizeof flight);
    if (!tls_build_encrypted_extensions(&w) || !tls_writer_finish(&w, &mlen)) {
        goto done;
    }
    tls_transcript_update(&transcript, flight, mlen);
    flight_len = mlen;
    /* Certificate */
    tls_writer_init(&w, flight + flight_len, sizeof flight - flight_len);
    if (!tls_build_certificate(&w, cfg->cert_der, cfg->cert_len) ||
        !tls_writer_finish(&w, &mlen)) {
        goto done;   /* certificate too large for the flight buffer */
    }
    tls_transcript_update(&transcript, flight + flight_len, mlen);
    flight_len += mlen;
    /* CertificateVerify — sign TH(ClientHello .. Certificate). */
    tls_transcript_current(&transcript, th);
    tls_sign_server_cert_verify(cfg->ed25519_seed, cfg->ed25519_pub, th, cv_sig);
    tls_writer_init(&w, flight + flight_len, sizeof flight - flight_len);
    if (!tls_build_certificate_verify(&w, cv_sig) || !tls_writer_finish(&w, &mlen)) {
        goto done;
    }
    tls_transcript_update(&transcript, flight + flight_len, mlen);
    flight_len += mlen;
    /* Finished — MAC TH(ClientHello .. CertificateVerify) with the server finished key. */
    tls_transcript_current(&transcript, th);
    tls_finished_verify_data(s_finished_key, th, verify_data);
    tls_writer_init(&w, flight + flight_len, sizeof flight - flight_len);
    if (!tls_build_finished(&w, verify_data) || !tls_writer_finish(&w, &mlen)) {
        goto done;
    }
    tls_transcript_update(&transcript, flight + flight_len, mlen);
    flight_len += mlen;

    /* 7. Application traffic keys and the expected client Finished, both over
     * TH(ClientHello .. server Finished) (RFC 8446 §7.1, §4.4.4). */
    tls_transcript_current(&transcript, th);
    if (!tls13_derive_secret(handshake_secret, "derived", empty_hash, derived2)) {
        goto done;
    }
    tls13_extract(derived2, sizeof derived2, zero32, sizeof zero32, master_secret);
    if (!tls13_derive_secret(master_secret, "s ap traffic", th, s_ap_secret) ||
        !tls13_derive_secret(master_secret, "c ap traffic", th, c_ap_secret) ||
        !tls13_traffic_keys(s_ap_secret, hs->server_ap_key, sizeof hs->server_ap_key,
                            hs->server_ap_iv) ||
        !tls13_traffic_keys(c_ap_secret, hs->client_ap_key, sizeof hs->client_ap_key,
                            hs->client_ap_iv)) {
        goto done;
    }
    /* The client will MAC the same transcript with its own finished key. */
    tls_finished_verify_data(c_finished_key, th, hs->expected_client_finished);

    /* 8. Seal the flight as one protected record (server handshake key, seq 0),
     * appended after the ServerHello record. */
    if (!tls_record_seal(server_hs_key, server_hs_iv, 0, TLS_CONTENT_HANDSHAKE,
                         flight, flight_len, 0, out + out_used, out_cap - out_used,
                         &rec_len)) {
        goto done;   /* flight too large or `out` too small -> internal_error */
    }
    out_used += rec_len;

    *out_len = out_used;   /* out_len is guaranteed non-NULL (checked above) */
    hs->phase = TLS_SERVER_HS_WAIT_FINISHED;
    ok = 1;

done:
    secure_zero(ecdhe, sizeof ecdhe);
    secure_zero(early_secret, sizeof early_secret);
    secure_zero(derived, sizeof derived);
    secure_zero(derived2, sizeof derived2);
    secure_zero(handshake_secret, sizeof handshake_secret);
    secure_zero(master_secret, sizeof master_secret);
    secure_zero(c_hs_secret, sizeof c_hs_secret);
    secure_zero(s_hs_secret, sizeof s_hs_secret);
    secure_zero(server_hs_key, sizeof server_hs_key);
    secure_zero(server_hs_iv, sizeof server_hs_iv);
    secure_zero(s_finished_key, sizeof s_finished_key);
    secure_zero(c_finished_key, sizeof c_finished_key);
    secure_zero(c_ap_secret, sizeof c_ap_secret);
    secure_zero(s_ap_secret, sizeof s_ap_secret);
    secure_zero(verify_data, sizeof verify_data);
    secure_zero(flight, sizeof flight);
    /* transcript, th, cv_sig, server_pub, empty_hash are non-secret (hashes of and
     * signatures over public handshake messages); left as-is. */

    if (!ok) {
        return fail(hs, alert);   /* fail-closed: FAILED + alert + wipe hs secrets */
    }
    return 1;
}

int tls_server_hs_read_client_finished(tls_server_hs_t *hs,
                                        const uint8_t *rec, size_t rec_len) {
    uint8_t plain[TLS_CLIENT_FIN_PLAINTEXT_CAP];
    size_t content_len = 0;
    uint8_t content_type = 0;
    uint32_t body_len;
    uint8_t alert = TLS_ALERT_INTERNAL_ERROR;
    int ok = 0;

    if (hs == NULL) {
        return 0;
    }
    /* Phase gate: the client Finished is accepted only after we have sent our flight. */
    if (hs->phase != TLS_SERVER_HS_WAIT_FINISHED) {
        uint8_t a = (hs->phase == TLS_SERVER_HS_FAILED) ? hs->alert
                                                        : TLS_ALERT_UNEXPECTED_MESSAGE;
        return fail(hs, a);
    }
    if (rec == NULL) {
        return fail(hs, TLS_ALERT_UNEXPECTED_MESSAGE);
    }

    /* Deprotect with the client handshake key (its first protected record, seq 0). */
    if (!tls_record_open(hs->client_hs_key, hs->client_hs_iv, 0, rec, rec_len,
                         plain, sizeof plain, &content_len, &content_type)) {
        alert = TLS_ALERT_BAD_RECORD_MAC;      /* AEAD failure / malformed record */
        goto done;
    }
    if (content_type != TLS_CONTENT_HANDSHAKE) {
        alert = TLS_ALERT_UNEXPECTED_MESSAGE;
        goto done;
    }
    /* Exactly one Finished message: type(1) || uint24 len(==32) || 32-byte verify_data. */
    if (content_len != 4 + 32) {
        alert = TLS_ALERT_DECODE_ERROR;
        goto done;
    }
    if (plain[0] != TLS_HS_FINISHED) {
        alert = TLS_ALERT_UNEXPECTED_MESSAGE;
        goto done;
    }
    body_len = ((uint32_t)plain[1] << 16) | ((uint32_t)plain[2] << 8) | (uint32_t)plain[3];
    if (body_len != 32) {
        alert = TLS_ALERT_DECODE_ERROR;
        goto done;
    }
    /* Constant-time verify_data check. A mismatch is an invalid Finished
     * (RFC 8446 §6.2 decrypt_error), the crux of key confirmation. */
    if (!secure_compare(plain + 4, hs->expected_client_finished, 32)) {
        alert = TLS_ALERT_DECRYPT_ERROR;
        goto done;
    }
    ok = 1;

done:
    secure_zero(plain, sizeof plain);
    if (!ok) {
        return fail(hs, alert);
    }
    /* Handshake complete. The client handshake key and the expected verify_data are
     * no longer needed; only the application keys remain (released via app_keys). */
    secure_zero(hs->client_hs_key, sizeof hs->client_hs_key);
    secure_zero(hs->client_hs_iv, sizeof hs->client_hs_iv);
    secure_zero(hs->expected_client_finished, sizeof hs->expected_client_finished);
    hs->phase = TLS_SERVER_HS_DONE;
    hs->alert = 0;
    return 1;
}

int tls_server_hs_app_keys(const tls_server_hs_t *hs,
                           uint8_t server_key[32], uint8_t server_iv[12],
                           uint8_t client_key[32], uint8_t client_iv[12]) {
    if (hs == NULL || hs->phase != TLS_SERVER_HS_DONE) {
        return 0;   /* keys unreadable until the client Finished has been verified */
    }
    if (server_key != NULL) {
        memcpy(server_key, hs->server_ap_key, sizeof hs->server_ap_key);
    }
    if (server_iv != NULL) {
        memcpy(server_iv, hs->server_ap_iv, sizeof hs->server_ap_iv);
    }
    if (client_key != NULL) {
        memcpy(client_key, hs->client_ap_key, sizeof hs->client_ap_key);
    }
    if (client_iv != NULL) {
        memcpy(client_iv, hs->client_ap_iv, sizeof hs->client_ap_iv);
    }
    return 1;
}

tls_server_hs_phase_t tls_server_hs_phase(const tls_server_hs_t *hs) {
    return (hs == NULL) ? TLS_SERVER_HS_FAILED : hs->phase;
}

uint8_t tls_server_hs_alert(const tls_server_hs_t *hs) {
    return (hs == NULL) ? 0 : hs->alert;
}

void tls_server_hs_wipe(tls_server_hs_t *hs) {
    if (hs == NULL) {
        return;
    }
    secure_zero(hs->client_hs_key, sizeof hs->client_hs_key);
    secure_zero(hs->client_hs_iv, sizeof hs->client_hs_iv);
    secure_zero(hs->expected_client_finished, sizeof hs->expected_client_finished);
    secure_zero(hs->server_ap_key, sizeof hs->server_ap_key);
    secure_zero(hs->server_ap_iv, sizeof hs->server_ap_iv);
    secure_zero(hs->client_ap_key, sizeof hs->client_ap_key);
    secure_zero(hs->client_ap_iv, sizeof hs->client_ap_iv);
    hs->phase = TLS_SERVER_HS_FAILED;
    hs->alert = 0;
}

#endif /* WEBLIB_TLS */
