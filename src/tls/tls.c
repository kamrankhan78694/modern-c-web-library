/*
 * tls.c — Experimental pure-C TLS 1.3 layer, scaffold (EXPERIMENTAL, UNAUDITED).
 *
 * Phase 0: build foundation only. No cryptography, handshake, or record layer is
 * implemented here yet — this translation unit exists so the CMake wiring
 * (option WEBLIB_ENABLE_TLS -> -DWEBLIB_TLS -> native-only source -> linkable
 * symbol) can be verified end to end before any security-critical code lands.
 *
 * Compiled only under -DWEBLIB_ENABLE_TLS=ON on native targets; excluded from the
 * default build and from WASM / Cloudflare Workers builds (which get HTTPS from
 * the browser / edge). See src/tls/README.md for scope and status.
 */
#include "tls.h"

#ifdef WEBLIB_TLS

const char *tls_build_info(void) {
    return "modern-c-web-library TLS 1.3 server (EXPERIMENTAL, UNAUDITED); "
           "suite TLS_CHACHA20_POLY1305_SHA256, group X25519, sig Ed25519";
}

#else

/* The build system only compiles this file when WEBLIB_TLS is defined, so the
 * branch above always applies in practice. This typedef keeps the translation
 * unit non-empty if the file is ever compiled without the define, avoiding an
 * "ISO C forbids an empty translation unit" warning under -pedantic. */
typedef int tls_translation_unit_not_empty;

#endif /* WEBLIB_TLS */
