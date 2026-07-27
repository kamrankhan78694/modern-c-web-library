/*
 * tls_transport.h — blocking-socket adapter over the TLS 1.3 connection engine.
 * EXPERIMENTAL / UNAUDITED.
 *
 * Part of the experimental pure-C TLS layer; compiled only under
 * -DWEBLIB_ENABLE_TLS=ON (native-only). This is the bridge between the sans-IO
 * engine (tls_khannection.h), which only transforms byte buffers, and a real
 * connected socket: it owns the recv()/send() loop and drives the engine to
 * termination, then exposes a plain blocking read/write pair the HTTP layer can
 * use in place of raw recv()/send().
 *
 * It deliberately does the *blocking* form (one thread per connection), matching
 * the library's threaded server path; a non-blocking adapter for the event-loop
 * path is a separate, later piece. This module performs socket I/O but touches no
 * existing server code, so it is additive and unit-tested over a socketpair before
 * any hot-path integration.
 *
 * SECURITY. The engine enforces the protocol; this adapter only moves bytes. It
 * fails closed: any engine error or socket error tears the connection down, and
 * close wipes the key material. Per-connection memory is dominated by the engine's
 * ~16 KiB record-reassembly buffer plus this adapter's ~20 KiB application-read
 * buffer — budget ~40 KiB per concurrent TLS connection.
 *
 * SCOPE / LIMITATIONS (documented): server side, blocking sockets only; inherits
 * tls_khannection's profile and limits (one cipher/group/signature, 1-RTT, no
 * resumption/KeyUpdate/renegotiation). The ephemeral key and ServerHello random in
 * `cfg` must be fresh CSPRNG output per connection in production (see
 * server_handshake.h). No half-close: if the peer sends its close_notify before it
 * has read a response (e.g. a one-shot client that shuts down its write side right
 * after the request), tls_transport_accept still succeeds and the buffered request
 * is readable, but tls_transport_write can no longer send a response (the engine is
 * CLOSED). Full half-close (responding after the peer's close_notify) is deferred to
 * the interoperability milestone; ordinary request/response clients that keep the
 * connection open for the reply are unaffected.
 *
 * Verified in tests/test_tls_transport.c: a real handshake and encrypted
 * request/response over a socketpair, cross-checked against the independent oracle
 * keys.
 */
#ifndef WEBLIB_TLS_TLS_TRANSPORT_H
#define WEBLIB_TLS_TLS_TRANSPORT_H

#ifdef WEBLIB_TLS

#include <stddef.h>
#include <sys/types.h>   /* ssize_t */
#include "tls_khannection.h"
#include "record.h"

/* recv() chunk size and the application read buffer. The engine may emit, in a
 * single step, one buffered record's plaintext (up to 2^14) plus the plaintext of
 * whole records contained in the fresh chunk (up to the chunk size), so the buffer
 * must hold their sum. */
#define TLS_TRANSPORT_RECV_CHUNK 4096
#define TLS_TRANSPORT_APPBUF     (TLS_RECORD_MAX_PLAINTEXT + TLS_TRANSPORT_RECV_CHUNK)

typedef struct {
    tls_khannection_t conn;
    int fd;
    /* Decrypted application bytes produced by the engine but not yet handed to the
     * caller: valid range is [app_off, app_len). */
    uint8_t app[TLS_TRANSPORT_APPBUF];
    size_t  app_off;
    size_t  app_len;
    int     eof;   /* peer closed (close_notify or TCP EOF); reads return 0 */
} tls_transport_t;

/*
 * Run the server handshake over the already-connected blocking socket `fd`. `cfg`
 * supplies the certificate, signing key, and the injected ephemeral/random (see
 * server_handshake.h). Blocks until the handshake completes or fails. Returns 0 on
 * success (the connection is established and ready for read/write), -1 on any
 * failure (a fatal alert may have been written to the peer; the caller should close
 * `fd`). Does not take ownership of `fd`.
 */
int tls_transport_accept(tls_transport_t *t, int fd, const tls_server_config_t *cfg);

/*
 * Read up to `len` decrypted application bytes into `buf`, blocking until at least
 * one byte is available. Returns the number of bytes read (>0), 0 on a clean
 * end-of-stream (peer close_notify or TCP EOF), or -1 on error. Semantics mirror
 * recv() closely enough to drop into the existing read loop.
 */
ssize_t tls_transport_read(tls_transport_t *t, void *buf, size_t len);

/*
 * Encrypt and send all `len` bytes of application data (splitting across records as
 * needed). Returns 0 on success, -1 on error. Valid only once established.
 */
int tls_transport_write(tls_transport_t *t, const void *buf, size_t len);

/* Send a close_notify (best effort) and wipe all key material. */
void tls_transport_close(tls_transport_t *t);

#endif /* WEBLIB_TLS */
#endif /* WEBLIB_TLS_TLS_TRANSPORT_H */
