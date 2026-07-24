/*
 * tls.h — Experimental pure-C TLS 1.3 layer (EXPERIMENTAL, UNAUDITED).
 *
 * SCAFFOLD ONLY. No handshake, record layer, key schedule, or cryptography is
 * implemented yet. This header exists so the build wiring can be verified before
 * any security-critical code lands.
 *
 * Compiled only when the project is configured with -DWEBLIB_ENABLE_TLS=ON, which
 * defines WEBLIB_TLS and adds the src/tls sources to the native-only source list.
 * The default build (WEBLIB_ENABLE_TLS=OFF) does not include this file at all, and is
 * byte-identical to a build without any TLS code. TLS is native-only: WASM and
 * Cloudflare Workers get HTTPS from the browser / edge, so nothing here is built
 * for those targets. See src/tls/README.md.
 *
 * Planned first milestone (NOT yet implemented): TLS 1.3 (RFC 8446), server-side
 * only, one cipher suite TLS_CHACHA20_POLY1305_SHA256, key-exchange group X25519,
 * signature scheme Ed25519 — all constant-time by construction (no bignum / RSA /
 * AES-GCM / P-256).
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
