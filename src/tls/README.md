# `src/tls/` — Experimental pure-C TLS 1.3 (EXPERIMENTAL · UNAUDITED)

> **Status: working end-to-end, but UNAUDITED.** The full stack is built and
> integrated: primitives, cert/key loading, key schedule, record layer, handshake
> state machine, the sans-IO connection engine, the socket adapter, and
> `http_server_enable_tls()`. A real **`openssl s_client` (OpenSSL 3.x) completes a
> TLS 1.3 handshake and an encrypted HTTP request/response** against the server
> (`tests/interop_openssl.sh`), so it genuinely interoperates with a mainstream TLS
> implementation. It has **NOT been security-audited**; do **not** rely on it for
> any security property in production. Known interop bound: the profile is
> Ed25519-only, so a client that does not offer `ed25519` in its
> `signature_algorithms` (e.g. an old LibreSSL) is — correctly — refused with
> `handshake_failure`.

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
- **Phase 3 (landed):** TLS 1.3 key schedule, record layer, handshake messages
  (ClientHello parser + server builders + auth crypto), the server handshake
  state machine (`server_handshake.c`), the sans-IO connection engine
  (`tls_khannection.c`) — record framing, handshake driving, and application-data
  encrypt/decrypt — and the blocking-socket adapter (`tls_transport.c`) that drives
  the engine over a real fd; for the 1-RTT `TLS_CHACHA20_POLY1305_SHA256` + X25519 +
  Ed25519 flow, each cross-checked against an independent Python oracle (the adapter
  over an actual socketpair).
- **Phase 4 — integration (landed):** `http_server_enable_tls()` wires
  `tls_transport` into the threaded server path — the handshake runs on accept and
  the request/response I/O is TLS-routed, so the server terminates TLS internally.
  The non-TLS path is byte-identical (the stress suite passes unchanged), and an
  end-to-end test drives a real TLS 1.3 request/response against the live server.
  Threaded mode only; WebSocket-over-TLS and the async path are not yet wired.
- **Interoperability (milestone #1, in progress):** ✅ `openssl s_client` (OpenSSL
  3.x) completes a real TLS 1.3 handshake + encrypted HTTP request/response against
  the server — a genuine third-party client — locked in by `tests/interop_openssl.sh`
  and demonstrated by `examples/tls_server.c`. ✅ **HelloRetryRequest** (RFC 8446
  §4.1.4): a client that offers X25519 but sends no X25519 key_share is now
  negotiated via a single HRR (with the §4.4.1 synthetic-transcript rewrite), rather
  than refused. ✅ ClientHello / record fuzzing (`tests/test_tls_fuzz.c`). Remaining:
  browser interop; optional middlebox-compat ChangeCipherSpec emission and ALPN.
  Known bound: Ed25519-only, so a client not offering `ed25519` is correctly refused.

Design references in-repo: the "Pure C TLS (not OpenSSL)" ADR in
[`NEXT_PHASE.md`](../../NEXT_PHASE.md) and the Phase 11 TLS plan in
[`TODO.md`](../../TODO.md). (The detailed crypto/transport/build recon lives in
the maintainers' internal working notes, not in the public tree.)
