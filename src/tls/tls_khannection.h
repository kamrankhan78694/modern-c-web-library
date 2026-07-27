/*
 * tls_khannection.h — TLS 1.3 server connection engine (sans-IO). EXPERIMENTAL / UNAUDITED.
 *
 * Part of the experimental pure-C TLS layer; compiled only under
 * -DWEBLIB_ENABLE_TLS=ON. This is the byte-in / byte-out engine that turns the
 * handshake state machine (server_handshake.h) and the record layer (record.h)
 * into a usable connection: it frames a raw TCP byte stream into TLS records
 * (records may span several reads, or several may arrive in one), drives the
 * handshake to completion, then encrypts and decrypts application data.
 *
 * It performs NO socket I/O of its own ("sans-IO"): the caller feeds it the bytes
 * it read from the peer and is handed back (a) bytes to write to the peer and
 * (b) decrypted application data. This keeps the engine pure and exhaustively
 * testable off the socket hot path; the transport layer wires it to real sockets
 * separately.
 *
 * SECURITY. Every incoming byte is attacker-controlled. All framing is bounded
 * (a declared record length over the 2^14+256 limit is rejected before any copy),
 * every record is dispatched by the current connection state (a record type that
 * is illegal for the state is a fatal alert), and any failure is terminal: the
 * connection latches FAILED, emits the corresponding alert for the caller to
 * flush, and refuses all further work. Sequence numbers are per-direction and a
 * 64-bit wrap is refused rather than silently reused (RFC 8446 §5.3, §5.5).
 *
 * SCOPE / LIMITATIONS (documented, none silent):
 *   - Server side only, one profile (TLS_CHACHA20_POLY1305_SHA256 + X25519 +
 *     Ed25519), 1-RTT handshake plus a single HelloRetryRequest when the client
 *     offers X25519 without a key_share — the engine routes the second ClientHello
 *     back through the handshake and drops an interleaved middlebox CCS. Inherits
 *     server_handshake.h's remaining limits (no client auth).
 *   - The ClientHello must arrive within a single record (handshake-message
 *     reassembly across records is not implemented); real curl/OpenSSL clients do
 *     this. Other handshake messages the engine consumes (the client Finished) are
 *     single-record by construction.
 *   - Post-handshake handshake messages (KeyUpdate, NewSessionTicket) are not
 *     supported; receiving one is a fatal unexpected_message. No session
 *     resumption / 0-RTT.
 *   - An incoming ChangeCipherSpec (RFC 8446 §D.4) is dropped; the engine does not
 *     itself emit the middlebox-compatibility CCS (that belongs to the
 *     interoperability milestone).
 *
 * Verified in tests/test_tls_parse.c against the independent Python
 * (`cryptography`) client oracle driving the full record stream, plus loopback and
 * adversarial cases (fragmentation, tampering, out-of-order, post-close).
 */
#ifndef WEBLIB_TLS_TLS_KHANNECTION_H
#define WEBLIB_TLS_TLS_KHANNECTION_H

#ifdef WEBLIB_TLS

#include <stddef.h>
#include <stdint.h>
#include "server_handshake.h"
#include "record.h"

/* Record reassembly buffer: one full-size TLSCiphertext (header + 2^14+256). */
#define TLS_KHANNECTION_INBUF_SIZE (TLS_RECORD_HEADER_LEN + TLS_RECORD_MAX_CIPHERTEXT)

/* ---- connection state -------------------------------------------------- */
typedef enum {
    TLS_KHANNECTION_HANDSHAKE = 0,  /* driving the handshake */
    TLS_KHANNECTION_ESTABLISHED,    /* handshake complete; application data flows */
    TLS_KHANNECTION_CLOSED,         /* peer sent close_notify; read side is finished */
    TLS_KHANNECTION_FAILED          /* terminal error; an alert was emitted */
} tls_khannection_state_t;

/* ---- call result ------------------------------------------------------- */
typedef enum {
    TLS_KHANNECTION_RC_OK = 0,   /* progressed; inspect *out_len, *app_len and the state */
    TLS_KHANNECTION_RC_CLOSED,   /* peer close_notify — a graceful read EOF */
    TLS_KHANNECTION_RC_ERROR     /* fatal; `out` may hold an alert to flush, then drop the conn */
} tls_khannection_rc_t;

