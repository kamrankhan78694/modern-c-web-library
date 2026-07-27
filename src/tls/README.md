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

## Scope (implemented)

- **In:** TLS 1.3 (RFC 8446), **server-side only**; one cipher suite
  `TLS_CHACHA20_POLY1305_SHA256`; group **X25519**; signature **Ed25519**; one
  self-supplied Ed25519 certificate; HelloRetryRequest (§4.1.4) for a client that
  offers X25519 without a key_share; ALPN (RFC 7301) selecting `http/1.1`.
- **Out (documented future):** TLS 1.2; AES-GCM and any other suite; RSA / ECDSA /
  P-256 / bignum; client-certificate verification; session resumption / tickets /
  0-RTT.
- **Rationale:** ChaCha20-Poly1305 + X25519 + Ed25519 are constant-time by
  construction and need no big-integer machinery — the only responsible choice for
  hand-rolled crypto — and curl and every modern browser speak them.

## Security scope — exactly what is and isn't provided

This is an **experimental, unaudited** TLS 1.3 server. Read this before relying on
it for anything.

**Provided:** server-side TLS 1.3 termination for the single profile above; a
forward-secret 1-RTT handshake (ephemeral X25519, fresh per connection); server
authentication via an Ed25519 certificate + CertificateVerify; AEAD record
protection (ChaCha20-Poly1305); constant-time verify_data / shared-secret checks;
the RFC 8446 §7.4.2 all-zero-ECDH rejection; a fail-closed handshake state machine
(every error latches a terminal state and wipes secrets); ALPN negotiation with a
correct `no_application_protocol` refusal.

**NOT provided (by design, this milestone):**

- **No security audit.** The crypto and the state machine are hand-written and have
  not been independently reviewed. Do not use this to protect real secrets.
- **No cipher/curve/signature agility.** Exactly one of each. A client that cannot
  offer all three is refused (`handshake_failure`); a client that offers ALPN with
  no `http/1.1` is refused (`no_application_protocol`).
- **No client authentication, session resumption / tickets / PSK, 0-RTT, or
  KeyUpdate.**
- **No middlebox-compatibility ChangeCipherSpec *emission* (RFC 8446 §D.4).** An
  incoming dummy CCS from the client is accepted and ignored, but the server does
  **not** emit its own. §D.4 makes this optional (`MAY`); direct clients (curl,
  `openssl s_client`, browsers) complete the handshake without it. It matters only
  on paths with legacy TLS-1.2-era middleboxes that mishandle a "bare" TLS 1.3
  flow; such deployments should front this server with a proxy, or the emission can
  be added later (it is a few bytes after ServerHello/HRR and is excluded from the
  transcript).
- **No downgrade-attack signalling beyond version pinning.** The server only
  accepts TLS 1.3 (via `supported_versions`); it does not implement the RFC 8446
  §4.1.3 downgrade sentinel in ServerHello.random, because it never negotiates an
  older version to be downgraded *to*.
- **Not wired for WebSocket-over-TLS or the async server path** (threaded mode
  only), per the integration notes below.

## Interoperability status (milestone #1)

- ✅ **OpenSSL** — `openssl s_client -tls1_3` completes a real handshake + encrypted
  HTTP request/response (`tests/interop_openssl.sh`, `examples/tls_server.c`).
- ✅ **HelloRetryRequest** and ✅ **ALPN** (`http/1.1`), each cross-checked against an
  independent Python oracle.
- ✅ **Adversarial-input robustness** — a deterministic ClientHello/record fuzzer
  (`tests/test_tls_fuzz.c`) runs clean under ASan/UBSan.
