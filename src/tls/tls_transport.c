/*
 * tls_transport.c — blocking-socket adapter over the TLS 1.3 connection engine.
 * EXPERIMENTAL / UNAUDITED. See tls_transport.h.
 *
 * Compiled only under -DWEBLIB_ENABLE_TLS=ON. Owns the recv()/send() loop and
 * drives the sans-IO engine (tls_khannection). No dynamic allocation.
 */
#include "tls_transport.h"

#ifdef WEBLIB_TLS

#include "kamran.k"   /* secure_zero */
#include <errno.h>
#include <string.h>
#include <sys/socket.h>

/* Suppress SIGPIPE on send() where the platform supports the flag (Linux);
 * elsewhere the server ignores SIGPIPE process-wide. */
#ifdef MSG_NOSIGNAL
#define TLS_SEND_FLAGS MSG_NOSIGNAL
#else
#define TLS_SEND_FLAGS 0
#endif

/* Tear down: wipe the engine's key material AND the decrypted application
 * plaintext buffered here (the strictly-more-sensitive secret — cookies, auth
 * headers, request bodies), so neither lingers in the freed struct / a core dump.
 * Idempotent. */
static void transport_wipe(tls_transport_t *t) {
    tls_khannection_wipe(&t->conn);
    secure_zero(t->app, sizeof t->app);
    t->app_off = 0;
    t->app_len = 0;
}

/* Write every byte of `buf` to `fd`, retrying short writes and EINTR. 0 / -1. */
static int send_all_fd(int fd, const uint8_t *buf, size_t len) {
    size_t off = 0;
    while (off < len) {
        ssize_t s = send(fd, buf + off, len - off, TLS_SEND_FLAGS);
        if (s < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        if (s == 0) {
            return -1;   /* a 0-byte send makes no progress: treat as an error, not a spin */
        }
        off += (size_t)s;
    }
    return 0;
}

/*
 * recv one chunk, feed it to the engine, flush any bytes the engine wants sent, and
 * stash any decrypted application bytes. Must only be called with the app buffer
 * drained (app_off == app_len). Returns 1 on progress (inspect state / app_len),
 * 0 on a TCP EOF, -1 on a socket or engine error.
 */
static int transport_pump(tls_transport_t *t) {
    uint8_t raw[TLS_TRANSPORT_RECV_CHUNK];
    uint8_t out[TLS_RECORD_MAX_CIPHERTEXT];
    size_t out_len = 0, app_len = 0;
    tls_khannection_rc_t rc;
    ssize_t n;

    do {
        n = recv(t->fd, raw, sizeof raw, 0);
    } while (n < 0 && errno == EINTR);
    if (n < 0) {
        transport_wipe(t);   /* fail closed */
        return -1;
    }
    if (n == 0) {
        t->eof = 1;   /* TCP EOF without a close_notify */
        return 0;
    }

    rc = tls_khannection_recv(&t->conn, raw, (size_t)n, out, sizeof out, &out_len,
                              t->app, sizeof t->app, &app_len);
    if (out_len > 0) {
        if (send_all_fd(t->fd, out, out_len) < 0) {
            transport_wipe(t);
            return -1;
        }
    }
    if (rc == TLS_KHANNECTION_RC_ERROR) {
        transport_wipe(t);   /* idempotent: the engine already wiped its own state */
        return -1;
    }
    t->app_off = 0;
    t->app_len = app_len;
    if (rc == TLS_KHANNECTION_RC_CLOSED) {
        t->eof = 1;
    }
    return 1;
}

int tls_transport_accept(tls_transport_t *t, int fd, const tls_server_config_t *cfg) {
    if (t == NULL) {
        return -1;
    }
#ifdef SO_NOSIGPIPE
    /* On platforms without MSG_NOSIGNAL (macOS/BSD), a send() to a peer that has
     * closed would otherwise raise SIGPIPE and kill the process. Suppress it on the
     * socket itself so the adapter is self-contained (best effort). */
    {
        int on = 1;
        (void)setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof on);
    }
#endif
    tls_khannection_init(&t->conn, cfg);
    t->fd = fd;
    t->app_off = 0;
    t->app_len = 0;
    t->eof = 0;

    while (tls_khannection_state(&t->conn) == TLS_KHANNECTION_HANDSHAKE) {
        if (transport_pump(t) <= 0) {
            transport_wipe(t);   /* error or EOF mid-handshake: fail closed */
            return -1;
        }
    }
    /* Success is normally state ESTABLISHED. It is ALSO success if the peer
     * coalesced its close_notify with (or right after) its Finished in one recv:
     * the engine then runs HANDSHAKE -> ESTABLISHED -> CLOSED inside a single pump,
     * so the terminal state is CLOSED. Application bytes can only be produced once
     * the handshake has established, so a non-empty app buffer proves it completed
     * and a request was received — keep it rather than discarding the request and
     * misreporting a completed handshake as a failure. (The peer has closed its
     * write side; responding to such a half-closed request is not yet supported —
     * see the header's limitations.) */
    if (tls_khannection_state(&t->conn) == TLS_KHANNECTION_ESTABLISHED) {
        return 0;
    }
    if (tls_khannection_state(&t->conn) == TLS_KHANNECTION_CLOSED && t->app_len > 0) {
        return 0;
    }
    transport_wipe(t);
    return -1;
}