/*
 * Connection object. Allocate one per accepted connection; drive it with the
 * functions below; do not touch the fields. It embeds the handshake state and a
 * single-record reassembly buffer, so sizeof is dominated by TLS_KHANNECTION_INBUF_SIZE
 * (~16 KiB) — budget that per concurrent TLS connection.
 */
typedef struct {
    tls_server_hs_t hs;
    const tls_server_config_t *cfg;   /* borrowed; must outlive the handshake */
    tls_khannection_state_t state;
    uint8_t alert;                    /* alert emitted on FAILED (0 otherwise) */

    uint8_t inbuf[TLS_KHANNECTION_INBUF_SIZE];
    size_t  in_len;                   /* bytes of the in-progress record buffered */

    /* Application traffic keys, server perspective: send with the server secret,
     * receive with the client secret. Installed when the handshake completes. */
    uint8_t send_key[32], send_iv[12];
    uint8_t recv_key[32], recv_iv[12];
    uint64_t send_seq, recv_seq;
    int keys_ready;

    /* Consecutive zero-length application_data records received while ESTABLISHED.
     * Empty records are legal (RFC 8446 §5.1, a traffic-analysis countermeasure) but
     * a flood of them yields no application data and would otherwise spin the caller;
     * too many in a row without real data is treated as abuse. Reset by any record
     * that carries application bytes. */
    uint32_t empty_app_records;
} tls_khannection_t;

/*
 * Initialize a server connection. `cfg` is borrowed (not copied) and must stay
 * valid until the handshake completes — it carries the certificate, signing key,
 * and the injected ephemeral/random (see server_handshake.h; production supplies
 * fresh CSPRNG bytes per connection).
 */
void tls_khannection_init(tls_khannection_t *c, const tls_server_config_t *cfg);

/*
 * Feed `len` bytes received from the peer. Appends any bytes that must be written
 * back to the peer (handshake flight, alerts) to `out` (capacity `out_cap`,
 * length in *out_len) and any decrypted application data to `app` (capacity
 * `app_cap`, length in *app_len). Records spanning multiple calls are buffered
 * internally; multiple records in one call are all processed.
 *
 * Returns TLS_KHANNECTION_RC_OK on progress, TLS_KHANNECTION_RC_CLOSED when the peer sent
 * close_notify, TLS_KHANNECTION_RC_ERROR on any fatal error (the connection latches
 * FAILED; when the error is ours to report, the alert to send is left in `out` —
 * a fatal alert *received* from the peer is not answered with one). `data` may be
 * NULL only when `len` is 0.
 *
 * Buffer sizing: `app_cap` must be at least `len` (decrypted data never exceeds
 * its ciphertext); `out_cap` must accommodate the server handshake flight (roughly
 * the certificate size plus a few hundred bytes) or an alert. A write that would
 * exceed a capacity is a fatal internal_error rather than an overflow.
 */
tls_khannection_rc_t tls_khannection_recv(tls_khannection_t *c, const uint8_t *data, size_t len,
                            uint8_t *out, size_t out_cap, size_t *out_len,
                            uint8_t *app, size_t app_cap, size_t *app_len);

/*
 * Encrypt `len` bytes of application plaintext to send, appending the resulting
 * TLSCiphertext record(s) to `out`. Plaintext longer than a single record's 2^14
 * limit is split across records. Valid only once ESTABLISHED; otherwise returns
 * TLS_KHANNECTION_RC_ERROR. `plaintext` may be NULL only when `len` is 0.
 */
tls_khannection_rc_t tls_khannection_send(tls_khannection_t *c, const uint8_t *plaintext, size_t len,
                            uint8_t *out, size_t out_cap, size_t *out_len);

/*
 * Emit a close_notify alert into `out` (encrypted once ESTABLISHED), moving the
 * connection to CLOSED. Idempotent-safe: a second call is a no-op returning
 * TLS_KHANNECTION_RC_CLOSED.
 */
tls_khannection_rc_t tls_khannection_close_notify(tls_khannection_t *c,
                                    uint8_t *out, size_t out_cap, size_t *out_len);

/* Current state / the alert emitted on failure (0 if none). */
tls_khannection_state_t tls_khannection_state(const tls_khannection_t *c);
uint8_t tls_khannection_alert(const tls_khannection_t *c);

/* Wipe all secret material and latch FAILED. Call when discarding the connection. */
void tls_khannection_wipe(tls_khannection_t *c);

#endif /* WEBLIB_TLS */
#endif /* WEBLIB_TLS_TLS_KHANNECTION_H */
