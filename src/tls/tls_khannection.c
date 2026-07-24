/*
 * tls_khannection.c — TLS 1.3 server connection engine (sans-IO). EXPERIMENTAL /
 * UNAUDITED. See tls_khannection.h.
 *
 * Compiled only under -DWEBLIB_ENABLE_TLS=ON. Frames a raw byte stream into TLS
 * records with a bounded reassembly buffer, dispatches each complete record by the
 * current connection state, drives the handshake state machine, and protects /
 * deprotects application data. No dynamic allocation and no socket I/O.
 */
#include "tls_khannection.h"

#ifdef WEBLIB_TLS

#include "kamran.k"   /* secure_zero */
#include <string.h>

/* Alert values not already named in server_handshake.h (RFC 8446 §6). */
#define TLS_ALERT_CLOSE_NOTIFY    0
#define TLS_ALERT_RECORD_OVERFLOW 22
#define ALERT_LEVEL_WARNING       1
#define ALERT_LEVEL_FATAL         2

/* Append raw bytes to the output buffer. Returns 1 on success, 0 if they do not
 * fit (the caller turns that into a fatal internal_error). */
static int out_append(uint8_t *out, size_t out_cap, size_t *out_len,
                      const uint8_t *data, size_t n) {
    if (*out_len > out_cap || n > out_cap - *out_len) {
        return 0;
    }
    memcpy(out + *out_len, data, n);
    *out_len += n;
    return 1;
}

/* Emit one alert record into `out`: encrypted under the send key once the
 * application keys are installed, otherwise a plaintext record. Best-effort — a
 * failure to append is ignored, since the caller is already tearing down. */
static void emit_alert(tls_khannection_t *c, uint8_t *out, size_t out_cap, size_t *out_len,
                       uint8_t level, uint8_t desc) {
    uint8_t body[2];
    body[0] = level;
    body[1] = desc;

    if (c->keys_ready) {
        size_t n = 0;
        if (c->send_seq == UINT64_MAX || *out_len > out_cap) {
            return;
        }
        if (tls_record_seal(c->send_key, c->send_iv, c->send_seq, TLS_CONTENT_ALERT,
                            body, sizeof body, 0, out + *out_len, out_cap - *out_len, &n)) {
            c->send_seq++;
            *out_len += n;
        }
    } else {
        uint8_t rec[TLS_RECORD_HEADER_LEN + 2];
        rec[0] = TLS_CONTENT_ALERT;
        rec[1] = 0x03;
        rec[2] = 0x03;
        rec[3] = 0x00;
        rec[4] = 0x02;
        rec[5] = level;
        rec[6] = desc;
        (void)out_append(out, out_cap, out_len, rec, sizeof rec);
    }
}

/* Latch a fatal error: emit the alert (for the caller to flush), wipe secrets, and
 * move to FAILED. Always returns TLS_KHANNECTION_RC_ERROR. */
static tls_khannection_rc_t conn_fail(tls_khannection_t *c, uint8_t *out, size_t out_cap,
                               size_t *out_len, uint8_t desc) {
    emit_alert(c, out, out_cap, out_len, ALERT_LEVEL_FATAL, desc);
    tls_khannection_wipe(c);          /* wipes keys, sets state FAILED, alert 0 */
    c->alert = desc;
    return TLS_KHANNECTION_RC_ERROR;
}

/* Handle an alert received from the peer (its 2-byte body). close_notify is a
 * graceful shutdown -> CLOSED; anything else is the peer aborting -> FAILED (we do
 * not answer an alert with an alert). */
static tls_khannection_rc_t handle_incoming_alert(tls_khannection_t *c, const uint8_t *body, size_t n) {
    if (n != 2) {
        tls_khannection_wipe(c);
        c->alert = TLS_ALERT_DECODE_ERROR;
        return TLS_KHANNECTION_RC_ERROR;
    }
    if (body[1] == TLS_ALERT_CLOSE_NOTIFY) {
        c->state = TLS_KHANNECTION_CLOSED;
        return TLS_KHANNECTION_RC_CLOSED;
    }
    tls_khannection_wipe(c);
    c->alert = body[1];
    return TLS_KHANNECTION_RC_ERROR;
}

