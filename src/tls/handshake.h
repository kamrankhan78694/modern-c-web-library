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
} tls_client_hello_t;

/*
 * Parse a ClientHello handshake message: `msg` is HandshakeType || uint24 length
 * || body, of total length `msg_len`. Returns 1 if it is a structurally
 * well-formed ClientHello (with `out` filled in), 0 on any malformation. Unknown
 * extensions are skipped; the recognized ones are extracted into `out`. Whether
 * the offered parameters are *acceptable* is the caller's decision.
 */
int tls_parse_client_hello(const uint8_t *msg, size_t msg_len, tls_client_hello_t *out);

#endif /* WEBLIB_TLS */
#endif /* WEBLIB_TLS_HANDSHAKE_H */
