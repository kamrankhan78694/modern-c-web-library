/*
 * server_handshake.c — TLS 1.3 server handshake state machine (RFC 8446 §4).
 * EXPERIMENTAL / UNAUDITED. See server_handshake.h.
 *
 * Compiled only under -DWEBLIB_ENABLE_TLS=ON. This is the orchestration layer that
 * sequences the verified primitives into a live 1-RTT handshake (with an optional
 * HelloRetryRequest round trip). The security properties it must uphold — none of
 * which the primitives can enforce on their own — are:
 *
 *   1. Sequencing: an explicit phase gates every entry point (a message is accepted
 *      in exactly one phase, then the phase advances and can never move back). At
 *      most one HelloRetryRequest is ever sent — WAIT_CH2 leads only to
 *      WAIT_FINISHED or FAILED, never back to START.
 *   2. Authentication: DONE is reached, and application keys released, only after
 *      the client Finished deprotects and its verify_data matches in constant time.
 *   3. Fail-closed: every failure latches a terminal FAILED phase, records the alert
 *      to send, and wipes all secret material before returning.
 *   4. Contributory behaviour: an all-zero X25519 shared secret is rejected
 *      (RFC 8446 §7.4.2).
 *   5. HRR transcript integrity: on the HRR path the first ClientHello is replaced
 *      by the synthetic message_hash (RFC 8446 §4.4.1) and CH1+HRR+CH2 are all bound
 *      into the transcript, so a tampered second ClientHello is fatal at Finished.
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

/*
 * Shared tail of both ClientHello paths. On entry `transcript` already contains
 * the ClientHello(s) (a single CH1, or the synthetic message_hash + HRR + CH2). We
 * perform the X25519 agreement with the chosen client share, write the ServerHello
 * record + protected flight into `out`, absorb each message into `transcript`,
 * derive the application keys and the expected client Finished, and advance to
 * WAIT_FINISHED. Returns 1 on success; on failure returns 0 and writes the alert to
 * *alert (the caller latches FAILED via fail()). All secret scratch is wiped here
 * regardless of outcome; the persistent hs->* keys written on success are wiped by
 * the caller's fail() on the failure path.
 */