/* Process exactly one complete record `rec[0..full)`. */
static tls_khannection_rc_t process_record(tls_khannection_t *c, const uint8_t *rec, size_t full,
                                    uint8_t *out, size_t out_cap, size_t *out_len,
                                    uint8_t *app, size_t app_cap, size_t *app_len) {
    uint8_t type = rec[0];
    const uint8_t *content = rec + TLS_RECORD_HEADER_LEN;
    size_t content_len = full - TLS_RECORD_HEADER_LEN;

    if (c->state == TLS_KHANNECTION_HANDSHAKE) {
        switch (type) {
        case TLS_CONTENT_HANDSHAKE: {
            /* Plaintext handshake record — the ClientHello. read_client_hello
             * writes the ServerHello + protected flight straight into `out`. */
            size_t produced = 0;
            size_t room = (out_cap >= *out_len) ? out_cap - *out_len : 0;
            if (!tls_server_hs_read_client_hello(&c->hs, c->cfg, content, content_len,
                                                 out + *out_len, room, &produced)) {
                return conn_fail(c, out, out_cap, out_len, tls_server_hs_alert(&c->hs));
            }
            *out_len += produced;
            return TLS_KHANNECTION_RC_OK;   /* now awaiting the client Finished */
        }
        case TLS_CONTENT_CHANGE_CIPHER_SPEC:
            /* RFC 8446 §5 / §D.4: a legitimate middlebox-compat CCS is exactly the
             * single byte 0x01 and is dropped; anything else is unexpected. */
            if (content_len == 1 && content[0] == 0x01) {
                return TLS_KHANNECTION_RC_OK;
            }
            return conn_fail(c, out, out_cap, out_len, TLS_ALERT_UNEXPECTED_MESSAGE);
        case TLS_CONTENT_APPLICATION_DATA:
            /* The client's Finished, protected under application_data. */
            if (!tls_server_hs_read_client_finished(&c->hs, rec, full)) {
                return conn_fail(c, out, out_cap, out_len, tls_server_hs_alert(&c->hs));
            }
            if (!tls_server_hs_app_keys(&c->hs, c->send_key, c->send_iv,
                                        c->recv_key, c->recv_iv)) {
                return conn_fail(c, out, out_cap, out_len, TLS_ALERT_INTERNAL_ERROR);
            }
            c->keys_ready = 1;
            c->send_seq = 0;
            c->recv_seq = 0;
            c->state = TLS_KHANNECTION_ESTABLISHED;
            return TLS_KHANNECTION_RC_OK;
        case TLS_CONTENT_ALERT:
            return handle_incoming_alert(c, content, content_len);
        default:
            return conn_fail(c, out, out_cap, out_len, TLS_ALERT_UNEXPECTED_MESSAGE);
        }
    }

    if (c->state == TLS_KHANNECTION_ESTABLISHED) {
        uint8_t inner_type = 0;
        size_t plain_len = 0;
        size_t room;

        /* After the handshake every record is application_data on the wire. */
        if (type != TLS_CONTENT_APPLICATION_DATA) {
            return conn_fail(c, out, out_cap, out_len, TLS_ALERT_UNEXPECTED_MESSAGE);
        }
        if (c->recv_seq == UINT64_MAX) {
            return conn_fail(c, out, out_cap, out_len, TLS_ALERT_INTERNAL_ERROR);
        }
        room = (app_cap >= *app_len) ? app_cap - *app_len : 0;
        /* A capacity shortfall is our local error, not the peer's, so report it as
         * internal_error rather than letting it masquerade as an AEAD failure below.
         * tls_record_open needs room for the whole inner plaintext = the record body
         * (full - header) minus the 16-byte tag. (A body too short to hold a tag is
         * left to tls_record_open, which rejects it as a bad record.) */
        if (full >= TLS_RECORD_HEADER_LEN + TLS_RECORD_TAG_LEN
            && room < full - TLS_RECORD_HEADER_LEN - TLS_RECORD_TAG_LEN) {
            return conn_fail(c, out, out_cap, out_len, TLS_ALERT_INTERNAL_ERROR);
        }
        /* Deprotect straight into the application buffer (zero-copy for app data). */
        if (!tls_record_open(c->recv_key, c->recv_iv, c->recv_seq, rec, full,
                             app + *app_len, room, &plain_len, &inner_type)) {
            return conn_fail(c, out, out_cap, out_len, TLS_ALERT_BAD_RECORD_MAC);
        }
        c->recv_seq++;

        switch (inner_type) {
        case TLS_CONTENT_APPLICATION_DATA:
            *app_len += plain_len;   /* expose the plaintext to the caller */
            return TLS_KHANNECTION_RC_OK;
        case TLS_CONTENT_ALERT:
            /* The alert body was written at app+*app_len; consume it there without
             * advancing *app_len, so it is never surfaced as application data. */
            return handle_incoming_alert(c, app + *app_len, plain_len);
        case TLS_CONTENT_HANDSHAKE:
            /* Post-handshake handshake messages (KeyUpdate / NewSessionTicket) are
             * out of scope. */
            return conn_fail(c, out, out_cap, out_len, TLS_ALERT_UNEXPECTED_MESSAGE);
        default:
            return conn_fail(c, out, out_cap, out_len, TLS_ALERT_UNEXPECTED_MESSAGE);
        }
    }

    /* CLOSED / FAILED never reach here (tls_khannection_recv guards first). */
    return conn_fail(c, out, out_cap, out_len, TLS_ALERT_UNEXPECTED_MESSAGE);
}

