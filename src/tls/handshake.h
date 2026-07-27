/*
 * handshake.h — TLS 1.3 handshake messages (RFC 8446 §4). EXPERIMENTAL /
 * UNAUDITED.
 *
 * Part of the experimental pure-C TLS layer; compiled only under
 * -DWEBLIB_ENABLE_TLS=ON. This first piece is the ClientHello parser — the most
 * exposed parser in the stack, since it reads the very first bytes an unknown
 * peer sends. It is built on the bounds-checked wire reader (wire.h) and extracts
 * only the subset a server needs, skipping every extension it does not
 * understand. Message *builders* (ServerHello, …) will join here.
 *
 * Exercised by tests/test_tls_parse.c against a real OpenSSL ClientHello.
 */
#ifndef WEBLIB_TLS_HANDSHAKE_H
#define WEBLIB_TLS_HANDSHAKE_H

#ifdef WEBLIB_TLS

#include <stddef.h>
#include <stdint.h>
#include "wire.h"   /* tls_writer_t for the message builders */

/* HandshakeType values (RFC 8446 §4). */
#define TLS_HS_CLIENT_HELLO         1
#define TLS_HS_SERVER_HELLO         2
#define TLS_HS_ENCRYPTED_EXTENSIONS 8
#define TLS_HS_CERTIFICATE          11
#define TLS_HS_CERTIFICATE_VERIFY   15
#define TLS_HS_FINISHED             20

/*
 * The parsed subset of a ClientHello. The pointer fields borrow into the caller's
 * message buffer and are valid only as long as it lives; they are NULL / 0 when
 * the corresponding item is absent.
 */
typedef struct {
    uint8_t random[32];
    const uint8_t *session_id;         /* legacy_session_id, to echo in ServerHello */
    size_t session_id_len;
    int offers_tls13;                  /* supported_versions offers 0x0304 */
    int offers_x25519;                 /* supported_groups offers x25519 (0x001d) */
    int offers_chacha20_poly1305;      /* cipher_suites offers 0x1303 */
    int offers_ed25519;                /* signature_algorithms offers ed25519 (0x0807) */
    const uint8_t *x25519_key_share;   /* client's 32-byte X25519 public key, or NULL */
    const uint8_t *server_name;        /* SNI host_name (not NUL-terminated), or NULL */
    size_t server_name_len;
    int alpn_present;                  /* an ALPN extension (RFC 7301) was offered */
    int alpn_http11;                   /* the ALPN list includes "http/1.1" */
} tls_client_hello_t;

/*
 * Parse a ClientHello handshake message: `msg` is HandshakeType || uint24 length
 * || body, of total length `msg_len`. Returns 1 if it is a structurally
 * well-formed ClientHello (with `out` filled in), 0 on any malformation. Unknown
 * extensions are skipped; the recognized ones are extracted into `out`. Whether
 * the offered parameters are *acceptable* is the caller's decision.
 */
int tls_parse_client_hello(const uint8_t *msg, size_t msg_len, tls_client_hello_t *out);

/* ---- server handshake message builders --------------------------------- */

/*
 * Each builder appends one complete handshake message (HandshakeType || uint24
 * length || body) to the writer `w`, and returns the writer's ok state (0 if the
 * output buffer overflowed or an argument was out of range). The caller drives
 * the writer (init over a buffer, build the flight's messages, tls_writer_finish).
 */

/* ServerHello (RFC 8446 §4.1.3): echoes `session_id` (<=32), selects
 * TLS_CHACHA20_POLY1305_SHA256, and carries supported_versions (0x0304) and a
 * key_share with the server's 32-byte X25519 public key. */
int tls_build_server_hello(tls_writer_t *w, const uint8_t random[32],
                           const uint8_t *session_id, size_t session_id_len,
                           const uint8_t x25519_public_key[32]);

/* HelloRetryRequest (RFC 8446 §4.1.4): structurally a ServerHello carrying the
 * special "HelloRetryRequest" Random (SHA-256 of "HelloRetryRequest"), echoing
 * `session_id` (<=32), selecting TLS_CHACHA20_POLY1305_SHA256, and carrying
 * supported_versions (0x0304) and a key_share naming only the group the server
 * requires (X25519) with no key_exchange. Sent when the ClientHello offers X25519
 * in supported_groups but did not include an X25519 key_share. */
int tls_build_hello_retry_request(tls_writer_t *w,
                                  const uint8_t *session_id, size_t session_id_len);

/* EncryptedExtensions (RFC 8446 §4.3.1). If `alpn` is non-NULL it carries a single
 * application_layer_protocol_negotiation (RFC 7301) selecting the `alpn_len`-byte
 * protocol name (1..255); otherwise the extensions block is empty. */
int tls_build_encrypted_extensions(tls_writer_t *w, const uint8_t *alpn, size_t alpn_len);

/* Certificate (RFC 8446 §4.4.2): empty certificate_request_context and a single
 * CertificateEntry carrying `cert_der` (with no per-certificate extensions). */
int tls_build_certificate(tls_writer_t *w, const uint8_t *cert_der, size_t cert_len);

/* CertificateVerify (RFC 8446 §4.4.3): the ed25519 SignatureScheme (0x0807) and
 * the 64-byte signature. */
int tls_build_certificate_verify(tls_writer_t *w, const uint8_t signature[64]);

/* Finished (RFC 8446 §4.4.4): the 32-byte verify_data (an HMAC the caller
 * computes from the finished key over the transcript hash). */
int tls_build_finished(tls_writer_t *w, const uint8_t verify_data[32]);

#endif /* WEBLIB_TLS */
#endif /* WEBLIB_TLS_HANDSHAKE_H */
