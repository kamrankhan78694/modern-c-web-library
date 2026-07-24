/*
 * server_handshake.h — TLS 1.3 server handshake state machine (RFC 8446 §4).
 * EXPERIMENTAL / UNAUDITED.
 *
 * Part of the experimental pure-C TLS layer; compiled only under
 * -DWEBLIB_ENABLE_TLS=ON. This is the driver that turns the verified primitives
 * (X25519, the key schedule, the record layer, the message builders/parsers and
 * the handshake-auth crypto) into a live 1-RTT TLS 1.3 handshake for the
 * TLS_CHACHA20_POLY1305_SHA256 + X25519 + Ed25519 profile.
 *
 * SECURITY NOTE. The handshake state machine — not the primitives — is where the
 * serious TLS vulnerabilities have historically lived: state-confusion attacks,
 * skipped authentication, out-of-order or replayed messages. This module is built
 * so those *classes* cannot occur rather than merely trying to avoid them:
 *
 *   - An explicit phase enum gates every entry point. Each event is accepted in
 *     exactly one phase; anything else latches a terminal FAILED phase. A message
 *     can never be processed twice and phases never move backwards.
 *   - The handshake only reaches DONE after the client's Finished has been
 *     deprotected and its verify_data matched in constant time. Application keys
 *     are unreadable until then, so key confirmation can never be skipped.
 *   - Every error path is fail-closed: it latches FAILED, records the RFC 8446
 *     alert to send, and wipes all secret material before returning.
 *   - The computed X25519 shared secret is rejected if all-zero (RFC 8446 §7.4.2),
 *     defeating small-subgroup / degenerate-key inputs.
 *
 * SCOPE / KNOWN LIMITATIONS (this phase — all documented, none silent):
 *   - Full 1-RTT handshake only. No HelloRetryRequest: a ClientHello that does not
 *     already carry an X25519 key_share is rejected with handshake_failure rather
 *     than negotiated down via HRR.
 *   - No client authentication (no CertificateRequest), no session resumption / PSK,
 *     no early data, no KeyUpdate.
 *   - The whole server flight {EncryptedExtensions, Certificate, CertificateVerify,
 *     Finished} is assembled in a 4 KiB buffer and emitted as a single protected
 *     record; a certificate too large to fit (comfortably larger than any single
 *     Ed25519 or RSA certificate) is rejected with internal_error rather than
 *     fragmented across records.
 *   - ChangeCipherSpec (RFC 8446 §D.4 middlebox compatibility) is a record-layer /
 *     transport concern: such records are never delivered to this state machine and
 *     are excluded from the handshake transcript.
 *
 * Determinism. The server's ephemeral X25519 private key and the ServerHello random
 * are supplied by the caller (tls_server_config_t) so the handshake is fully
 * reproducible under test. In production the transport wrapper MUST fill both with
 * fresh CSPRNG output (secure_random_bytes) for every handshake — reusing an
 * ephemeral key across handshakes destroys forward secrecy.
 *
 * Verified in tests/test_tls_parse.c against an independent Python (`cryptography`)
 * TLS-1.3 client oracle and a self-consistent in-process client simulation.
 */
#ifndef WEBLIB_TLS_SERVER_HANDSHAKE_H
#define WEBLIB_TLS_SERVER_HANDSHAKE_H

#ifdef WEBLIB_TLS

#include <stddef.h>
#include <stdint.h>

/* ---- alerts we may raise (RFC 8446 §6) --------------------------------- */
#define TLS_ALERT_UNEXPECTED_MESSAGE 10
#define TLS_ALERT_BAD_RECORD_MAC     20
#define TLS_ALERT_HANDSHAKE_FAILURE  40
#define TLS_ALERT_ILLEGAL_PARAMETER  47
#define TLS_ALERT_DECODE_ERROR       50
#define TLS_ALERT_DECRYPT_ERROR      51
#define TLS_ALERT_PROTOCOL_VERSION   70
#define TLS_ALERT_INTERNAL_ERROR     80

/* ---- phases ------------------------------------------------------------ */
/*
 * The single source of truth for "what will I accept now". Phases only advance
 * START -> WAIT_FINISHED -> DONE on success; any error jumps to FAILED, which is
 * terminal (every entry point refuses to do further work once FAILED).
 */
typedef enum {
    TLS_SERVER_HS_START = 0,     /* awaiting ClientHello */
    TLS_SERVER_HS_WAIT_FINISHED, /* server flight emitted; awaiting client Finished */
    TLS_SERVER_HS_DONE,          /* client Finished verified; application keys ready */
    TLS_SERVER_HS_FAILED         /* terminal error; all secrets wiped */
} tls_server_hs_phase_t;

/* ---- per-handshake configuration (borrowed by read_client_hello) ------- */
/*
 * All fields are consumed entirely within tls_server_hs_read_client_hello and are
 * only borrowed for the duration of that call — none are copied into the handshake
 * struct, so the long-term signing key never outlives the caller's control. Every
 * pointer must be non-NULL and name a buffer of the documented length.
 */