static int emit_server_flight(tls_server_hs_t *hs, const tls_server_config_t *cfg,
                              tls_transcript_t *transcript,
                              const uint8_t client_share[32],
                              const uint8_t *session_id, size_t session_id_len,
                              uint8_t *out, size_t out_cap, size_t *out_len,
                              uint8_t *alert) {
    uint8_t ecdhe[32];
    uint8_t server_pub[32];                        /* public */
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
    uint8_t local_alert = TLS_ALERT_INTERNAL_ERROR;
    int ok = 0;

    memset(zero32, 0, sizeof zero32);

    /* 1. X25519 key agreement, then the RFC 8446 §7.4.2 contributory-behaviour
     * check: reject an all-zero shared secret (a degenerate / small-order peer
     * key). Constant-time over the secret. */
    x25519(ecdhe, cfg->server_eph_sk, client_share);
    if (all_zero(ecdhe, sizeof ecdhe)) {
        local_alert = TLS_ALERT_ILLEGAL_PARAMETER;
        goto done;
    }
    x25519_base(server_pub, cfg->server_eph_sk);

    /* 2. Build the ServerHello directly into `out` behind a 5-byte record header,
     * then backfill the header, and absorb it into the transcript (which already
     * carries the ClientHello context). */
    if (out_cap < TLS_RECORD_HEADER_LEN) {
        goto done;   /* internal_error */
    }
    tls_writer_init(&w, out + TLS_RECORD_HEADER_LEN, out_cap - TLS_RECORD_HEADER_LEN);
    if (!tls_build_server_hello(&w, cfg->server_random, session_id, session_id_len,
                                server_pub) ||
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
    tls_transcript_update(transcript, out + TLS_RECORD_HEADER_LEN, sh_len);
    out_used = TLS_RECORD_HEADER_LEN + sh_len;

    /* 3. Key schedule through the handshake secret (RFC 8446 §7.1). */
    tls13_extract(NULL, 0, zero32, sizeof zero32, early_secret);   /* no PSK */
    tls13_empty_transcript_hash(empty_hash);
    if (!tls13_derive_secret(early_secret, "derived", empty_hash, derived)) {
        goto done;
    }
    tls13_extract(derived, sizeof derived, ecdhe, sizeof ecdhe, handshake_secret);

    tls_transcript_current(transcript, th);   /* TH(.. || ServerHello) */
    if (!tls13_derive_secret(handshake_secret, "s hs traffic", th, s_hs_secret) ||
        !tls13_derive_secret(handshake_secret, "c hs traffic", th, c_hs_secret) ||
        !tls13_traffic_keys(s_hs_secret, server_hs_key, sizeof server_hs_key, server_hs_iv) ||
        !tls13_traffic_keys(c_hs_secret, hs->client_hs_key, sizeof hs->client_hs_key,
                            hs->client_hs_iv) ||
        !tls13_finished_key(s_hs_secret, s_finished_key) ||
        !tls13_finished_key(c_hs_secret, c_finished_key)) {
        goto done;
    }

    /* 4. Assemble the server flight, absorbing each message into the transcript in
     * order. CertificateVerify signs the transcript through Certificate; the server
     * Finished MACs the transcript through CertificateVerify. Each message is built
     * with its own writer at a running offset so its exact bytes are known. */
    /* EncryptedExtensions */
    tls_writer_init(&w, flight, sizeof flight);
    if (!tls_build_encrypted_extensions(&w) || !tls_writer_finish(&w, &mlen)) {
        goto done;
    }
    tls_transcript_update(transcript, flight, mlen);
    flight_len = mlen;
    /* Certificate */
    tls_writer_init(&w, flight + flight_len, sizeof flight - flight_len);
    if (!tls_build_certificate(&w, cfg->cert_der, cfg->cert_len) ||
        !tls_writer_finish(&w, &mlen)) {
        goto done;   /* certificate too large for the flight buffer */
    }
    tls_transcript_update(transcript, flight + flight_len, mlen);
    flight_len += mlen;
    /* CertificateVerify — sign TH(.. .. Certificate). */
    tls_transcript_current(transcript, th);
    tls_sign_server_cert_verify(cfg->ed25519_seed, cfg->ed25519_pub, th, cv_sig);
    tls_writer_init(&w, flight + flight_len, sizeof flight - flight_len);
    if (!tls_build_certificate_verify(&w, cv_sig) || !tls_writer_finish(&w, &mlen)) {
        goto done;
    }
    tls_transcript_update(transcript, flight + flight_len, mlen);
    flight_len += mlen;
    /* Finished — MAC TH(.. .. CertificateVerify) with the server finished key. */
    tls_transcript_current(transcript, th);
    tls_finished_verify_data(s_finished_key, th, verify_data);
    tls_writer_init(&w, flight + flight_len, sizeof flight - flight_len);
    if (!tls_build_finished(&w, verify_data) || !tls_writer_finish(&w, &mlen)) {
        goto done;
    }
    tls_transcript_update(transcript, flight + flight_len, mlen);
    flight_len += mlen;

    /* 5. Application traffic keys and the expected client Finished, both over
     * TH(.. .. server Finished) (RFC 8446 §7.1, §4.4.4). */
    tls_transcript_current(transcript, th);
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

    /* 6. Seal the flight as one protected record (server handshake key, seq 0),
     * appended after the ServerHello record. */
    if (!tls_record_seal(server_hs_key, server_hs_iv, 0, TLS_CONTENT_HANDSHAKE,
                         flight, flight_len, 0, out + out_used, out_cap - out_used,
                         &rec_len)) {
        goto done;   /* flight too large or `out` too small -> internal_error */
    }
    out_used += rec_len;

    *out_len = out_used;
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
    /* th, cv_sig, server_pub, empty_hash are non-secret (hashes of and signatures
     * over public handshake messages); left as-is. */

    if (!ok) {
        *alert = local_alert;
    }
    return ok;
}

/*
 * HelloRetryRequest path (RFC 8446 §4.1.4). The ClientHello offered X25519 in
 * supported_groups but sent no X25519 key_share; ask for one. Writes the HRR record
 * to `out`, performs the §4.4.1 transcript rewrite (message_hash(Hash(CH1)) || HRR)
 * into hs->transcript, remembers what CH2 must echo unchanged, and advances to
 * WAIT_CH2. No key material exists yet (the key agreement waits for CH2's share).
 */
static int send_hello_retry_request(tls_server_hs_t *hs, const tls_client_hello_t *ch,
                                    const uint8_t *ch_msg, size_t ch_len,
                                    uint8_t *out, size_t out_cap, size_t *out_len) {
    tls_transcript_t tmp;
    tls_writer_t w;
    uint8_t ch1_hash[32];
    size_t hrr_len = 0;

    /* Build the HRR message into `out` behind a 5-byte record header. */
    if (out_cap < TLS_RECORD_HEADER_LEN) {
        return fail(hs, TLS_ALERT_INTERNAL_ERROR);
    }
    tls_writer_init(&w, out + TLS_RECORD_HEADER_LEN, out_cap - TLS_RECORD_HEADER_LEN);
    if (!tls_build_hello_retry_request(&w, ch->session_id, ch->session_id_len) ||
        !tls_writer_finish(&w, &hrr_len)) {
        return fail(hs, TLS_ALERT_INTERNAL_ERROR);
    }
    if (hrr_len > TLS_RECORD_MAX_PLAINTEXT) {
        return fail(hs, TLS_ALERT_INTERNAL_ERROR);
    }
    out[0] = TLS_CONTENT_HANDSHAKE;
    out[1] = 0x03;
    out[2] = 0x03;
    out[3] = (uint8_t)(hrr_len >> 8);
    out[4] = (uint8_t)(hrr_len & 0xff);

    /* Transcript rewrite (RFC 8446 §4.4.1): the first ClientHello is replaced by
     * the synthetic message_hash(Hash(CH1)); then the HRR is absorbed. `tmp` is a
     * throwaway used only to take Hash(CH1). */
    tls_transcript_init(&tmp);
    tls_transcript_update(&tmp, ch_msg, ch_len);
    tls_transcript_current(&tmp, ch1_hash);
    tls_transcript_reinit_after_hrr(&hs->transcript, ch1_hash);
    tls_transcript_update(&hs->transcript, out + TLS_RECORD_HEADER_LEN, hrr_len);

    /* Remember what the second ClientHello must echo unchanged (RFC 8446 §4.1.2).
     * session_id_len is <=32 (enforced by the parser). */
    memcpy(hs->ch1_random, ch->random, 32);
    if (ch->session_id_len > 0) {
        memcpy(hs->ch1_session_id, ch->session_id, ch->session_id_len);
    }
    hs->ch1_session_id_len = (uint8_t)ch->session_id_len;

    *out_len = TLS_RECORD_HEADER_LEN + hrr_len;
    hs->phase = TLS_SERVER_HS_WAIT_CH2;
    hs->alert = 0;
    return 1;
}

/* Validate the offered parameters common to both ClientHellos: TLS 1.3 and our one
 * cipher / signature / group must all be on offer. Returns 1 if acceptable; on a
 * mismatch returns 0 having latched FAILED with the RFC 8446 §6 alert. */
static int require_profile(tls_server_hs_t *hs, const tls_client_hello_t *ch) {
    if (!ch->offers_tls13) {
        return fail(hs, TLS_ALERT_PROTOCOL_VERSION);   /* no TLS 1.3 offered */
    }
    if (!ch->offers_chacha20_poly1305 || !ch->offers_ed25519 || !ch->offers_x25519) {
        /* No common cipher / signature / group. Without X25519 even offered there is
         * no group to retry with, so this is terminal (not HRR-eligible). */
        return fail(hs, TLS_ALERT_HANDSHAKE_FAILURE);
    }
    return 1;
}

/* Phase START: the first ClientHello. Either complete the 1-RTT flight, or (if the
 * X25519 key_share is absent) send a HelloRetryRequest. */
static int handle_client_hello_1(tls_server_hs_t *hs, const tls_server_config_t *cfg,
                                 const uint8_t *ch_msg, size_t ch_len,
                                 uint8_t *out, size_t out_cap, size_t *out_len) {
    tls_client_hello_t ch;
    tls_transcript_t transcript;
    uint8_t alert = TLS_ALERT_INTERNAL_ERROR;

    if (!tls_parse_client_hello(ch_msg, ch_len, &ch)) {
        return fail(hs, TLS_ALERT_DECODE_ERROR);
    }
    if (!require_profile(hs, &ch)) {
        return 0;   /* require_profile already latched FAILED + alert */
    }
    if (ch.x25519_key_share == NULL) {
        /* X25519 offered but no share supplied -> HelloRetryRequest. */
        return send_hello_retry_request(hs, &ch, ch_msg, ch_len, out, out_cap, out_len);
    }
    /* Normal 1-RTT: transcript = {ClientHello}, then the server flight. */
    tls_transcript_init(&transcript);
    tls_transcript_update(&transcript, ch_msg, ch_len);
    if (!emit_server_flight(hs, cfg, &transcript, ch.x25519_key_share,
                            ch.session_id, ch.session_id_len, out, out_cap, out_len,
                            &alert)) {
        return fail(hs, alert);
    }
    return 1;
}

/* Phase WAIT_CH2: the second ClientHello after our HelloRetryRequest. It must now
 * carry the X25519 key_share and be consistent with the first (RFC 8446 §4.1.2). We
 * never answer a still-share-less CH2 with a second HRR (§4.1.4) — we abort. */
static int handle_client_hello_2(tls_server_hs_t *hs, const tls_server_config_t *cfg,
                                 const uint8_t *ch_msg, size_t ch_len,
                                 uint8_t *out, size_t out_cap, size_t *out_len) {
    tls_client_hello_t ch;
    uint8_t alert = TLS_ALERT_INTERNAL_ERROR;

    if (!tls_parse_client_hello(ch_msg, ch_len, &ch)) {
        return fail(hs, TLS_ALERT_DECODE_ERROR);
    }
    if (!require_profile(hs, &ch)) {
        return 0;
    }
    if (ch.x25519_key_share == NULL) {
        /* The client ignored the retry request. Do not loop; abort. */
        return fail(hs, TLS_ALERT_ILLEGAL_PARAMETER);
    }
    /* Consistency with CH1 (the subset of RFC 8446 §4.1.2 we enforce): the Random
     * and legacy_session_id must be unchanged. These are public, so a plain compare
     * is fine. The transcript binds CH1+HRR+CH2 regardless, making any on-the-wire
     * change fatal at the client Finished. */
    if (memcmp(ch.random, hs->ch1_random, 32) != 0) {
        return fail(hs, TLS_ALERT_ILLEGAL_PARAMETER);
    }
    if (ch.session_id_len != hs->ch1_session_id_len ||
        (ch.session_id_len > 0 &&
         memcmp(ch.session_id, hs->ch1_session_id, ch.session_id_len) != 0)) {
        return fail(hs, TLS_ALERT_ILLEGAL_PARAMETER);
    }

    /* Continue the transcript with CH2 (message_hash(CH1) + HRR already absorbed),
     * then complete the flight. */
    tls_transcript_update(&hs->transcript, ch_msg, ch_len);
    if (!emit_server_flight(hs, cfg, &hs->transcript, ch.x25519_key_share,
                            ch.session_id, ch.session_id_len, out, out_cap, out_len,
                            &alert)) {
        return fail(hs, alert);
    }
    /* The HRR round-trip scratch is no longer needed. */
    memset(&hs->transcript, 0, sizeof hs->transcript);
    secure_zero(hs->ch1_random, sizeof hs->ch1_random);
    secure_zero(hs->ch1_session_id, sizeof hs->ch1_session_id);
    hs->ch1_session_id_len = 0;
    return 1;
}

int tls_server_hs_read_client_hello(tls_server_hs_t *hs,
                                     const tls_server_config_t *cfg,
                                     const uint8_t *ch_msg, size_t ch_len,
                                     uint8_t *out, size_t out_cap, size_t *out_len) {
    if (out_len != NULL) {
        *out_len = 0;
    }
    if (hs == NULL) {
        return 0;
    }
    if (cfg == NULL || ch_msg == NULL || out == NULL || out_len == NULL ||
        cfg->cert_der == NULL || cfg->ed25519_seed == NULL || cfg->ed25519_pub == NULL ||
        cfg->server_eph_sk == NULL || cfg->server_random == NULL) {
        /* out_len is required: a caller must always learn the response length so it
         * never sends an unknown/unbounded number of bytes (cf. tls_record_seal). */
        return fail(hs, TLS_ALERT_INTERNAL_ERROR);
    }

    /* Phase gate: a ClientHello is accepted in START (the first) or WAIT_CH2 (the
     * second, after our HRR). Anything else — a replay after we advanced, or a call
     * on a FAILED handshake — is a protocol violation. */
    if (hs->phase == TLS_SERVER_HS_START) {
        return handle_client_hello_1(hs, cfg, ch_msg, ch_len, out, out_cap, out_len);
    }
    if (hs->phase == TLS_SERVER_HS_WAIT_CH2) {
        return handle_client_hello_2(hs, cfg, ch_msg, ch_len, out, out_cap, out_len);
    }
    return fail(hs, TLS_ALERT_UNEXPECTED_MESSAGE);
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
    /* HRR round-trip scratch (non-secret, but reset for a clean discard). */
    memset(&hs->transcript, 0, sizeof hs->transcript);
    secure_zero(hs->ch1_random, sizeof hs->ch1_random);
    secure_zero(hs->ch1_session_id, sizeof hs->ch1_session_id);
    hs->ch1_session_id_len = 0;
    hs->phase = TLS_SERVER_HS_FAILED;
    hs->alert = 0;
}

#endif /* WEBLIB_TLS */
