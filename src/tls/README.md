# `src/tls/` — Experimental pure-C TLS 1.3 (EXPERIMENTAL · UNAUDITED)

> **Status: scaffold only.** There is **no** working TLS here yet — no handshake,
> no record layer, no cryptography. Do **not** rely on this for any security
> property. This directory is the foundation for an incremental, from-scratch
> TLS build; every cryptographic primitive will land with a known-answer test
> against its official RFC vector before anything depends on it.

## Why hand-written TLS

The library's identity is *zero external dependencies, pure ISO C*. Its one
missing production capability is TLS — today HTTPS requires an external reverse
proxy. The owner chose **Option A: hand-write TLS in pure C from scratch**
(nginx-inspired), matching the repo's own ADR (`NEXT_PHASE.md`). No OpenSSL,
mbedTLS, or any crypto library is linked; `PLATFORM_LIBS` gains no new dependency.

## Scope (first milestone — planned, not yet built)

- **In:** TLS 1.3 (RFC 8446), **server-side only**; one cipher suite
  `TLS_CHACHA20_POLY1305_SHA256`; group **X25519**; signature **Ed25519**; one
  self-supplied Ed25519 certificate.
- **Out (documented future):** TLS 1.2; AES-GCM and any other suite; RSA / ECDSA /
  P-256 / bignum; client-certificate verification; session resumption / tickets /
  0-RTT.
- **Rationale:** ChaCha20-Poly1305 + X25519 + Ed25519 are constant-time by
  construction and need no big-integer machinery — the only responsible choice for
  hand-rolled crypto — and curl and every modern browser speak them.

## Build

TLS is **off by default** and **native-only**.

```sh
cmake -S . -B build-tls -DWEBLIB_ENABLE_TLS=ON
cmake --build build-tls
ctest --test-dir build-tls
```

- `WEBLIB_ENABLE_TLS=OFF` (the default): the build is byte-identical to a build
  with no TLS code — nothing in this directory is compiled.
- `WEBLIB_ENABLE_TLS=ON` on a native target: defines `WEBLIB_TLS`, compiles
  `src/tls/*.c`, and builds the guarded `TlsTests` suite.
- WASM / Emscripten builds ignore the option entirely (no sockets to wrap; the
  browser / Cloudflare edge terminates TLS).

## Roadmap (incremental, each step independently verifiable)

- **Phase 0 (this scaffold):** build option + `-DWEBLIB_TLS` wiring + a linkable
  smoke symbol (`tls_build_info`). Default build proven unchanged.
- **Phase 0 (next):** shared crypto module — promote the existing static SHA-256 /
  HMAC-SHA256 / base64 out of `middleware_auth.c` / `websocket.c`; transport
  choke-point (`conn_read` / `conn_write`) proving no behavior change.
- **Phase 1:** primitives from scratch, each with an RFC known-answer test
  (ChaCha20, Poly1305, ChaCha20-Poly1305, HKDF, SHA-384/512, X25519, Ed25519).
- **Phase 2:** PEM + minimal ASN.1/DER + X.509 loading.
- **Phase 3:** TLS 1.3 record layer + server handshake state machine + key schedule.
- **Phase 4:** integration (`http_server_enable_tls`), config, `curl` / `openssl
  s_client` interop, ClientHello fuzzing, honest security labeling.

Design references in-repo: the "Pure C TLS (not OpenSSL)" ADR in
[`NEXT_PHASE.md`](../../NEXT_PHASE.md) and the Phase 11 TLS plan in
[`TODO.md`](../../TODO.md). (The detailed crypto/transport/build recon lives in
the maintainers' internal working notes, not in the public tree.)