void tls_khannection_init(tls_khannection_t *c, const tls_server_config_t *cfg) {
    if (c == NULL) {
        return;
    }
    memset(c, 0, sizeof *c);
    tls_server_hs_init(&c->hs);
    c->cfg = cfg;
    c->state = TLS_KHANNECTION_HANDSHAKE;
    c->keys_ready = 0;
    c->in_len = 0;
    c->send_seq = 0;
    c->recv_seq = 0;
    c->alert = 0;
}

tls_khannection_rc_t tls_khannection_recv(tls_khannection_t *c, const uint8_t *data, size_t len,
                            uint8_t *out, size_t out_cap, size_t *out_len,
                            uint8_t *app, size_t app_cap, size_t *app_len) {
    size_t p = 0;

    if (out_len != NULL) {
        *out_len = 0;
    }
    if (app_len != NULL) {
        *app_len = 0;
    }
    if (c == NULL || out == NULL || out_len == NULL || app == NULL || app_len == NULL) {
        return TLS_KHANNECTION_RC_ERROR;
    }
    if (data == NULL && len != 0) {
        return TLS_KHANNECTION_RC_ERROR;
    }
    if (c->state == TLS_KHANNECTION_FAILED) {
        return TLS_KHANNECTION_RC_ERROR;
    }
    if (c->state == TLS_KHANNECTION_CLOSED) {
        return TLS_KHANNECTION_RC_CLOSED;   /* peer already closed; ignore further bytes */
    }
    if (len == 0) {
        /* Nothing to add. Returning here also avoids forming `data + p` when `data`
         * is NULL (len == 0 is allowed with a NULL buffer) — NULL pointer arithmetic
         * is undefined even when the length copied is zero. */
        return TLS_KHANNECTION_RC_OK;
    }

    for (;;) {
        uint16_t rec_len;
        size_t full, take;
        tls_khannection_rc_t rc;

        /* Accumulate the 5-byte record header. */
        if (c->in_len < TLS_RECORD_HEADER_LEN) {
            size_t need = TLS_RECORD_HEADER_LEN - c->in_len;
            take = (len - p < need) ? (len - p) : need;
            memcpy(c->inbuf + c->in_len, data + p, take);
            c->in_len += take;
            p += take;
            if (c->in_len < TLS_RECORD_HEADER_LEN) {
                break;   /* need more bytes for the header */
            }
        }

        rec_len = (uint16_t)(((uint16_t)c->inbuf[3] << 8) | c->inbuf[4]);
        if (rec_len > TLS_RECORD_MAX_CIPHERTEXT) {
            return conn_fail(c, out, out_cap, out_len, TLS_ALERT_RECORD_OVERFLOW);
        }
        full = TLS_RECORD_HEADER_LEN + rec_len;   /* <= TLS_KHANNECTION_INBUF_SIZE */

        /* Accumulate the record body. */
        if (c->in_len < full) {
            size_t need = full - c->in_len;
            take = (len - p < need) ? (len - p) : need;
            memcpy(c->inbuf + c->in_len, data + p, take);
            c->in_len += take;
            p += take;
            if (c->in_len < full) {
                break;   /* need more bytes for the body */
            }
        }

        /* One complete record is buffered. */
        rc = process_record(c, c->inbuf, full, out, out_cap, out_len, app, app_cap, app_len);
        c->in_len = 0;
        if (rc != TLS_KHANNECTION_RC_OK) {
            return rc;   /* CLOSED or ERROR */
        }
    }

    return TLS_KHANNECTION_RC_OK;
}

