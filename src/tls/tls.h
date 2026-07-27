/*
 * tls.h — Experimental pure-C TLS 1.3 layer (EXPERIMENTAL, UNAUDITED).
 *
 * This header is the build-wiring smoke test for the TLS layer (see tls_build_info
 * below); the layer itself is implemented across the other files in this directory
 * -- record layer, key schedule, handshake state machine and primitives all shipped
 * in v2.0.0. It remains EXPERIMENTAL and UNAUDITED: no external cryptographic audit
 * has been performed.
 *
 * Compiled only when the project is configured with -DWEBLIB_ENABLE_TLS=ON, which
 * defines WEBLIB_TLS and adds the src/tls sources to the native-only source list.
 * The default build (WEBLIB_ENABLE_TLS=OFF) does not include this file at all, and is
 * byte-identical to a build without any TLS code. TLS is native-only: WASM and
 * Cloudflare Workers get HTTPS from the browser / edge, so nothing here is built
 * for those targets. See src/tls/README.md.
 *
 * Shipped profile (v2.0.0): TLS 1.3 (RFC 8446), server-side only, one cipher suite
 * TLS_CHACHA20_POLY1305_SHA256, key-exchange group X25519, signature scheme Ed25519
 * — all constant-time by construction (no bignum / RSA / AES-GCM / P-256). Browser
 * page-load is NOT achieved; openssl s_client interop is.
 */
#ifndef WEBLIB_TLS_H
#define WEBLIB_TLS_H

#ifdef WEBLIB_TLS

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Return a human-readable banner describing this experimental TLS build and its
 * planned scope. Never returns NULL or an empty string.
 *
 * This is intentionally the only symbol the Phase 0 scaffold exports: it lets the
 * build pipeline (option WEBLIB_ENABLE_TLS -> -DWEBLIB_TLS -> compiled native
 * source -> linkable symbol -> smoke test) be verified end to end before any
 * cryptography exists. It performs no cryptographic or network operation.
 *
 * Naming: the tls_ prefix matches the subsystem-prefixed convention used across
 * the library (http_server_*, websocket_*, ...) and the planned TLS API
 * (tls_accept/tls_read/tls_write/tls_shutdown).
 */
const char *tls_build_info(void);

#ifdef __cplusplus
}
#endif

#endif /* WEBLIB_TLS */
#endif /* WEBLIB_TLS_H */