- ⚠️ **Browsers (Chrome/Firefox/Safari): not something to rely on today.** Not a
  handshake bug — the blocker is the **Ed25519-only certificate** profile. Ed25519
  *server-certificate* support in mainstream browsers is limited and inconsistent
  across browsers and versions (much more so than Ed25519 support elsewhere in the
  ecosystem), so a browser page-load with this profile cannot be relied upon.
  Removing this bound is gated on adding an RSA/ECDSA certificate path (documented
  future), not on any missing handshake feature; the TLS-1.3 + ChaCha20 + X25519
  handshake itself is otherwise negotiable with browsers.
- **Known bound:** Ed25519-only — a client not offering `ed25519` in
  `signature_algorithms` (e.g. an old LibreSSL) is correctly refused with
  `handshake_failure`.

## Build

TLS is **off by default** and **native-only**.

```sh
cmake -S . -B build-tls -DWEBLIB_ENABLE_TLS=ON -DWEBLIB_TLS_TEST_HOOKS=ON
cmake --build build-tls
(cd build-tls && ctest)   # 13 suites, of which 7 are TLS
```

> `WEBLIB_TLS_TEST_HOOKS` exposes a deterministic-RNG seam used only by `TlsHttpTests`.
> It is **TEST-ONLY** — never enable it in a production build. A deterministic RNG
> removes every security property TLS has.

- `WEBLIB_ENABLE_TLS=OFF` (the default): the build is byte-identical to a build
  with no TLS code — nothing in this directory is compiled.
- `WEBLIB_ENABLE_TLS=ON` on a native target: defines `WEBLIB_TLS`, compiles
  `src/tls/*.c`, and builds six guarded suites — `TlsTests`, `TlsCryptoTests`,
  `TlsParseTests`, `TlsTransportTests`, `TlsFuzzTests` and `TlsInteropOpenssl`.
  The last is registered only when the `tls_server` example is built and CMake
  finds `bash`, and even then it **self-skips and reports a pass** without a
  TLS 1.3-capable `openssl s_client` — so confirm it actually ran.
- Adding `-DWEBLIB_TLS_TEST_HOOKS=ON` additionally builds `TlsHttpTests`, the only
  end-to-end `http_server_enable_tls()` test, for **7 TLS suites and 13 in total**.
  Without that flag you get 6 TLS suites and 12 in total.
- WASM / Emscripten builds ignore the option entirely (no sockets to wrap; the
  browser / Cloudflare edge terminates TLS).

## Roadmap (incremental, each step independently verifiable)

- **Phase 0 (landed):** build option + `-DWEBLIB_TLS` wiring + a linkable
  smoke symbol (`tls_build_info`). Default build proven unchanged.
- **Phase 0 (landed):** shared crypto module — promote the existing static SHA-256 /
  HMAC-SHA256 / base64 out of `middleware_auth.c` / `websocket.c`; transport
  choke-point (`conn_read` / `conn_write`) proving no behavior change.
- **Phase 1:** primitives from scratch, each with an RFC known-answer test
  (ChaCha20, Poly1305, ChaCha20-Poly1305, HKDF, SHA-512, X25519, Ed25519).
  *(Landed, except SHA-384, which was never built — SHA-512 shipped instead.)*
- **Phase 2 (landed, narrowed):** PEM + minimal ASN.1/DER, used to parse the PKCS#8
  Ed25519 private key. **No X.509 parsing shipped** — the server certificate is
  base64-decoded from PEM to DER and sent opaquely.
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
- **Interoperability (milestone #1):** real `openssl s_client` interop,
  HelloRetryRequest (§4.1.4 with the synthetic-transcript rewrite), ALPN (`http/1.1`),
  and ClientHello/record fuzzing have all landed — see **"Interoperability status"**
  above for the current matrix, the browser/Ed25519 bound, and the deliberate
  middlebox-CCS-emission omission.

Design references in-repo: the "Pure C TLS (not OpenSSL)" ADR in
[`NEXT_PHASE.md`](../../NEXT_PHASE.md) and the Phase 11 TLS plan in
[`TODO.md`](../../TODO.md). (The detailed crypto/transport/build recon lives in
the maintainers' internal working notes, not in the public tree.)