tls_khannection_rc_t tls_khannection_send(tls_khannection_t *c, const uint8_t *plaintext, size_t len,
                            uint8_t *out, size_t out_cap, size_t *out_len) {
    size_t off = 0;

    if (out_len != NULL) {
        *out_len = 0;
    }
    if (c == NULL || out == NULL || out_len == NULL) {
        return TLS_KHANNECTION_RC_ERROR;
    }
    if (plaintext == NULL && len != 0) {
        return TLS_KHANNECTION_RC_ERROR;
    }
    if (c->state != TLS_KHANNECTION_ESTABLISHED) {
        return TLS_KHANNECTION_RC_ERROR;
    }
    if (len == 0) {
        return TLS_KHANNECTION_RC_OK;   /* never emit an empty application record */
    }

    do {
        size_t chunk = len - off;
        size_t n = 0;
        if (chunk > TLS_RECORD_MAX_PLAINTEXT) {
            chunk = TLS_RECORD_MAX_PLAINTEXT;
        }
        if (c->send_seq == UINT64_MAX || *out_len > out_cap) {
            return conn_fail(c, out, out_cap, out_len, TLS_ALERT_INTERNAL_ERROR);
        }
        if (!tls_record_seal(c->send_key, c->send_iv, c->send_seq,
                             TLS_CONTENT_APPLICATION_DATA, plaintext + off, chunk, 0,
                             out + *out_len, out_cap - *out_len, &n)) {
            return conn_fail(c, out, out_cap, out_len, TLS_ALERT_INTERNAL_ERROR);
        }
        c->send_seq++;
        *out_len += n;
        off += chunk;
    } while (off < len);

    return TLS_KHANNECTION_RC_OK;
}

tls_khannection_rc_t tls_khannection_close_notify(tls_khannection_t *c,
                                    uint8_t *out, size_t out_cap, size_t *out_len) {
    if (out_len != NULL) {
        *out_len = 0;
    }
    if (c == NULL || out == NULL || out_len == NULL) {
        return TLS_KHANNECTION_RC_ERROR;
    }
    if (c->state == TLS_KHANNECTION_CLOSED) {
        return TLS_KHANNECTION_RC_CLOSED;
    }
    if (c->state == TLS_KHANNECTION_FAILED) {
        return TLS_KHANNECTION_RC_ERROR;
    }
    emit_alert(c, out, out_cap, out_len, ALERT_LEVEL_WARNING, TLS_ALERT_CLOSE_NOTIFY);
    c->state = TLS_KHANNECTION_CLOSED;
    return TLS_KHANNECTION_RC_CLOSED;
}

tls_khannection_state_t tls_khannection_state(const tls_khannection_t *c) {
    return (c == NULL) ? TLS_KHANNECTION_FAILED : c->state;
}

uint8_t tls_khannection_alert(const tls_khannection_t *c) {
    return (c == NULL) ? 0 : c->alert;
}

void tls_khannection_wipe(tls_khannection_t *c) {
    if (c == NULL) {
        return;
    }
    tls_server_hs_wipe(&c->hs);
    secure_zero(c->send_key, sizeof c->send_key);
    secure_zero(c->send_iv, sizeof c->send_iv);
    secure_zero(c->recv_key, sizeof c->recv_key);
    secure_zero(c->recv_iv, sizeof c->recv_iv);
    secure_zero(c->inbuf, sizeof c->inbuf);
    c->in_len = 0;
    c->keys_ready = 0;
    c->state = TLS_KHANNECTION_FAILED;
    c->alert = 0;
}

#endif /* WEBLIB_TLS */