ssize_t tls_transport_read(tls_transport_t *t, void *buf, size_t len) {
    size_t avail, n;
    if (t == NULL || buf == NULL) {
        return -1;
    }
    if (len == 0) {
        return 0;
    }
    /* Serve from the buffer; pump the socket until some application data arrives or
     * the stream ends. */
    while (t->app_off == t->app_len) {
        int r;
        if (t->eof || tls_khannection_state(&t->conn) == TLS_KHANNECTION_CLOSED) {
            return 0;
        }
        r = transport_pump(t);
        if (r < 0) {
            return -1;
        }
        if (r == 0) {
            return 0;   /* TCP EOF */
        }
    }
    avail = t->app_len - t->app_off;
    n = (avail < len) ? avail : len;
    memcpy(buf, t->app + t->app_off, n);
    t->app_off += n;
    return (ssize_t)n;
}

int tls_transport_write(tls_transport_t *t, const void *buf, size_t len) {
    const uint8_t *p = (const uint8_t *)buf;
    uint8_t out[TLS_RECORD_MAX_CIPHERTEXT];
    size_t off = 0;

    if (t == NULL || (buf == NULL && len != 0)) {
        return -1;
    }
    if (tls_khannection_state(&t->conn) != TLS_KHANNECTION_ESTABLISHED) {
        return -1;
    }
    while (off < len) {
        size_t chunk = len - off;
        size_t out_len = 0;
        if (chunk > TLS_RECORD_MAX_PLAINTEXT) {
            chunk = TLS_RECORD_MAX_PLAINTEXT;   /* one record per iteration bounds `out` */
        }
        if (tls_khannection_send(&t->conn, p + off, chunk, out, sizeof out, &out_len)
            != TLS_KHANNECTION_RC_OK) {
            transport_wipe(t);   /* fail closed */
            return -1;
        }
        if (send_all_fd(t->fd, out, out_len) < 0) {
            transport_wipe(t);
            return -1;
        }
        off += chunk;
    }
    return 0;
}

void tls_transport_close(tls_transport_t *t) {
    if (t == NULL) {
        return;
    }
    if (tls_khannection_state(&t->conn) == TLS_KHANNECTION_ESTABLISHED) {
        uint8_t out[64];
        size_t out_len = 0;
        if (tls_khannection_close_notify(&t->conn, out, sizeof out, &out_len)
                != TLS_KHANNECTION_RC_ERROR && out_len > 0) {
            (void)send_all_fd(t->fd, out, out_len);   /* best effort */
        }
    }
    transport_wipe(t);   /* wipe keys AND any buffered decrypted plaintext */
}

#endif /* WEBLIB_TLS */