typedef struct {
    const uint8_t *cert_der;    /* server certificate, DER (a single X.509 cert) */
    size_t         cert_len;
    const uint8_t *ed25519_seed;   /* 32-byte Ed25519 private seed (signs CertificateVerify) */
    const uint8_t *ed25519_pub;    /* 32-byte Ed25519 public key matching the seed */
    const uint8_t *server_eph_sk;  /* 32-byte ephemeral X25519 private key (CSPRNG in prod) */
    const uint8_t *server_random;  /* 32-byte ServerHello random (CSPRNG in prod) */
} tls_server_config_t;

/* ---- handshake state --------------------------------------------------- */
/*
 * Opaque-by-convention: allocate one, drive it with the functions below, do not
 * read or write the fields directly. It holds only what is needed after the server
 * flight is sent — the client handshake-read key (to deprotect the client
 * Finished), the expected client Finished verify_data, and the derived application
 * traffic keys — so no key-schedule intermediate or long-term secret persists here.
 */
typedef struct {
    tls_server_hs_phase_t phase;
    uint8_t  alert;   /* RFC 8446 alert to send if phase == FAILED, else 0 */

    uint8_t  client_hs_key[32];   /* client handshake traffic key (opens client Finished) */
    uint8_t  client_hs_iv[12];
    uint8_t  expected_client_finished[32];  /* verify_data the client must present */

    uint8_t  server_ap_key[32];   /* application traffic keys, released only in DONE */
    uint8_t  server_ap_iv[12];
    uint8_t  client_ap_key[32];
    uint8_t  client_ap_iv[12];
} tls_server_hs_t;

/*
 * Initialize a handshake to phase START. Must be called before any other entry
 * point. Does not retain any pointer.
 */
void tls_server_hs_init(tls_server_hs_t *hs);

/*
 * Event 1 — process the ClientHello.
 *
 * `ch_msg`/`ch_len` is the ClientHello *handshake message* (HandshakeType ||
 * uint24 length || body), i.e. the record payload with the 5-byte record header
 * already stripped by the transport. `cfg` supplies the server identity and the
 * injected ephemeral/random.
 *
 * On success (phase START only): validates the offered parameters, performs the
 * X25519 key agreement (with the §7.4.2 all-zero check), runs the key schedule,
 * builds and transcript-absorbs the server flight, and writes the response to
 * `out` (capacity `out_cap`), setting *out_len to its length. `out` and `out_len`
 * must be non-NULL — a NULL `out_len` is rejected with internal_error so a caller
 * always learns the response length. The response is:
 *
 *     ServerHello record  (TLSPlaintext, ContentType handshake)
 *   || protected flight   (one TLSCiphertext, ContentType application_data,
 *                          sealing {EncryptedExtensions, Certificate,
 *                          CertificateVerify, Finished})
 *
 * Advances START -> WAIT_FINISHED and returns 1.
 *
 * On any failure returns 0, latches phase FAILED, sets the alert (query with
 * tls_server_hs_alert), leaves *out_len 0, and wipes every secret. Calling in any
 * phase other than START is itself a failure (unexpected_message).
 */
int tls_server_hs_read_client_hello(tls_server_hs_t *hs,
                                     const tls_server_config_t *cfg,
                                     const uint8_t *ch_msg, size_t ch_len,
                                     uint8_t *out, size_t out_cap, size_t *out_len);

/*
 * Event 2 — process the client's Finished.
 *
 * `rec`/`rec_len` is the single protected record (TLSCiphertext) the client sends
 * after the server flight. Deprotects it with the client handshake key, requires
 * it to be exactly one Finished handshake message, and compares its verify_data to
 * the expected value in constant time.
 *
 * On success (phase WAIT_FINISHED only): advances WAIT_FINISHED -> DONE and returns
 * 1; the application traffic keys become available via tls_server_hs_app_keys.
 *
 * On any failure returns 0, latches FAILED, sets the alert, and wipes every secret.
 * Distinguishes the failure via the alert: bad_record_mac (deprotection failed),
 * unexpected_message (not a handshake / not a Finished), decode_error (malformed),
 * decrypt_error (verify_data mismatch). Calling outside WAIT_FINISHED is a failure
 * (unexpected_message).
 */
int tls_server_hs_read_client_finished(tls_server_hs_t *hs,
                                        const uint8_t *rec, size_t rec_len);

/*
 * Copy out the derived application traffic keys/IVs. Returns 1 only in phase DONE
 * (so keys are unreadable until the client Finished has been verified); otherwise
 * returns 0 and writes nothing. Any out pointer may be NULL to skip it.
 */
int tls_server_hs_app_keys(const tls_server_hs_t *hs,
                           uint8_t server_key[32], uint8_t server_iv[12],
                           uint8_t client_key[32], uint8_t client_iv[12]);

/* Current phase. */
tls_server_hs_phase_t tls_server_hs_phase(const tls_server_hs_t *hs);

/* The alert to send after a failure (0 if not FAILED). */
uint8_t tls_server_hs_alert(const tls_server_hs_t *hs);

/*
 * Wipe all secret material and latch FAILED. Idempotent; call when discarding a
 * handshake so no key lingers in the freed struct.
 */
void tls_server_hs_wipe(tls_server_hs_t *hs);

#endif /* WEBLIB_TLS */
#endif /* WEBLIB_TLS_SERVER_HANDSHAKE_H */
