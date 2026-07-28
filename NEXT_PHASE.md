# Next Phase Roadmap — Modern C Web Library (post-v2.0.0)

> **Methodology**: First-principles engineering workflow.
> Every decision below traces back to a verifiable technical constraint, not convention.

> **Where this roadmap stands — 2026-07-27.**
> Phases 11 and 12 (the pure-C TLS layer) **shipped together in v2.0.0**. The plan below targeted
> them at v1.1.0 and v1.2.0 separately; that did not happen, and the version targets for the
> remaining phases have been re-baselined accordingly (see §13).
>
> What actually landed is a hand-written **TLS 1.3 server** — **EXPERIMENTAL and UNAUDITED**, not for
> production use without an external cryptographic audit. It is OFF by default (`WEBLIB_ENABLE_TLS`,
> default OFF; with it off no `src/tls` code is compiled) and it is deliberately narrower than the
> plan below: one cipher suite (`TLS_CHACHA20_POLY1305_SHA256`), X25519 only, Ed25519 only,
> server-side only, threaded mode only, native builds only (not WASM, not Cloudflare Workers).
>
> The planned scope that did **not** land — AES-GCM, RSA/ECDSA certificates (and with them browser
> page-load), TLS 1.2, client mode, session resumption, async-mode TLS, and an external audit — is
> carried forward as **Phase 21** below rather than quietly dropped.
> See [`src/tls/README.md`](src/tls/README.md) for the full status and limits.

---

## Part I: Released Phases (Reference)

| Phase | Version | Status | Highlights |
|-------|---------|--------|------------|
| Phase 1 | v0.1.0 | ✅ Complete | HTTP server, event loop (epoll/kqueue/poll), routing, middleware, JSON, CMake build |
| Phase 2 | v0.2.0 | ✅ Complete | WebSocket RFC 6455 (handshake, framing, masking, fragmentation, control frames) |
| Phase 3 | v0.3.0 | ✅ Complete | WebSocket frame processing in threaded mode, persistent connections, ping/pong |
| Phase 4 | v0.4.0 | ✅ Complete | HTTP parser hardening, header storage, JSON arrays, connection handling |
| Phase 5 | v0.5.0 | ✅ Complete | Body parsing, cookies, CORS, rate limiting, static file serving |
| Phase 6 | v0.6.0 | ✅ Complete | Sessions, template engine, auth middleware (Basic/JWT/API-Key), DB pooling, API docs |
| Phase 7 | v0.7.0 | ✅ Complete | Socket timeouts, thread pool, graceful shutdown, GitHub Actions CI, integration tests |
| Phase 8 | v0.8.0 | ✅ Complete | CSRF middleware, logging, error handler, input validation, health check |
| Phase 9 | v0.9.0 | ✅ Complete | Response compression, caching layer, metrics middleware, async WebSocket, benchmarking |
| Phase 10 | v1.0.0 | ✅ Complete | REST API example, tutorials, documentation, release engineering |
| Phase 11 | v2.0.0 | ✅ Delivered (narrowed) | TLS crypto primitives — SHA-256, SHA-512, HMAC, HKDF, ChaCha20, Poly1305, ChaCha20-Poly1305 AEAD, X25519, Ed25519, each with RFC known-answer tests. **EXPERIMENTAL · UNAUDITED.** AES-GCM and SHA-384 were dropped, not built |
| Phase 12 | v2.0.0 | ✅ Delivered (narrowed) | TLS 1.3 **server** — record layer (2^14 plaintext limit, fragmentation), handshake state machine incl. HelloRetryRequest, DER/PEM and Ed25519 key parsing (the certificate is sent opaquely, not parsed as X.509), ALPN `http/1.1`, `http_server_enable_tls()`. **EXPERIMENTAL · UNAUDITED.** No client mode, no RSA/ECDSA, no session resumption |

**v1.0.0 baseline** (2026-02-22): 129/129 unit tests · zero compiler warnings · 25 source modules · 5 example servers

**v2.0.0 baseline** (2026-07-27): default build — 6 ctest suites green (`WebLibTests`, `KamranHeaderTests`,
`AsyncWebSocketTests`, `StressTests`, `WorkerTests`, `WasmTests`) · 36 source modules · 5 example servers.
With `-DWEBLIB_ENABLE_TLS=ON -DWEBLIB_TLS_TEST_HOOKS=ON` — 13 ctest suites green (adds `TlsTests`,
`TlsCryptoTests`, `TlsParseTests`, `TlsTransportTests`, `TlsFuzzTests`, `TlsHttpTests`,
`TlsInteropOpenssl`) · 56 source modules · 6 example servers (adds `tls_server`).
Zero compiler warnings under `-Wall -Wextra -pedantic`.

---

## Part II: Roadmap — Phases 11–21

---

## 1. Idea Intake

**Core problem in one sentence** *(restated 2026-07 after v2.0.0)*: The library has solid HTTP/1.1 functionality and now has an experimental, unaudited pure-C TLS 1.3 server for the narrow ChaCha20-Poly1305 / X25519 / Ed25519 profile, but it still lacks audited and broadly-interoperable transport encryption, modern protocol support (HTTP/2, HTTP/3), persistent storage, multi-process scalability, cross-platform parity, and the developer tooling necessary to compete with frameworks like nginx and libuv as a self-contained, zero-dependency C web platform.

---

## 2. Crystallized Brief

| Dimension | Detail |
|-----------|--------|
| **Target users** | C developers building production HTTP/HTTPS backends, microservices, and real-time systems who require zero external dependencies and full source-level control |
| **Desired outcomes** | A library that encrypts traffic (TLS 1.3 — a first, experimental server-side cut shipped in v2.0.0), speaks HTTP/2 and HTTP/3, persists data without external databases, scales across CPU cores via multi-process architecture, runs identically on Linux/macOS/Windows/BSD, and provides developer tooling for rapid iteration |
| **Shipped non-native targets** | A WASM-safe source subset builds under Emscripten via `emcmake` — see the `EMSCRIPTEN` branch and `WEBLIB_SOURCES_WASM_SAFE` in `CMakeLists.txt`. It covers the router, JSON, template engine, body parser, cookies, sessions, input validation, cache, compression and the middleware set; WebSocket, async WebSocket, the benchmark harness and the whole TLS layer are excluded. `src/http_server.c` and `src/event_loop.c` are in the list too, but under Emscripten they compile down to stubs — there are no sockets on that target, so you drive the WASM build through the `wasm_*` API instead. A Cloudflare Workers layer (`src/worker_runtime.c`, `worker_kv/r2/d1/queues.c`) bridges the fetch-event model to the library's request/response types — `worker_set_router()` is accepted but never dispatched — with in-memory simulations of the KV/R2/D1/Queues bindings that are the implementation in every build, Workers included |
| **Non-goals** | Full ORM abstraction; language bindings (Python/Go/Rust wrappers); GUI tools; package manager integration (apt/brew/vcpkg); a full server/socket stack under WebAssembly (the WASM-safe pure-logic subset already ships — porting the event loop and sockets to WASM is out of scope) |

---

## 3. Grounded First-Principles Design

| Phase | Version | Status | Highlights |
|-------|---------|--------|------------|
| Phase 4 | v0.4.0 | ✅ Complete | HTTP parser hardening, header storage, JSON arrays, connection handling |
| Phase 5 | v0.5.0 | ✅ Complete | Body parsing, cookies, CORS, rate limiting, static file serving |
| Phase 6 | v0.6.0 | ✅ Complete | Sessions, template engine, auth middleware (Basic/JWT/API-Key), DB pooling, API docs |
| Phase 7 | v0.7.0 | ✅ Complete | Socket timeouts, thread pool, graceful shutdown, GitHub Actions CI, integration tests |
| Phase 8 | v0.8.0 | ✅ Complete | CSRF middleware, logging, error handler, input validation, health check |
| Phase 9 | v0.9.0 | ✅ Complete | Response compression, caching layer, metrics middleware, async WebSocket, benchmarking suite |
| Phase 10 | v1.0.0 | ✅ Complete | REST API example, tutorials, release engineering |
| Phase 10.1 | unreleased (rolled into v2.0.0) | ✅ Complete | Security utilities, security headers middleware, secure secret handling |
| Phase 11 | v2.0.0 | ✅ Delivered (narrowed) | TLS crypto primitives with RFC known-answer tests — EXPERIMENTAL · UNAUDITED |
| Phase 12 | v2.0.0 | ✅ Delivered (narrowed) | TLS 1.3 server handshake + HTTPS integration — EXPERIMENTAL · UNAUDITED |

**Current state** (main, v2.0.1+): default build — 7 ctest suites green · 36 source modules · 7 example servers.
With `-DWEBLIB_ENABLE_TLS=ON -DWEBLIB_TLS_TEST_HOOKS=ON` — 14 ctest suites green · 56 source modules ·
6 example servers. Zero compiler warnings under `-Wall -Wextra -pedantic`.

> The `src/tls/` pure-C TLS 1.3 server (5,481 lines) is **EXPERIMENTAL and UNAUDITED** — see
> [`src/tls/README.md`](src/tls/README.md). It is OFF by default and native-only. `openssl s_client`
> interop is achieved; browser page-load is not. The Phase 11/12 plans below were written before it
> landed, so read them as *what was planned*, annotated with *what shipped*.

1. **Transport encryption is non-negotiable** — Every production deployment requires HTTPS. Without TLS, browsers refuse connections, load balancers reject backends, and credentials travel in plaintext. Pure C TLS (not OpenSSL) was the single highest-impact v2 feature. *Status: a first cut shipped in v2.0.0 — experimental, unaudited, server-side only, one cipher profile. It does not yet discharge this requirement for production traffic; a reverse proxy is still the right answer for anything you care about.*

2. **Protocol evolution drives adoption** — HTTP/2 multiplexing eliminates head-of-line blocking. HTTP/3 over QUIC eliminates TCP head-of-line blocking entirely. Supporting these protocols in pure C is a differentiator no other single-file C library offers.

3. **Persistence eliminates deployment complexity** — Requiring an external database (PostgreSQL, Redis) for simple state storage adds operational burden. An embedded key-value store and file-based persistence layer makes the library self-sufficient for 80% of use cases.

4. **Multi-core utilization determines throughput ceiling** — A single-threaded event loop or thread pool is limited to one core's throughput. Multi-process (fork-based) architecture with shared-nothing design scales linearly with CPU cores.

5. **Platform parity determines reach** — Windows IOCP, BSD kqueue variants, and cross-platform builds determine whether the library is Linux-only or truly portable.

6. **Developer experience determines adoption** — CLI scaffolding tools, hot reload, structured debug modes, plugin architecture, and configuration file support determine whether developers choose this library over alternatives.

### Architecture Decision Records (v2.0.0)

| Decision | Rationale |
|----------|-----------|
| Pure C TLS 1.3 (not 1.2) | TLS 1.3 has simpler handshake (1-RTT), fewer cipher suites to implement, mandatory forward secrecy; aligns with modern security baseline |
| X25519 key exchange (not RSA key exchange) | Smaller code, faster computation, mandatory in TLS 1.3; RSA key exchange removed in TLS 1.3 |
| ~~AES-256-GCM + ChaCha20-Poly1305 ciphers~~ → **ChaCha20-Poly1305 only** (AES-GCM deferred, not cancelled) | *Amended 2026-07.* The original rationale — AES-GCM for hardware-accelerated systems, ChaCha20 for ARM/embedded — assumed an AES-NI path. Under the zero-dependency pure-C constraint there is none, and portable software AES-GCM (T-tables + GHASH) is cache-timing-hazardous. What shipped in v2.0.0 is exactly one suite, `TLS_CHACHA20_POLY1305_SHA256`, with X25519 and Ed25519: constant-time by construction, no big-integer machinery. AES-GCM remains open work (Phase 21) |
| Ed25519-only server certificates | Follows from the constant-time-by-construction rule above (no RSA, no ECDSA, no P-256 bignum code). *Consequence, recorded 2026-07:* browsers have limited and inconsistent support for Ed25519 server certificates, so **browser page-load is not achievable** with the shipped profile. `openssl s_client` interop is. Adding RSA/ECDSA certificate support is Phase 21 work |
| HTTP/2 via ALPN negotiation over TLS | HTTP/2 cleartext (h2c) rarely used in practice; TLS-based negotiation is the standard path |
| HPACK with static table only (initially) | Dynamic table adds complexity and memory-based attacks (HPACK bomb); static table covers 90% of headers |
| Embedded B-tree key-value store | Simplest data structure that supports ordered iteration, range queries, and O(log n) access; avoids LSM complexity |
| Multi-process via `fork()` + shared-nothing | No shared memory races; each worker is independent; master process handles signals and restarts; proven model (nginx, Redis) |
| Windows IOCP via abstraction layer | `src/platform.h` abstracts epoll/kqueue/IOCP behind common interface; compile-time selection |
| Configuration via C struct + optional INI parser | No YAML/JSON config dependency; INI is trivially parseable in C; C struct provides type safety |

---

## 4. Adversarial Review

| Attack Vector / Failure Mode | Current Exposure (v2.0.0) | Mitigation (Phase) |
|------------------------------|--------------------------|---------------------|
| **Plaintext credential exposure** | HIGH by default: TLS is opt-in (`WEBLIB_ENABLE_TLS`, default OFF), so a stock build still speaks plaintext HTTP. With TLS on, only the experimental TLS 1.3 profile is available | Pure C TLS 1.3 server delivered in v2.0.0 (Phases 11–12), EXPERIMENTAL · UNAUDITED; terminate TLS at a reverse proxy for anything production-facing |
| **TLS implementation bugs** | REAL and UNAUDITED — 5,481 lines of hand-written crypto and protocol code now ship behind the opt-in flag | RFC known-answer tests per primitive (`TlsCryptoTests`); constant-time-by-construction primitive choice; deterministic fuzzer over the untrusted-input path (`TlsFuzzTests`); `openssl s_client` interop (`TlsInteropOpenssl`); ASan/UBSan TLS build in the `tls-check` CI job. **Still open: external cryptographic audit (Phase 21)** |
| **HTTP/2 stream flood** | N/A (not yet implemented) | MAX_CONCURRENT_STREAMS limit; stream reset rate limiting; SETTINGS_MAX_HEADER_LIST_SIZE (Phase 13) |
| **HPACK bomb (memory exhaustion)** | N/A | Static table only initially; dynamic table with hard size cap (Phase 13) |
| **Persistent storage corruption** | N/A (no persistence) | Write-ahead log; fsync on commit; CRC32 checksums on pages (Phase 14) |
| **B-tree page split crash** | N/A | WAL replay on recovery; atomic rename for page files (Phase 14) |
| **Multi-process fork bomb** | N/A (single process) | Hard worker count limit; master process rate-limits respawns (Phase 16) |
| **Shared socket thundering herd** | N/A | SO_REUSEPORT with per-worker accept; or master-distributes-fd model (Phase 16) |
| **QUIC amplification attack** | N/A | Retry token validation; address validation before resource commitment (Phase 19) |
| **Cross-platform divergence** | MEDIUM: Windows untested | Platform abstraction layer; CI matrix expansion (Phase 17) |
| **Plugin arbitrary code execution** | N/A | Plugins are compile-time linked C modules, not runtime-loaded DSOs (Phase 18) |
| **Config injection** | N/A | INI parser rejects values > MAX_CONFIG_VALUE_LEN; no shell expansion (Phase 18) |
| **Memory growth under load** | LOW: Valgrind clean | Continuous benchmark with RSS monitoring; leak detection in CI (Phase 20) |

---

## 5. Design Iteration (Refined Architecture)

Phases ordered by **dependency chain** and **blast radius**:

```
Phase 11 — DELIVERED in v2.0.0 (planned as v1.1.0): TLS Foundation — Crypto Primitives
   EXPERIMENTAL · UNAUDITED · off by default (WEBLIB_ENABLE_TLS) · native-only
   ├── [x] SHA-256 (FIPS 180-4)          — src/crypto/sha256.c (not src/tls/sha256.c as planned)
   ├── [x] SHA-512                        — src/tls/sha512.c (needed for Ed25519)
   ├── [ ] SHA-384                        — never built; nothing in the shipped profile needs it
   ├── [ ] AES-256-GCM (NIST SP 800-38D)  — DEFERRED to Phase 21, see the amended ADR in §3
   ├── [x] ChaCha20-Poly1305 (RFC 8439)   — src/tls/chacha20.c, poly1305.c, chacha20poly1305.c
   ├── [x] X25519 key exchange (RFC 7748) — src/tls/x25519.c
   ├── [x] Ed25519 signatures (RFC 8032)  — src/tls/ed25519.c  (added; not in the original plan)
   ├── [x] HMAC-SHA256 (src/crypto/sha256.c) + HKDF (RFC 5869) — src/tls/hkdf.c, key_schedule.c
   ├── [x] Pure C constant-time field arithmetic for X25519/Ed25519 — src/tls/field25519.c
   └── [x] RFC test-vector validation for every primitive — TlsCryptoTests

Phase 12 — DELIVERED in v2.0.0 (planned as v1.2.0): TLS 1.3 Handshake & HTTPS
   EXPERIMENTAL · UNAUDITED · server-side only · threaded mode only · native-only
   ├── [x] TLS 1.3 record layer (RFC 8446) — src/tls/record.c, 2^14 plaintext limit + fragmentation
   ├── [x] Server handshake state machine  — src/tls/handshake.c, server_handshake.c,
   │       handshake_auth.c, wire.c: ClientHello → ServerHello / HelloRetryRequest →
   │       EncryptedExtensions → Certificate → CertificateVerify → Finished, including the
   │       RFC 8446 §4.4.1 synthetic message_hash transcript rewrite on the HRR path
   ├── [x] Sans-IO connection engine + blocking-socket adapter — src/tls/tls_khannection.c,
   │       src/tls/tls_transport.c
   ├── [x] PEM + DER *parsing* for one self-supplied Ed25519 cert + PKCS#8 key — the key
   │       is fully DER-parsed; the certificate is only PEM-decoded to DER and sent opaquely
   │       — src/tls/der.c, pem.c, ed25519_key.c
   ├── [ ] Certificate *chain validation* (RSA/ECDSA signature verification) — NOT built, Phase 21
   ├── [x] ALPN negotiation of http/1.1  (h2 depends on Phase 13, not yet built)
   ├── [x] `http_server_enable_tls()` API — include/kamran.k; takes PEM buffers + lengths,
   │       NOT file paths; returns -1 in async mode
   ├── [x] HTTPS example server with a self-signed certificate — examples/tls_server.c
   ├── [x] Real `openssl s_client` TLS 1.3 interop, incl. a >16 KiB fragmented response and two
   │       requests on one connection — TlsInteropOpenssl
   └── [ ] Browser page-load — NOT achieved; Ed25519-only server certs have limited and
           inconsistent browser support. See Phase 21.

Phase 13 (v2.1.0): HTTP/2 Protocol
   ├── Binary framing layer (RFC 7540)
   ├── HPACK header compression (RFC 7541, static table)
   ├── Stream multiplexing with priority
   ├── Flow control (connection-level + stream-level)
   ├── Server push
   ├── `http_server_enable_http2()` API
   └── h2 + h2c (cleartext) support

Phase 14 (v2.2.0): Persistent Storage Engine
   ├── B-tree key-value store (on-disk, memory-mapped)
   ├── Write-ahead log (WAL) for crash recovery
   ├── Transaction support (begin/commit/rollback)
   ├── Page-level CRC32 checksums
   ├── Iterator API for range queries
   ├── `storage_open()` / `storage_close()` lifecycle
   └── Integration with session store + cache persistence

Phase 15 (v2.3.0): Advanced Middleware & Content
   ├── Directory listing (auto-generated HTML indexes)
   ├── Server-Sent Events (SSE) for streaming
   ├── Content negotiation (Accept header parsing)
   ├── Request/response streaming (chunked transfer)
   ├── Route groups with scoped middleware
   ├── Regex-based route matching
   └── ETag generation improvements (weak/strong)

Phase 16 (v2.4.0): Multi-Process Architecture
   ├── Master-worker process model (fork-based)
   ├── Worker supervision and auto-restart
   ├── SO_REUSEPORT per-worker accept
   ├── Signal-based worker management (SIGUSR1/SIGUSR2)
   ├── Zero-downtime reload (hot restart)
   ├── `http_server_set_workers(n)` API
   └── Per-worker metrics aggregation

Phase 17 (v2.5.0): Cross-Platform Hardening
   ├── Platform abstraction layer (`src/platform.h`)
   ├── Windows IOCP event loop backend
   ├── Windows named pipes for IPC
   ├── BSD (FreeBSD/OpenBSD/NetBSD) testing + CI
   ├── MSVC build support (CMake generator)
   ├── CI matrix: Linux (GCC/Clang) + macOS (Clang) + Windows (MSVC) + FreeBSD
   └── Platform compatibility documentation

Phase 18 (v2.6.0): Developer Experience & Configuration
   ├── INI configuration file parser
   ├── Plugin/extension architecture (compile-time modules)
   ├── Multiple template formats (Mustache-style, includes, inheritance)
   ├── Auto-escaping (HTML/URL/JS context-aware)
   ├── Debug mode (verbose logging, request inspection, timing)
   ├── API versioning support (URL prefix + header-based)
   └── Configuration validation and hot-reload

Phase 19 (v2.7.0): HTTP/3 & QUIC
   ├── UDP socket layer
   ├── QUIC transport protocol (RFC 9000)
   ├── QUIC handshake (integrates Phase 11-12 TLS 1.3)
   ├── Connection migration
   ├── HTTP/3 framing (RFC 9114)
   ├── QPACK header compression (RFC 9204)
   └── `http_server_enable_http3()` API

Phase 20 (v3.0.0): Release Engineering & Ecosystem
   ├── CLI tool (project scaffolding, route listing, config validator)
   ├── Prometheus-compatible metrics export endpoint
   ├── OpenTelemetry-compatible trace context propagation
   ├── Comprehensive fuzz testing suite
   ├── Performance regression CI gate
   ├── Complete v3.0.0 documentation + migration guide
   └── Semantic versioning enforcement + release automation

Phase 21 (unscheduled): Security Residuals & TLS Hardening
   Everything the Phase 11/12 plans promised that v2.0.0 did not deliver, plus the
   security items left over from the earlier "Advanced Security Hardening" plan (§7).
   ├── External cryptographic audit of src/tls/ — the gate that removes "UNAUDITED"
   ├── RSA / ECDSA certificate support + X.509 chain validation (unlocks browser page-load)
   ├── AES-256-GCM suite (needs an answer to the timing-safety problem in the amended ADR)
   ├── TLS client mode (currently server-side only)
   ├── Session resumption / PSK / 0-RTT
   ├── TLS in async mode (http_server_enable_tls() returns -1 there today)
   ├── WebSocket over TLS (a WS upgrade on a TLS connection is refused with 503 today)
   ├── SNI callback for virtual hosting
   ├── TLS private-key page locking (mlock()) — secure_zero() already ships, mlock() does not
   ├── PBKDF2-HMAC-SHA256 + password_hash_create()/password_hash_verify()
   ├── Request-ID middleware (X-Request-Id generation + propagation)
   ├── IP allowlist/denylist middleware with CIDR support
   └── Per-route body size limits + Content-Length enforcement before buffering
```

---

## 6. Milestone Roadmap (1–2 Week Slices)

### Phase 11: TLS Foundation — Crypto Primitives — DELIVERED in v2.0.0 (planned as v1.1.0, 6 weeks)

> **EXPERIMENTAL · UNAUDITED.** Not for production without an external cryptographic audit.
> Built only with `-DWEBLIB_ENABLE_TLS=ON` (default OFF); native-only.
> The "Delivered" column below records what actually landed against each planned week.

| Week | Milestone | Planned deliverables | Delivered |
|------|-----------|----------------------|-----------|
| **W1** | SHA-256 / SHA-384 | FIPS 180-4 compliant; test vectors; `sha256()` / `sha384()` APIs | ✅ SHA-256 as `src/crypto/sha256.c` (not `src/tls/sha256.c`) and SHA-512 as `src/tls/sha512.c` (Ed25519 needs it). ❌ SHA-384 — nothing in the shipped profile uses it |
| **W2** | AES-256-GCM | `src/tls/aes_gcm.c` — NIST SP 800-38D; key schedule, GCM encrypt/decrypt, auth tag | ❌ Not built. Deferred to Phase 21 — see the amended ADR in §3 (no AES-NI path under the pure-C constraint; portable AES-GCM is cache-timing-hazardous) |
| **W3** | ChaCha20-Poly1305 | RFC 8439 stream cipher + AEAD construction; IETF test vectors | ✅ `src/tls/chacha20.c`, `poly1305.c`, `chacha20poly1305.c` — RFC 8439 known-answer tests in `TlsCryptoTests` |
| **W4** | X25519 Key Exchange | RFC 7748; pure C field arithmetic (mod 2^255-19); scalar multiplication | ✅ `src/tls/x25519.c` on `src/tls/field25519.c` (constant-time limb arithmetic). Ed25519 (`src/tls/ed25519.c`) was added on top — not in the original plan, but required once RSA/ECDSA were ruled out |
| **W5** | HKDF + Key Derivation | RFC 5869 Extract + Expand over HMAC-SHA256/384; TLS 1.3 key schedule helpers | ✅ `src/tls/hkdf.c` (SHA-256 only) + `hkdf_expand_label()` per RFC 8446 §7.1; TLS 1.3 key schedule in `src/tls/key_schedule.c`; RFC 5869 and RFC 8448 vectors |
| **W6** | Integration & Hardening | Constant-time comparison; key material zeroed; Valgrind clean; unified crypto header | 🟡 Partial: primitives chosen to be constant-time by construction; key material scrubbed via `secure_zero()`; ASan/UBSan over the TLS suites in `tls-check`. ❌ No Valgrind coverage of TLS code — the `primary-checks` Valgrind gate builds from `Dockerfile.dev` with TLS OFF, so no `src/tls` object or `test_tls*` binary exists in that image. ❌ No unified crypto header — headers stayed per-module (`src/tls/*.h`) rather than one `crypto.h` |

**Still open from Phase 11** → carried into Phase 21: AES-256-GCM, and an external audit of everything above.

### Phase 12: TLS 1.3 Handshake & HTTPS — DELIVERED in v2.0.0 (planned as v1.2.0, 6 weeks)

> **EXPERIMENTAL · UNAUDITED.** Server-side only; threaded mode only; native-only.
> Single profile: `TLS_CHACHA20_POLY1305_SHA256` + X25519 + Ed25519.

| Week | Milestone | Planned deliverables | Delivered |
|------|-----------|----------------------|-----------|
| **W7** | TLS Record Layer | Record framing; content-type encryption; padding; size limits | ✅ `src/tls/record.c` — 2^14 plaintext limit and fragmentation, per RFC 8446 |
| **W8** | TLS Handshake Part 1 | ClientHello parsing; ServerHello; supported_versions; key_share (X25519) | ✅ `src/tls/handshake.c`, `wire.c`, `server_handshake.c` — plus HelloRetryRequest with the RFC 8446 §4.4.1 synthetic `message_hash` transcript rewrite, which the plan did not anticipate |
| **W9** | TLS Handshake Part 2 | EncryptedExtensions; Certificate; CertificateVerify; Finished; transcript hash | ✅ `src/tls/server_handshake.c`, `handshake_auth.c` |
| **W10** | Certificate Handling | `src/tls/x509.c` — X.509 DER/PEM parser; chain building; RSA/ECDSA verification | 🟡 Partial: `src/tls/der.c`, `pem.c`, `ed25519_key.c` *parse* the PKCS#8 Ed25519 private key, malformed-input hardened; the certificate is only PEM-decoded to DER and sent opaquely. ❌ No X.509 structure parsing, no chain building, no RSA/ECDSA verification — Phase 21 |
| **W11** | HTTPS Server Integration | `http_server_enable_tls()`; ALPN; HTTPS example; SNI callback | ✅ `http_server_enable_tls()` (`include/kamran.k`) taking **PEM buffers plus explicit lengths**, not file paths; ALPN negotiating `http/1.1`; `examples/tls_server.c` (which reads the files itself). ❌ No SNI callback — Phase 21 |
| **W12** | TLS Testing & Security Audit | curl/browser validation; session resumption; TLS unit tests; timing analysis | 🟡 Partial: 7 TLS ctest suites (`TlsTests`, `TlsCryptoTests`, `TlsParseTests`, `TlsTransportTests`, `TlsFuzzTests`, `TlsHttpTests`, `TlsInteropOpenssl`), a deterministic fuzzer, and a real `openssl s_client` TLS 1.3 handshake + HTTPS round-trip in CI. ❌ No session resumption / 0-RTT. ❌ No browser page-load (Ed25519-only certs). ❌ **No external audit** |

**Still open from Phase 12** → carried into Phase 21: RSA/ECDSA certificates and chain validation (and
with them browser interop), SNI, session resumption, client mode, async-mode TLS, WebSocket-over-TLS,
external audit.

### Phase 13: HTTP/2 Protocol — v2.1.0 (5 weeks)

| Week | Milestone | Deliverables | Dependencies |
|------|-----------|-------------|--------------|
| **W13** | Binary Framing | `src/http2/frame.c` — frame parser/serializer for all 10 frame types (DATA, HEADERS, PRIORITY, RST_STREAM, SETTINGS, PUSH_PROMISE, PING, GOAWAY, WINDOW_UPDATE, CONTINUATION) | None |
| **W14** | HPACK Compression | `src/http2/hpack.c` — static table (61 entries); Huffman coding; integer encoding/decoding; header block encoding/decoding; HPACK test vectors | None |
| **W15** | Stream Multiplexing | `src/http2/stream.c` — stream state machine (idle→open→half-closed→closed); stream priority tree; connection-level + stream-level flow control; MAX_CONCURRENT_STREAMS enforcement | W13 |
| **W16** | HTTP/2 Server Integration | `http_server_enable_http2()` API; connection preface handling; settings negotiation; integration with existing router; server push API; h2c upgrade support | W13–W15, Phase 12 (ALPN) |
| **W17** | HTTP/2 Testing | `curl --http2` validation; multiplexed request tests; flow control tests; HPACK bomb protection; 50+ HTTP/2 unit tests; performance comparison vs HTTP/1.1 | W13–W16 |

### Phase 14: Persistent Storage Engine — v2.2.0 (5 weeks)

| Week | Milestone | Deliverables | Dependencies |
|------|-----------|-------------|--------------|
| **W18** | B-tree Core | `src/storage/btree.c` — on-disk B-tree with configurable page size (4KB default); page allocation; node split/merge; key-value insert/lookup/delete | None |
| **W19** | Write-Ahead Log | `src/storage/wal.c` — append-only WAL; fsync-on-commit; CRC32 checksums per record; WAL replay on crash recovery; WAL truncation after checkpoint | W18 |
| **W20** | Transaction Support | `src/storage/transaction.c` — begin/commit/rollback; MVCC snapshot isolation; read-only transactions without locks; deadlock detection timeout | W18–W19 |
| **W21** | Iterator & Query Interface | `storage_iterator_t` — forward/reverse iteration; range queries (start_key, end_key); prefix scan; cursor-based pagination | W18 |
| **W22** | Integration & Persistence APIs | `storage_open(path)` / `storage_close()` lifecycle; `storage_get()` / `storage_put()` / `storage_delete()`; session store backend; cache persistence backend; 40+ storage tests | W18–W21 |

### Phase 15: Advanced Middleware & Content — v2.3.0 (4 weeks)

| Week | Milestone | Deliverables | Dependencies |
|------|-----------|-------------|--------------|
| **W23** | Directory Listing & SSE | `src/middleware_dirlist.c` — auto-generated HTML directory indexes with sortable columns; `src/sse.c` — Server-Sent Events (SSE) with event ID, retry, multi-line data | None |
| **W24** | Content Negotiation & Streaming | `Accept` / `Accept-Language` / `Accept-Encoding` parsing with quality values; request body streaming (chunked read callback); response streaming (chunked write callback) | None |
| **W25** | Route Groups & Regex Routes | `router_group_create(prefix)` — scoped middleware per group; `router_add_regex_route()` — POSIX `regcomp()`/`regexec()` based pattern matching; named captures | None |
| **W26** | Testing & Integration | 30+ middleware tests; SSE example (`examples/sse_server.c`); directory listing example; streaming upload example; backward-compatible with existing router API | W23–W25 |

### Phase 16: Multi-Process Architecture — v2.4.0 (5 weeks)

| Week | Milestone | Deliverables | Dependencies |
|------|-----------|-------------|--------------|
| **W27** | Master-Worker Model | `src/worker.c` — `fork()`-based worker spawning; master process signal handling (SIGCHLD, SIGTERM, SIGHUP); worker PID tracking; automatic restart on crash | None |
| **W28** | Socket Sharing & Accept | `SO_REUSEPORT` per-worker accept (Linux 3.9+); fallback to master-distributes-fd via `sendmsg()`/`SCM_RIGHTS` on older kernels/macOS; accept mutex for thundering herd prevention | W27 |
| **W29** | Zero-Downtime Reload | SIGHUP triggers: master forks new workers → new workers start accepting → old workers drain and exit; binary upgrade via `execve()` with inherited listening socket | W27–W28 |
| **W30** | Metrics Aggregation | Per-worker metrics collection; master aggregates via shared memory or pipe; `/metrics` endpoint serves combined JSON; worker health monitoring | W27, Phase 9 (metrics) |
| **W31** | Testing & Stabilization | `http_server_set_workers(n)` API; multi-worker stress tests (10 workers, 10K requests); graceful shutdown of all workers; Valgrind on single-worker mode; 25+ worker tests | W27–W30 |

### Phase 17: Cross-Platform Hardening — v2.5.0 (4 weeks)

| Week | Milestone | Deliverables | Dependencies |
|------|-----------|-------------|--------------|
| **W32** | Platform Abstraction Layer | `src/platform.h` + `src/platform_posix.c` + `src/platform_win32.c` — unified API for: socket operations, file I/O, threading, time, random bytes, memory-mapped files | None |
| **W33** | Windows IOCP Backend | `src/event_loop_iocp.c` — IOCP-based event loop; overlapped I/O; completion port per-thread; integration with platform abstraction; Windows socket initialization (`WSAStartup`) | W32 |
| **W34** | BSD & CI Matrix | FreeBSD/OpenBSD CI runners (or cross-compilation); BSD-specific `kqueue` flags; `arc4random_buf()` for random bytes; platform compatibility matrix documentation | W32 |
| **W35** | MSVC Build & Testing | CMake MSVC generator support; `#pragma` warning suppression mapping; Windows-specific test adaptations; end-to-end CI: Linux (GCC/Clang) + macOS (Clang) + Windows (MSVC) + FreeBSD | W32–W34 |

### Phase 18: Developer Experience & Configuration — v2.6.0 (5 weeks)

| Week | Milestone | Deliverables | Dependencies |
|------|-----------|-------------|--------------|
| **W36** | INI Configuration Parser | `src/config.c` — INI file parser with sections, key=value pairs, comments (`#`, `;`); `config_load(path)` / `config_get(section, key)` / `config_get_int()` / `config_get_bool()`; max value length enforcement | None |
| **W37** | Plugin Architecture | `src/plugin.c` — compile-time plugin registration; `plugin_register(name, init_fn, cleanup_fn)` macro; plugin lifecycle hooks (on_server_start, on_request, on_response, on_server_stop); plugin dependency ordering | None |
| **W38** | Advanced Templates | `src/template_v2.c` — Mustache-compatible syntax (`{{#section}}`, `{{/section}}`, `{{>partial}}`); template inheritance (`{{<base}}`); auto-escaping (HTML context by default, `{{{raw}}}` for unescaped); compiled template caching | None |
| **W39** | Debug Mode & API Versioning | `http_server_set_debug(true)` — verbose request/response logging with headers, timing per middleware, memory allocation tracking; `router_version_group("v1", router_v1)` — URL-prefix and `Accept-Version` header-based API versioning | None |
| **W40** | Testing & Documentation | 35+ developer experience tests; configuration example; plugin example; advanced template example; debug mode documentation; API versioning tutorial | W36–W39 |

### Phase 19: HTTP/3 & QUIC — v2.7.0 (6 weeks)

| Week | Milestone | Deliverables | Dependencies |
|------|-----------|-------------|--------------|
| **W41** | UDP Socket Layer | `src/quic/udp.c` — UDP socket creation; `recvmmsg()`/`sendmmsg()` for batch I/O; `SO_REUSEPORT` for multi-worker UDP; GSO/GRO support (Linux); event loop integration for UDP readability | None |
| **W42** | QUIC Transport Core | `src/quic/transport.c` — QUIC packet parsing (Initial, Handshake, 0-RTT, 1-RTT); variable-length integer encoding; connection ID management; packet number encryption/decryption | Phase 11 (ChaCha20-Poly1305, delivered v2.0.0). Note: QUIC's Initial packets are AES-GCM-protected by RFC 9001, so this also depends on the deferred AES-GCM work in Phase 21 |
| **W43** | QUIC Handshake | `src/quic/handshake.c` — integrates TLS 1.3 (Phase 12) as QUIC crypto; Initial packet protection; handshake completion; retry tokens for address validation; anti-amplification limits | Phase 12 (TLS 1.3, delivered v2.0.0 — but QUIC needs a client side and an AEAD the shipped stack does not have; see Phase 21), W42 |
| **W44** | QUIC Streams & Flow Control | `src/quic/stream.c` — bidirectional and unidirectional streams; stream-level and connection-level flow control; MAX_STREAMS enforcement; stream prioritization | W42 |
| **W45** | HTTP/3 Framing & QPACK | `src/http3/frame.c` — HTTP/3 frame types (DATA, HEADERS, CANCEL_PUSH, SETTINGS, PUSH_PROMISE, GOAWAY); `src/http3/qpack.c` — QPACK header compression (static table + dynamic table with encoder/decoder streams) | W44 |
| **W46** | HTTP/3 Server Integration | `http_server_enable_http3()` API; Alt-Svc header for HTTP/3 discovery; connection migration support; `examples/http3_server.c`; 50+ QUIC/HTTP3 unit tests | W41–W45 |

### Phase 20: Release Engineering & Ecosystem — v3.0.0 (4 weeks)

| Week | Milestone | Deliverables | Dependencies |
|------|-----------|-------------|--------------|
| **W47** | CLI Tooling | `tools/weblib-cli.c` — `weblib init` (project scaffolding with CMakeLists.txt + main.c + Dockerfile); `weblib routes` (list registered routes from compiled server); `weblib config validate` (check INI file syntax) | Phase 18 (config) |
| **W48** | Observability Export | `src/prometheus.c` — `/metrics` endpoint in Prometheus exposition format (counters, gauges, histograms); `src/tracing.c` — W3C Trace Context (`traceparent` header) propagation; span creation/completion; trace ID in log output | Phase 9 (metrics) |
| **W49** | Fuzz Testing & Performance CI | `tests/fuzz/` — fuzz harnesses for HTTP parser, JSON parser, TLS handshake, HPACK decoder, QUIC packet parser (using libFuzzer-compatible API); benchmark regression CI gate (±10% tolerance on req/s) | All phases |
| **W50** | v3.0.0 Release | `CHANGELOG.md` finalization; migration guide (v2→v3 breaking changes); complete API reference update; `docs/architecture.md` (system design document); semantic version tag; GitHub Release with binary artifacts | All phases |

---

## 7. Atomic Task Breakdown

### Phase 11 — TLS Foundation: Crypto Primitives — DELIVERED in v2.0.0

> The planned tasks are kept below for provenance, each annotated with where it actually landed.
> Everything here is **EXPERIMENTAL and UNAUDITED**, compiled only with `-DWEBLIB_ENABLE_TLS=ON`.

#### 11.1 Hashes and HMAC (planned as "SHA-256 / SHA-384")
| # | Task | Planned file(s) | Status / where it landed |
|---|------|-----------------|--------------------------|
| 11.1.1 | SHA-256 core (init, update, final) | `src/tls/sha256.c` | ✅ Shipped as `src/crypto/sha256.c` — it predates the TLS work and is shared with the rest of the library |
| 11.1.2 | SHA-384 (truncated SHA-512) | `src/tls/sha384.c` | ❌ Never built. `src/tls/sha512.c` shipped instead, because Ed25519 needs SHA-512; nothing in the shipped profile needs SHA-384 |
| 11.1.3 | HMAC-SHA256 / HMAC-SHA384 | `src/tls/hmac.c` | 🟡 HMAC-SHA256 shipped as `hmac_sha256()` in `src/crypto/sha256.c` and is what `src/tls/hkdf.c` builds on; HMAC-SHA384 not built |
| 11.1.4 | Known-answer tests | `tests/test_crypto.c` | ✅ Shipped as `tests/test_tls_crypto.c` (`TlsCryptoTests`), RFC vectors per primitive |
| 11.1.5 | Constant-time comparison utility | `src/tls/crypto_util.c` | ✅ `secure_compare()` already existed in the security-utilities module and is reused |

#### 11.2 AES-256-GCM — NOT BUILT, deferred to Phase 21
| # | Task | Planned file(s) | Status |
|---|------|-----------------|--------|
| 11.2.1 | AES key schedule (128/256-bit) | `src/tls/aes.c` | ❌ Deferred |
| 11.2.2 | GCM mode (GHASH + CTR) | `src/tls/aes_gcm.c` | ❌ Deferred |
| 11.2.3 | AEAD encrypt/decrypt API | `src/tls/aes_gcm.c` | ❌ Deferred |
| 11.2.4 | GCM tag verification (constant-time) | `src/tls/aes_gcm.c` | ❌ Deferred |

> **Why it was deferred, not cancelled.** With no AES-NI path available under the zero-dependency
> pure-C constraint, a portable software AES-GCM (T-tables + GHASH) carries cache-timing risk that
> ChaCha20-Poly1305 does not. Shipping one constant-time-by-construction suite was the safer first
> cut. AES-GCM is still wanted — for hardware-accelerated peers and as a prerequisite for QUIC
> Initial packet protection (RFC 9001) — and is tracked in Phase 21.

#### 11.3 ChaCha20-Poly1305 — DELIVERED
| # | Task | File(s) | Status |
|---|------|---------|--------|
| 11.3.1 | ChaCha20 quarter-round + block function | `src/tls/chacha20.c` | ✅ RFC 8439 §2.1–2.3 vectors pass |
| 11.3.2 | ChaCha20 stream cipher | `src/tls/chacha20.c` | ✅ RFC 8439 §2.4 vector passes |
| 11.3.3 | Poly1305 one-time authenticator | `src/tls/poly1305.c` | ✅ RFC 8439 §2.5 vectors pass |
| 11.3.4 | AEAD_CHACHA20_POLY1305 construction | `src/tls/chacha20poly1305.c` (planned as `chacha20_poly1305.c`) | ✅ RFC 8439 §2.8 AEAD vector passes |

#### 11.4 X25519 Key Exchange — DELIVERED (+ Ed25519, unplanned)
| # | Task | File(s) | Status |
|---|------|---------|--------|
| 11.4.1 | Field arithmetic (mod 2^255-19) | `src/tls/field25519.c` (planned inside `x25519.c`) | ✅ Constant-time limb arithmetic, factored into its own module |
| 11.4.2 | Montgomery ladder scalar multiplication | `src/tls/x25519.c` | ✅ RFC 7748 §5.2 scalar-mult vectors and the §6.1 Alice/Bob key-exchange vectors pass |
| 11.4.3 | Key generation (clamp + multiply) | `src/tls/x25519.c` | ✅ Shared secret matches the RFC output |
| 11.4.4 | Timing-safe implementation review | `src/tls/x25519.c`, `field25519.c` | 🟡 No secret-dependent branches or indexing by construction — but this is a self-review, **not** an external audit. Audit is Phase 21 |
| 11.4.5 | Ed25519 signatures (RFC 8032) | `src/tls/ed25519.c` | ✅ **Added, not planned.** Needed for CertificateVerify once RSA/ECDSA were ruled out |

#### 11.5 HKDF Key Derivation — DELIVERED
| # | Task | File(s) | Status |
|---|------|---------|--------|
| 11.5.1 | HKDF-Extract | `src/tls/hkdf.c` | ✅ RFC 5869 vectors (SHA-256) pass |
| 11.5.2 | HKDF-Expand | `src/tls/hkdf.c` | ✅ RFC 5869 vectors pass |
| 11.5.3 | TLS 1.3 key schedule helpers | `src/tls/hkdf.c`, `src/tls/key_schedule.c` | ✅ `hkdf_expand_label()` per RFC 8446 §7.1; `early_secret` and `derived` anchored on the RFC 8448 trace, the rest of the chain derived over a fixed ECDHE (the RFC 7748 §6.1 shared secret) and synthetic transcript hashes, cross-checked against an independent out-of-tree HKDF-SHA256 reference — not against the RFC 8448 trace |

---

### Phase 12 — TLS 1.3 Handshake & HTTPS — DELIVERED in v2.0.0

> **EXPERIMENTAL · UNAUDITED.** Server-side only, threaded mode only, native-only, one profile.

#### 12.1 TLS Record Layer — DELIVERED
| # | Task | File(s) | Status |
|---|------|---------|--------|
| 12.1.1 | Record framing (read/write) | `src/tls/record.c` (planned as `tls_record.c`) | ✅ Enforces the 2^14 plaintext limit from RFC 8446 §5.1 |
| 12.1.2 | Record encryption (AEAD) | `src/tls/record.c` | ✅ Per-record nonce; inner content type hiding |
| 12.1.3 | Record layer buffering | `src/tls/record.c`, `tls_khannection.c` | ✅ Partial TCP reads handled; handshake messages reassembled across records; outbound responses fragmented across records |

#### 12.2 Handshake State Machine — DELIVERED
| # | Task | File(s) | Status |
|---|------|---------|--------|
| 12.2.1 | ClientHello parse → ServerHello | `src/tls/handshake.c`, `wire.c`, `server_handshake.c` | ✅ `supported_versions` (TLS 1.3 only), `key_share` (X25519), cipher-suite selection |
| 12.2.2 | HelloRetryRequest path | `src/tls/server_handshake.c` | ✅ Including the RFC 8446 §4.4.1 synthetic `message_hash` transcript rewrite |
| 12.2.3 | EncryptedExtensions → Certificate → CertificateVerify → Finished | `src/tls/server_handshake.c`, `handshake_auth.c` | ✅ Ed25519 CertificateVerify; transcript hash maintained across the whole flow |
| 12.2.4 | Sans-IO engine + blocking-socket adapter | `src/tls/tls_khannection.c`, `src/tls/tls_transport.c` | ✅ The state machine never touches a socket; the adapter adds a read deadline and an empty-record flood bound |
| 12.2.5 | Handshake test coverage | `TlsTests`, `TlsParseTests`, `TlsTransportTests`, `TlsFuzzTests` | ✅ Plus a deterministic fuzzer over the untrusted-input path |

> ❌ Not delivered here, tracked in Phase 21: client-side handshake, session resumption / PSK / 0-RTT,
> SNI callback, post-handshake key update beyond what the record layer needs.

#### 12.3 Certificate Handling — PARTIALLY DELIVERED
| # | Task | File(s) | Status |
|---|------|---------|--------|
| 12.3.1 | DER/ASN.1 reader | `src/tls/der.c` | ✅ Bounds-checked, malformed-input hardened, fuzzed |
| 12.3.2 | PEM decode | `src/tls/pem.c` | ✅ `CERTIFICATE` block + PKCS#8 `PRIVATE KEY` block |
| 12.3.3 | Ed25519 key loading | `src/tls/ed25519_key.c` | ✅ Public key derived from the private key at load time |
| 12.3.4 | X.509 chain building + RSA/ECDSA verification | — | ❌ Not built. The server presents one self-supplied certificate; it does not validate chains. Phase 21 |

#### 12.4 HTTPS Integration — DELIVERED
Shipped as `http_server_enable_tls()` — declared at `include/kamran.k`:

```c
int http_server_enable_tls(http_server_t *server,
                           const char *cert_pem, size_t cert_len,
                           const char *key_pem,  size_t key_len);
```

It takes **in-memory PEM buffers with explicit lengths, not file paths** — `examples/tls_server.c`
reads the files itself and passes the buffers. Call it before `http_server_listen()`.

| # | Task | File(s) | Status |
|---|------|---------|--------|
| 12.4.1 | `http_server_enable_tls()`: PEM/PKCS#8 decode, Ed25519 pubkey derivation, cert DER retained | `include/kamran.k`, `src/http_server.c` | ✅ |
| 12.4.2 | TLS handshake on every accepted connection before HTTP is spoken | `src/http_server.c` | ✅ |
| 12.4.3 | Request/response I/O routed through the TLS transport | `src/http_server.c`, `src/tls/tls_transport.c` | ✅ |
| 12.4.4 | ALPN negotiation | `src/tls/server_handshake.c` | ✅ `http/1.1` only — `h2` waits on Phase 13 |
| 12.4.5 | Worked HTTPS example server | `examples/tls_server.c` | ✅ Built only when `WEBLIB_ENABLE_TLS=ON` |
| 12.4.6 | Integration + real-client interop suites | `TlsHttpTests`, `TlsInteropOpenssl` | ✅ `openssl s_client` TLS 1.3 handshake + HTTPS round-trip, a >16 KiB response fragmented across records, and two requests on one connection |

Scope limits that are deliberate and still open (Phase 21):
- **Threaded mode only** — `http_server_enable_tls()` returns -1 when async mode is set, and setting async mode is refused once TLS is on.
- **Native only** — no TLS in the WASM or Cloudflare Workers builds.
- **No WebSocket over TLS** — a WS upgrade on a TLS connection is answered with 503.
- **No browser page-load** — Ed25519-only server certificates have limited and inconsistent browser support.
- `WEBLIB_TLS_TEST_HOOKS` (default OFF) gates a deterministic-RNG seam used only by `TlsHttpTests`. It must never be enabled in a production build.

---

### Phase 14 — Persistent Storage Engine

#### 14.1 B-tree Core
| # | Task | File(s) | Acceptance Criteria | Est. |
|---|------|---------|-------------------|------|
| 14.1.1 | On-disk page layout + page allocator | `src/storage/btree.c` | Configurable page size (4KB default); free-list reuse of released pages | 1d |
| 14.1.2 | Node serialization / deserialization | `src/storage/btree.c` | Leaf and internal node encode/decode round-trip; CRC32 per page | 1d |
| 14.1.3 | Key-value insert with node split | `src/storage/btree.c` | Split propagates to root; tree stays balanced, depth ≤ log(N) | 1d |
| 14.1.4 | Lookup and delete with node merge | `src/storage/btree.c` | Underflow triggers merge/rebalance; deleted keys not retrievable | 1d |
| 14.1.5 | B-tree unit tests | `tests/test_storage.c` (new) | Insert 100K keys, all retrieved correctly; balanced depth verified | 1d |

#### 14.2 Write-Ahead Log
| # | Task | File(s) | Acceptance Criteria | Est. |
|---|------|---------|-------------------|------|
| 14.2.1 | WAL record format | `src/storage/wal.c` | Type + length + CRC32 + payload; sequential write | 3h |
| 14.2.2 | WAL append + fsync | `src/storage/wal.c` | Durable writes; configurable sync mode (per-commit / periodic) | 3h |
| 14.2.3 | WAL replay on recovery | `src/storage/wal.c` | Replay uncommitted records; skip corrupted (CRC mismatch) | 4h |
| 14.2.4 | WAL checkpoint + truncation | `src/storage/wal.c` | Flush dirty pages to B-tree file; truncate WAL | 3h |

#### 14.3 Transactions
| # | Task | File(s) | Acceptance Criteria | Est. |
|---|------|---------|-------------------|------|
| 14.3.1 | Begin / commit / rollback | `src/storage/transaction.c` | Transaction isolation; rollback discards WAL records | 4h |
| 14.3.2 | Read-only transactions | `src/storage/transaction.c` | Snapshot reads without locks; no WAL writes | 2h |
| 14.3.3 | Concurrent read-write | `src/storage/transaction.c` | Single writer + multiple readers; no blocking reads | 3h |

#### 14.4 Iterator & Public API
| # | Task | File(s) | Acceptance Criteria | Est. |
|---|------|---------|-------------------|------|
| 14.4.1 | Forward/reverse iterator | `src/storage/iterator.c` | Cursor-based traversal; `storage_iterator_next()` / `_prev()` | 3h |
| 14.4.2 | Range query | `src/storage/iterator.c` | `storage_iterator_seek(start_key)` + iterate until end_key | 2h |
| 14.4.3 | Public API surface | `include/weblib.h`, `src/storage/storage.c` | `storage_open()`, `storage_close()`, `storage_get()`, `storage_put()`, `storage_delete()` | 3h |
| 14.4.4 | Integration with session store | `src/session.c` | Optional persistent session backend via storage engine | 2h |

---

### Phase 15 — Advanced Middleware & Content (Summary)

| # | Task | Est. |
|---|------|------|
| 15.1.1–15.1.3 | Directory listing middleware (HTML index, sortable, configurable) | 2d |
| 15.2.1–15.2.3 | Server-Sent Events (SSE) with event ID, retry, keep-alive | 2d |
| 15.3.1–15.3.3 | Content negotiation (Accept parsing, quality values, format selection) | 2d |
| 15.4.1–15.4.3 | Request/response streaming (chunked read/write callbacks) | 2d |
| 15.5.1–15.5.4 | Route groups with scoped middleware + regex routes (POSIX regex) | 3d |
| 15.6.1–15.6.2 | Testing + examples (SSE server, streaming upload) | 2d |

### Phase 16 — Multi-Process Architecture (Summary)

| # | Task | Est. |
|---|------|------|
| 16.1.1–16.1.4 | Master-worker fork model with PID tracking and signal handling | 3d |
| 16.2.1–16.2.3 | Socket sharing (SO_REUSEPORT or SCM_RIGHTS) with accept balancing | 3d |
| 16.3.1–16.3.3 | Zero-downtime reload (SIGHUP, new workers, old workers drain) | 3d |
| 16.4.1–16.4.3 | Per-worker metrics aggregation via pipe/shared memory | 2d |
| 16.5.1–16.5.3 | Multi-worker stress tests + API (`http_server_set_workers()`) | 2d |

### Phase 17 — Cross-Platform Hardening (Summary)

| # | Task | Est. |
|---|------|------|
| 17.1.1–17.1.4 | Platform abstraction layer (socket, file, thread, time, random) | 3d |
| 17.2.1–17.2.4 | Windows IOCP event loop backend with completion ports | 4d |
| 17.3.1–17.3.3 | BSD kqueue refinements + FreeBSD/OpenBSD CI | 2d |
| 17.4.1–17.4.3 | MSVC build support + Windows CI runner | 3d |
| 17.5.1–17.5.2 | Platform compatibility documentation + matrix | 1d |

### Phase 18 — Developer Experience & Configuration (Summary)

| # | Task | Est. |
|---|------|------|
| 18.1.1–18.1.4 | INI configuration parser with sections, validation, defaults | 2d |
| 18.2.1–18.2.4 | Plugin architecture (compile-time registration, lifecycle hooks) | 3d |
| 18.3.1–18.3.4 | Advanced templates (Mustache, includes, inheritance, auto-escape) | 3d |
| 18.4.1–18.4.3 | Debug mode (verbose logging, timing, memory tracking) | 2d |
| 18.5.1–18.5.3 | API versioning (URL prefix + Accept-Version header) | 2d |

### Phase 19 — HTTP/3 & QUIC (Summary)

| # | Task | Est. |
|---|------|------|
| 19.1.1–19.1.3 | UDP socket layer with batch I/O and event loop integration | 3d |
| 19.2.1–19.2.4 | QUIC transport (packet parsing, connection IDs, packet number encryption) | 5d |
| 19.3.1–19.3.3 | QUIC handshake (TLS 1.3 integration, retry tokens, anti-amplification) | 4d |
| 19.4.1–19.4.3 | QUIC streams and flow control (bidirectional/unidirectional) | 3d |
| 19.5.1–19.5.3 | HTTP/3 framing + QPACK header compression | 4d |
| 19.6.1–19.6.3 | HTTP/3 server integration + connection migration + example | 3d |

### Phase 20 — Release Engineering & Ecosystem (Summary)

| # | Task | Est. |
|---|------|------|
| 20.1.1–20.1.4 | CLI tool (init, routes, config validate, build) | 3d |
| 20.2.1–20.2.3 | Prometheus metrics export (exposition format, histograms) | 2d |
| 20.3.1–20.3.3 | Trace context propagation (W3C traceparent, span lifecycle) | 2d |
| 20.4.1–20.4.3 | Fuzz testing suite (HTTP, JSON, TLS, HPACK, QUIC parsers) | 3d |
| 20.5.1–20.5.2 | Performance regression CI gate (benchmark baseline ±10%) | 1d |
| 20.6.1–20.6.3 | v3.0.0 release (CHANGELOG, migration guide, API docs, release automation) | 2d |

### Phase 21 — Security Residuals & TLS Hardening — unscheduled (8 weeks)

This is where the Phase 11/12 scope that v2.0.0 did **not** deliver lives, together with the
security items left over from the pre-2026-03 "Advanced Security Hardening" plan preserved below.
None of it is started. Nothing here has been quietly dropped.

| Week | Milestone | Scope | Why it is still open |
|------|-----------|-------|----------------------|
| **W51** | External cryptographic audit of `src/tls/` | Third-party review of the primitives, the record layer and the handshake state machine; publish findings and fixes | This is the gate that removes the word **UNAUDITED** from the docs. Nothing else in this phase matters as much |
| **W52** | RSA / ECDSA certificates + X.509 chain validation | Signature verification, chain building, expiry and basic-constraints checks | Unlocks real browser page-load, which Ed25519-only certificates cannot reach. Note this reintroduces bignum code, which the ADR in §3 deliberately avoided — it needs its own decision record |
| **W53** | AES-256-GCM suite | `TLS_AES_256_GCM_SHA384` alongside the ChaCha20 suite | Wanted for hardware-accelerated peers and required by RFC 9001 for QUIC Initial packets (Phase 19). Blocked on a credible answer to software AES cache-timing |
| **W54** | TLS in async mode | `http_server_enable_tls()` returns -1 under async mode today; the sans-IO engine already supports non-blocking drive | The engine was built sans-IO precisely so this is an adapter problem, not a redesign |
| **W55** | WebSocket over TLS | A WS upgrade on a TLS connection is answered with 503 today | The WS frame loop bypasses the TLS transport; it needs to route through the same read/write path |
| **W56** | Session resumption + SNI | PSK / 0-RTT resumption; SNI callback for virtual hosting | 0-RTT carries replay risk and must be opt-in with anti-replay, so this is deliberately late |
| **W57** | TLS client mode | Outbound TLS 1.3 client handshake; certificate pinning | Prerequisite for any outbound HTTPS the library does itself |
| **W58** | Remaining security middleware | PBKDF2-HMAC-SHA256 + `password_hash_create()` / `password_hash_verify()`; request-ID middleware; IP allowlist/denylist with CIDR; per-route body size limits and `Content-Length` enforcement before buffering; `mlock()` on TLS private-key pages (`secure_zero()` already ships, `mlock()` does not) | Carried over from the superseded plan below — none of it was ever built |

---

> ## ⚠️ SUPERSEDED — historical plan, retained for provenance
>
> Everything from here to the end of §7 is the **pre-2026-03 "Phase 11 — Advanced Security
> Hardening" plan**. It was written before the TLS design was settled and it double-books the
> Phase 11 number and the W18–W23 week range (Phase 14 owns W18–W22).
>
> **Do not implement from it.** It plans **TLS 1.2**; the ADR in §3 chose TLS 1.3, and the shipped
> server accepts TLS 1.3 only and rejects 1.2 by design. Its file names, cipher suites and
> acceptance criteria do not match what exists.
>
> The parts of it that are still genuinely wanted have been re-homed into **Phase 21 (W58)** above.
> Rows below are annotated with what actually happened.

### Phase 11 (superseded) — Advanced Security Hardening — originally targeted v1.1.0 (6 weeks)

> **Vision**: No one should ever worry about compromising their keys.
> Phase 11 closes every remaining security gap identified in the threat model,
> bringing the library to state-of-the-art security without a single external dependency.

| Week | Milestone | Deliverables | Outcome |
|------|-----------|-------------|---------|
| **W18** | Crypto Primitives (Foundation) | `src/crypto/sha256.c` (NIST FIPS 180-4 test vectors); `src/crypto/hmac_sha256.c` (RFC 2104); `src/crypto/aes_gcm.c` (NIST SP 800-38D, 128+256-bit keys) | 🟡 SHA-256 shipped as `src/crypto/sha256.c`; HMAC-SHA256 shipped as `hmac_sha256()` in `src/crypto/sha256.c`; **AES-GCM never built** → Phase 21 W53 |
| **W19** | Key Derivation & Password Hashing | `src/crypto/pbkdf2.c` (RFC 2898, HMAC-SHA256, configurable iterations); `src/crypto/hkdf.c` (RFC 5869 extract+expand); `password_hash_create()` / `password_hash_verify()` API; automatic salt via `secure_random_bytes()` | 🟡 HKDF shipped as `src/tls/hkdf.c` (TLS-internal, RFC 5869 + RFC 8446 §7.1); **PBKDF2 and password hashing never built** → Phase 21 W58 |
| **W20** | TLS Record Layer & Server Handshake — superseded by the delivered TLS 1.3 work | Delivered instead as: `src/tls/record.c` (TLS **1.3** record layer, 2^14 plaintext limit, fragmentation); `src/tls/handshake.c` + `src/tls/server_handshake.c` (TLS 1.3 server state machine, HelloRetryRequest, ALPN `http/1.1`); `src/tls/pem.c` + `src/tls/der.c` + `src/tls/ed25519_key.c` (PEM/DER certificate loading + Ed25519 private-key parsing; the certificate is not parsed as X.509). EXPERIMENTAL · UNAUDITED | ✅ Delivered in v2.0.0 as TLS 1.3, not TLS 1.2. `mlock()` on key pages was **not** implemented → Phase 21 W58 |
| **W21** | TLS Handshake (Part 2) & Server Integration | Delivered instead as `http_server_enable_tls(server, cert_pem, cert_len, key_pem, key_len)` — PEM **buffers with lengths**, not file paths; `examples/tls_server.c`; validated by `openssl s_client -tls1_3` (`TlsInteropOpenssl`), **not** `curl --tlsv1.2`, which the server rejects by design | ✅ Delivered in v2.0.0. Key material is scrubbed on server destroy |
| **W22** | Request ID & IP Access Control Middleware | `src/middleware_request_id.c` (UUID v4 / hex, `X-Request-Id` header, logging integration); `src/middleware_ip_access.c` (allowlist/denylist with CIDR support) | ❌ Never built → Phase 21 W58 |
| **W23** | Security Audit Tooling & Hardening | Fuzz testing harness (`tests/fuzz/`); ASan/MSan CI integration; per-route body size limits; `Content-Length` enforcement before buffering; full regression pass | 🟡 A deterministic TLS fuzzer shipped (`TlsFuzzTests`) and the `tls-check` CI job runs an ASan/UBSan TLS build; **an HTTP-parser fuzz harness, per-route body limits and pre-buffer `Content-Length` enforcement were not built** → Phase 21 W58 |

#### Phase 11 (superseded) — Atomic Task Breakdown

##### 11.1 Crypto Primitives *(superseded)*
| # | Task | File(s) | Acceptance Criteria | Est. |
|---|------|---------|-------------------|------|
| 11.1.1 | SHA-256 implementation | `src/crypto/sha256.c` | NIST FIPS 180-4 short/long/Monte Carlo test vectors pass | 2d |
| 11.1.2 | HMAC-SHA256 implementation | `src/crypto/hmac_sha256.c` | RFC 4231 test vectors pass; constant-time via `secure_compare()` | 1d |
| 11.1.3 | AES-128/256-GCM implementation | `src/crypto/aes_gcm.c` | NIST SP 800-38D test vectors pass (encrypt + decrypt + auth tag) | 3d |
| 11.1.4 | Unit tests + CI gate | `tests/test_crypto.c` | All NIST vectors pass; Valgrind clean; key material zeroed after use | 1d |

##### 11.2 Password Hashing & Key Derivation *(superseded — re-homed to Phase 21 W58)*
| # | Task | File(s) | Acceptance Criteria | Est. |
|---|------|---------|-------------------|------|
| 11.2.1 | PBKDF2-HMAC-SHA256 | `src/crypto/pbkdf2.c` | RFC 6070 test vectors pass; configurable iteration count (default 600,000) | 2d |
| 11.2.2 | HKDF (extract + expand) | `src/crypto/hkdf.c` | RFC 5869 Appendix A test vectors pass | 1d |
| 11.2.3 | `password_hash_create/verify` API | `src/password.c`, `include/kamran.k` | Salt auto-generated; timing-safe verify; hash format includes iteration count | 2d |
| 11.2.4 | Unit tests | `tests/test_weblib.c` | Round-trip hash/verify; wrong password fails; different salts produce different hashes | 1d |

##### 11.3 TLS Transport Encryption *(superseded — delivered as TLS 1.3, see §7 Phase 12)*
| # | Task | Shipped file(s) | What actually happened |
|---|------|-----------------|------------------------|
| 11.3.1 | TLS record layer (framing) | `src/tls/record.c` (planned as `tls_record.c`) | ✅ Delivered as a TLS **1.3** record layer: framing, the 2^14 plaintext limit, and fragmentation |
| 11.3.2 | TLS handshake state machine | `src/tls/handshake.c`, `src/tls/server_handshake.c` (planned as `tls_handshake.c`) | ✅ Delivered as a TLS **1.3** server handshake: ClientHello → ServerHello / HelloRetryRequest → EncryptedExtensions → Certificate → CertificateVerify → Finished. The planned flow listed a `KeyExchange` message, which does not exist in TLS 1.3 |
| 11.3.3 | PEM parser + key loader | `src/tls/pem.c`, `src/tls/der.c`, `src/tls/ed25519_key.c` (planned as `pem_parser.c`) | 🟡 Loads one self-supplied Ed25519 certificate and its PKCS#8 key. No chain loading, and **no `mlock()` on key pages** → Phase 21 W58 |
| 11.3.4 | Server API integration | `src/http_server.c`, `include/kamran.k` | ✅ `http_server_enable_tls()` shipped; it takes PEM buffers plus lengths, not file paths. Key material is scrubbed on destroy |
| 11.3.5 | HTTPS example + tests | `examples/tls_server.c` (planned as `https_server.c`), `tests/test_tls*.c`, `tests/interop_openssl.sh` | 🟡 Delivered, but against different criteria: `openssl s_client -tls1_3` completes a handshake and an HTTPS round-trip (`TlsInteropOpenssl`). `curl --tlsv1.2` cannot pass — TLS 1.2 is rejected by design — and browser page-load is not achievable with Ed25519-only certificates |

##### 11.4 Request ID & IP Access Control *(superseded — re-homed to Phase 21 W58)*
| # | Task | File(s) | Acceptance Criteria | Est. |
|---|------|---------|-------------------|------|
| 11.4.1 | Request ID middleware | `src/middleware_request_id.c` | Generates unique ID; sets `X-Request-Id` header; propagates if present | 1d |
| 11.4.2 | IP allowlist/denylist middleware | `src/middleware_ip_access.c` | Configurable lists; CIDR support; per-route or global | 2d |
| 11.4.3 | Unit tests | `tests/test_weblib.c` | Request ID unique across requests; blocked IPs get 403 | 1d |

##### 11.5 Security Audit Tooling *(superseded — partially delivered, remainder in Phase 21 W58)*
| # | Task | File(s) | Acceptance Criteria | Est. |
|---|------|---------|-------------------|------|
| 11.5.1 | HTTP parser fuzz harness | `tests/fuzz/fuzz_http_parser.c` | AFL/libFuzzer compatible; runs 1M iterations without crash | 2d |
| 11.5.2 | ASan/MSan CI integration | `.github/workflows/ci.yml` | Sanitizer builds run on every push; zero errors | 1d |
| 11.5.3 | Per-route body size limits | `src/http_server.c`, `include/kamran.k` | `router_set_max_body(route, bytes)` API; reject before buffering | 1d |

#### Phase 11 (superseded) — Success Criteria

| Module | Validation | Met? |
|--------|-----------|------|
| SHA-256 | NIST FIPS 180-4 test vectors pass (short msg, long msg, Monte Carlo) | 🟡 Short-message vectors + streaming equivalence only (`tests/test_weblib.c`, `test_sha256_kat`); no long-message and no Monte Carlo vectors were run |
| HMAC-SHA256 | RFC 4231 test vectors pass | ✅ |
| AES-GCM | NIST SP 800-38D test vectors pass (128+256-bit keys) | ❌ not built → Phase 21 W53 |
| PBKDF2 | RFC 6070 test vectors pass; 600K iterations in < 1s | ❌ not built → Phase 21 W58 |
| HKDF | RFC 5869 Appendix A test vectors pass | ✅ |
| Password hashing | Round-trip create/verify; wrong password → false; salts differ | ❌ not built → Phase 21 W58 |
| ~~TLS 1.2~~ → **TLS 1.3 (EXPERIMENTAL · UNAUDITED)** | `openssl s_client -tls1_3` completes a handshake and an HTTPS round-trip (`TlsInteropOpenssl`); a >16 KiB response is fragmented across records; two requests share one connection. TLS 1.2 is rejected by design. **Browser page-load is explicitly not a gate** — Ed25519-only server certificates have limited and inconsistent browser support | ✅ against the corrected criterion |
| TLS key safety | Private key in `mlock()`'d pages; `secure_zero()` on destroy; never logged | 🟡 `secure_zero()` and no-logging hold; **`mlock()` is not implemented** → Phase 21 W58 |
| Request ID | Unique per request; `X-Request-Id` in response; round-trip propagation | ❌ not built → Phase 21 W58 |
| IP access control | Allowlist passes; denylist blocks (403); CIDR ranges work | ❌ not built → Phase 21 W58 |
| Fuzz testing | Zero crashes; zero ASan/UBSan errors | 🟡 Met for the TLS untrusted-input path (`TlsFuzzTests` + the ASan/UBSan build in the `tls-check` CI job). An HTTP-parser fuzz harness is still open → Phase 21 W58 |

---

## 8. Parallel Build Strategy

Tasks with no data dependencies can be developed simultaneously:

```
GROUPS A–C (W1–W12, Phases 11–12) — DONE, delivered in v2.0.0.
Kept for the record; the actual module split differed from the plan:
├── SHA-256                   ← src/crypto/sha256.c   (SHA-384 never built)
├── SHA-512                   ← src/tls/sha512.c      (added for Ed25519)
├── AES-256-GCM               ← NOT BUILT             (→ Phase 21 W53)
├── ChaCha20-Poly1305         ← src/tls/chacha20.c, poly1305.c, chacha20poly1305.c
├── X25519 + Ed25519          ← src/tls/x25519.c, ed25519.c on src/tls/field25519.c
├── HKDF + key schedule       ← src/tls/hkdf.c, key_schedule.c
├── Record layer              ← src/tls/record.c
├── Server handshake          ← src/tls/handshake.c, server_handshake.c, handshake_auth.c, wire.c
├── Sans-IO engine + adapter  ← src/tls/tls_khannection.c, tls_transport.c
└── Cert/key parsing          ← src/tls/der.c, pem.c, ed25519_key.c (parsing only, no chain validation)

PARALLEL GROUP C' (W13–W17, Phase 13+14) — the next things that can start in parallel:
├── 13.1 HTTP/2 Framing       ← src/http2/frame.c (standalone, no TLS dependency)
├── 13.2 HPACK Compression    ← src/http2/hpack.c (standalone)
└── 14.1 B-tree Core          ← src/storage/btree.c (standalone)
    Note: 13.4 HTTP/2 over TLS needs ALPN to negotiate `h2`, which the shipped
    handshake does not yet offer — it advertises `http/1.1` only.

PARALLEL GROUP D (W18–W22, Phase 14+15):
├── 14.2 Write-Ahead Log      ← depends on 14.1 (B-tree)
├── 15.1 Directory Listing    ← src/middleware_dirlist.c (standalone)
├── 15.2 Server-Sent Events   ← src/sse.c (standalone)
└── 15.5 Route Groups         ← src/router.c (independent feature)

PARALLEL GROUP E (W27–W35, Phase 16+17):
├── 16.1 Master-Worker Model  ← src/worker.c (standalone)
├── 17.1 Platform Abstraction ← src/platform.h (standalone)
└── 18.1 INI Parser           ← src/config.c (standalone)

PARALLEL GROUP F (W36–W40, Phase 18):
├── 18.2 Plugin Architecture  ← src/plugin.c (standalone)
├── 18.3 Advanced Templates   ← src/template_v2.c (standalone)
└── 18.4 Debug Mode           ← src/http_server.c (non-overlapping)

PARALLEL GROUP G (W41–W46, Phase 19):
├── 19.1 UDP Socket Layer     ← src/quic/udp.c (standalone)
└── 20.1 CLI Tool             ← tools/weblib-cli.c (standalone)

SEQUENTIAL (W42–W46, Phase 19):
└── 19.2–19.6 QUIC/HTTP3      ← each depends on previous
```

---

## 9. Build Validation — Success Criteria per Phase

### Phase 11 Checkpoints (Crypto Primitives) — DELIVERED in v2.0.0

| Module | Unit Test Gate | Security Gate | Result |
|--------|---------------|--------------|--------|
| SHA-256 / SHA-512 | RFC / FIPS 180-4 known-answer vectors | No timing side-channels in the compression function | 🟡 SHA-512 vectors in `TlsCryptoTests`; SHA-256 has no KAT there — its short-message KAT lives in `WebLibTests` (`test_sha256_kat`), with no long-message or Monte Carlo vector |
| AES-256-GCM | NIST SP 800-38D test cases 1–18 | Constant-time tag verification | ❌ Not built → Phase 21 W53 |
| ChaCha20-Poly1305 | RFC 8439 §A test vectors | No secret-dependent branches | ✅ `TlsCryptoTests` |
| X25519 / Ed25519 | RFC 7748 §5.2 scalar-mult and §6.1 Alice/Bob key-exchange vectors; RFC 8032 vectors | Constant-time ladder and field arithmetic | ✅ `TlsCryptoTests` |
| HKDF | RFC 5869 vectors; RFC 8448 anchors for `early_secret`/`derived`, rest cross-checked against an independent HKDF reference | Key material zeroed after use | ✅ `TlsCryptoTests` |

> Throughput gates from the original plan (≥ 200/300 MB/s etc.) were never measured and are not
> claimed. Correctness and constant-time construction were the gates that actually ran.
> **None of this has been externally audited** — that gate is Phase 21 W51.

### Phase 12 Checkpoints (TLS 1.3) — DELIVERED in v2.0.0

| Module | Validation | Result |
|--------|-----------|--------|
| TLS Record Layer | Encrypt/decrypt round-trip; reject oversized records; handle partial TCP reads; fragment large writes | ✅ `TlsTests`, `TlsTransportTests` |
| TLS Handshake | Complete TLS 1.3 handshake, including the HelloRetryRequest path | ✅ `openssl s_client -tls1_3` in `TlsInteropOpenssl`. ❌ Browser green lock is **not** a gate and is not achieved — Ed25519-only server certificates have limited and inconsistent browser support |
| Certificate Handling | Parse a self-supplied Ed25519 certificate + PKCS#8 key; reject malformed DER/PEM | 🟡 The PKCS#8 **key** is DER-parsed and malformed input rejected; the **certificate** is only PEM-decoded to DER and sent opaquely, never parsed as X.509. Chain building, signature verification and expiry checks are **not** implemented → Phase 21 W52 |
| HTTPS Integration | `https://localhost:8443/` returns 200; ALPN negotiates a protocol | ✅ via `TlsHttpTests` and `TlsInteropOpenssl`, including a >16 KiB fragmented response and two requests on one connection. ALPN negotiates `http/1.1`; `h2` waits on Phase 13 |
| Build gating | TLS compiles only with `-DWEBLIB_ENABLE_TLS=ON`; with it OFF the build is byte-identical to a pre-TLS build | ✅ CMake option defaults OFF; `tls-check` CI job covers the ON path |

### Phase 13 Checkpoints (HTTP/2)

| Module | Validation |
|--------|-----------|
| Binary Framing | Parse/serialize all 10 frame types; reject unknown flags with PROTOCOL_ERROR |
| HPACK | Static table lookup; Huffman round-trip; decode h2spec test cases |
| Stream Multiplexing | 100 concurrent streams; priority-weighted DATA frame scheduling |
| Integration | `curl --http2 https://localhost:8443/` returns 200; server push delivered |

### Phase 14 Checkpoints (Storage)

| Module | Validation |
|--------|-----------|
| B-tree | Insert 100K keys; all retrieved correctly; balanced tree depth ≤ log(N) |
| WAL | Kill process mid-write; restart; no data corruption; WAL replay recovers |
| Transactions | Concurrent read + write; read sees consistent snapshot; rollback discards writes |
| Iterator | Forward/reverse scan matches sorted order; range query returns correct subset |

### Phase 15 Checkpoints (Middleware)

| Module | Validation |
|--------|-----------|
| Directory Listing | Navigate directories via browser; sorted columns; correct file sizes |
| SSE | `EventSource` client receives events; reconnects with Last-Event-ID |
| Route Groups | Scoped middleware applies only within group; regex routes match patterns |
| Streaming | 100MB chunked upload completes; chunked response streams to client |

### Phase 16 Checkpoints (Multi-Process)

| Module | Validation |
|--------|-----------|
| Master-Worker | `http_server_set_workers(4)` spawns 4 child processes; all accept connections |
| Zero-Downtime Reload | Send SIGHUP; new workers start; old workers drain; zero dropped requests |
| Metrics Aggregation | `/metrics` shows combined counts from all workers |

### Phase 17 Checkpoints (Cross-Platform)

| Module | Validation |
|--------|-----------|
| Platform Abstraction | Same application code compiles on Linux + macOS + Windows + FreeBSD |
| Windows IOCP | Server accepts 100 concurrent connections on Windows; async I/O works |
| CI Matrix | Green builds on all 4 platforms in GitHub Actions |

### Phase 18 Checkpoints (Developer Experience)

| Module | Validation |
|--------|-----------|
| INI Parser | Load config file; `config_get("server", "port")` returns correct value |
| Plugin Architecture | Register plugin; lifecycle hooks fire in order; plugin adds custom route |
| Advanced Templates | Mustache sections render correctly; includes resolve; auto-escaping prevents XSS |
| Debug Mode | Request/response headers logged; per-middleware timing displayed |

### Phase 19 Checkpoints (HTTP/3 & QUIC)

| Module | Validation |
|--------|-----------|
| UDP Layer | Send/receive UDP datagrams; event loop triggers on UDP readability |
| QUIC Transport | Complete QUIC handshake; reliable data transfer over unreliable UDP |
| HTTP/3 | `curl --http3 https://localhost:8443/` returns 200 (requires curl 7.66+) |
| Connection Migration | Client changes IP; connection continues without re-handshake |

### Phase 20 Checkpoints (Release)

| Module | Validation |
|--------|-----------|
| CLI Tool | `weblib init myapp` generates buildable project; `weblib routes` lists routes |
| Prometheus Export | `/metrics` returns valid Prometheus exposition format; Grafana imports |
| Fuzz Testing | 1M iterations per harness; zero crashes; zero memory errors |
| v3.0.0 Release | All ctest suites green in both the default and `WEBLIB_ENABLE_TLS=ON` configurations; zero warnings; CHANGELOG complete; migration guide accurate |

---

## 10. QA Pipeline

### Automated Testing — what runs today (v2.0.0)

`.github/workflows/ci.yml` currently has five jobs:

| Job | What it does |
|-----|--------------|
| `primary-checks` | GCC build inside the dev Docker image, the full `ctest` run, and a Valgrind memory check |
| `clang-check` | Clang build and full `ctest` run |
| `tls-check` | RelWithDebInfo build with `-DWEBLIB_ENABLE_TLS=ON -DWEBLIB_TLS_TEST_HOOKS=ON` running all 14 suites, then a separate ASan/UBSan build running the 7 TLS suites |
| `macos-check` | macOS build and test — pull requests only |
| `docker-image-check` | Builds the production Docker image |

Build and test the TLS configuration locally with:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo -DWEBLIB_ENABLE_TLS=ON
cmake --build build --parallel
cd build && ctest --output-on-failure
```

That gives you 12 suites. The thirteenth, `TlsHttpTests`, needs the deterministic-RNG
test seam, so add `-DWEBLIB_TLS_TEST_HOOKS=ON` to the configure line to match the
`tls-check` job exactly. Never enable that option in a build you intend to deploy —
a production server with a pinned RNG would have predictable handshake randomness.

### Automated Testing — target pipeline (aspirational, Phases 13–20)

The block below is the pipeline this roadmap is building toward. Most of it does not exist yet:
there is no Windows or FreeBSD runner, no `test_crypto` / `test_http2` / `test_storage` / `test_quic`
/ `test_http3` binary, no cppcheck stage, and no benchmark regression gate.

```
┌─────────────────────────────────────────────────────────────┐
│  GitHub Actions CI Pipeline (target)                         │
│                                                              │
│  Stage 1: Build Matrix                                       │
│  ├── Linux (GCC 12 + Clang 15)                               │
│  ├── macOS (Apple Clang 15)                                  │
│  ├── Windows (MSVC 2022)                                     │
│  └── FreeBSD (Clang 15, cross-compilation)                   │
│     └── cmake -DCMAKE_C_FLAGS="-Wall -Wextra -Werror" ..    │
│                                                              │
│  Stage 2: Unit Tests                                         │
│  ├── ./tests/test_weblib          (core library tests)       │
│  ├── ./tests/test_crypto          (crypto test vectors)      │
│  ├── ./tests/test_tls             (TLS handshake tests)      │
│  ├── ./tests/test_http2           (HTTP/2 protocol tests)    │
│  ├── ./tests/test_storage         (B-tree + WAL tests)       │
│  ├── ./tests/test_quic            (QUIC transport tests)     │
│  └── ./tests/test_http3           (HTTP/3 framing tests)     │
│                                                              │
│  Stage 3: Integration Tests                                  │
│  ├── TLS handshake with curl                                 │
│  ├── HTTP/2 multiplexed requests                             │
│  ├── Multi-worker concurrent load                            │
│  └── Storage crash recovery (kill + restart)                 │
│                                                              │
│  Stage 4: Memory Safety (Linux only)                         │
│  └── valgrind --leak-check=full --error-exitcode=1           │
│      (all test binaries)                                     │
│                                                              │
│  Stage 5: Fuzz Testing (Phase 20+)                           │
│  └── 60-second fuzz runs per harness (HTTP, JSON, TLS,       │
│      HPACK, QUIC); zero crashes = pass                       │
│                                                              │
│  Stage 6: Benchmark Regression                               │
│  └── Compare req/s and latency against baseline ±10%         │
│                                                              │
│  Stage 7: Static Analysis                                    │
│  └── cppcheck --enable=all --error-exitcode=1                │
└─────────────────────────────────────────────────────────────┘
```

### Manual Testing Checkpoints (Per Release)

| # | Test | Method | Pass Criteria |
|---|------|--------|--------------|
| M1 | TLS browser compatibility | Chrome + Firefox → `https://localhost:8443` | Green lock; TLS 1.3 in certificate info. **NOT MET, and not reachable with the v2.0.0 profile** — Ed25519-only server certificates have limited and inconsistent browser support. Blocked on RSA/ECDSA certificates (Phase 21 W52). What *is* verified today is `openssl s_client -tls1_3` interop |
| M2 | HTTP/2 multiplexing | `h2load -n 10000 -c 100 https://localhost:8443/` | All 10K requests succeed; streams multiplexed |
| M3 | HTTP/3 connectivity | `curl --http3 https://localhost:8443/` | Response received over QUIC |
| M4 | Storage durability | Insert 10K records; `kill -9` server; restart; verify all records | Zero data loss |
| M5 | Multi-worker load test | 4 workers; `wrk -t4 -c400 -d30s` | Linear throughput scaling vs single worker |
| M6 | Zero-downtime reload | Continuous `wrk` load + send SIGHUP | Zero failed requests during reload |
| M7 | Windows build | Build on Windows with MSVC 2022 | Zero errors, zero warnings, tests pass |
| M8 | Memory under sustained load | Run 10M requests; monitor RSS per worker | RSS stable (no unbounded growth) |
| M9 | CLI scaffolding | `weblib init myapp && cd myapp && mkdir build && cd build && cmake .. && make && ./myapp` | Server starts and responds to curl |
| M10 | Fuzz testing marathon | 24-hour fuzz run on all harnesses | Zero crashes; zero memory errors |

---

## 11. Security Review — Threat Model (v2.0.0)

### Assets Under Protection

| Asset | Location | Sensitivity | New in v2 |
|-------|----------|-------------|-----------|
| HTTP request data | `http_request_t` in memory | Contains credentials (cookies, auth headers) | — |
| Session store | `session_store_t` / storage engine | Session IDs = authentication tokens | Persistent backend |
| TLS private keys | Loaded from PEM at startup | Compromise = full traffic decryption | **New** |
| TLS session keys | In-memory per connection | Compromise = decrypt single session | **New** |
| Storage engine files | On-disk B-tree + WAL | User data at rest; potential PII | **New** |
| QUIC connection state | In-memory per connection | Connection IDs, crypto state | **New** |
| Worker process memory | Per-process address space | Isolated but shares listening socket | **New** |
| Configuration files | On-disk INI files | May contain secrets (API keys, DB paths) | **New** |

### Threat Model (STRIDE) — v2.0.0 Additions

| Threat | Category | Target | Mitigation |
|--------|----------|--------|-----------|
| **TLS implementation bugs** | Tampering / Disclosure | TLS handshake | ✅ *in place (v2.0.0)*: RFC known-answer vectors per primitive; constant-time-by-construction primitives; deterministic fuzzer over the untrusted-input path; ASan/UBSan CI build; TLS off by default. ❌ *still open*: **external security audit** (Phase 21 W51) — until then the layer is UNAUDITED and the mitigation is incomplete |
| **TLS downgrade to 1.2** | Tampering | TLS negotiation | ✅ *in place (v2.0.0)*: only TLS 1.3 is accepted; a client offering nothing higher gets a `protocol_version` alert. This is enforced through `supported_versions`, not the legacy record version |
| **TLS private key extraction** | Disclosure | Key material in memory | 🟡 *partial (v2.0.0)*: key material is scrubbed with `secure_zero()` and never logged. ❌ *still open*: `mlock()` on key pages (Phase 21 W58) |
| **HTTP/2 stream flood** | Denial of Service | Stream table | MAX_CONCURRENT_STREAMS (default 100); RST_STREAM rate limiting (Phase 13) |
| **HPACK bomb** | Denial of Service | Memory | Dynamic table hard cap (4KB default); reject oversized header blocks (Phase 13) |
| **HTTP/2 slow read** | Denial of Service | Flow control | Connection-level flow control timeout; close slow-consuming streams (Phase 13) |
| **Storage corruption** | Tampering | B-tree files | CRC32 per page; WAL replay validation; fsync on commit (Phase 14) |
| **Storage path traversal** | Disclosure | File system | Canonicalize storage path; reject `..` sequences; sandbox to data directory (Phase 14) |
| **Fork bomb via worker API** | Denial of Service | Process table | Hard cap on `http_server_set_workers()` (max 64); rate-limit respawns (Phase 16) |
| **QUIC amplification** | Denial of Service | Network | Retry token validation; 3x amplification limit before address validation (Phase 19) |
| **QUIC connection ID spoofing** | Spoofing | Connection table | Server-generated connection IDs with HMAC; reject unknown IDs (Phase 19) |
| **Config file injection** | Tampering | Configuration | Max value length; no shell expansion; no template evaluation in config values (Phase 18) |
| **Plugin code execution** | Elevation of Privilege | Server process | Compile-time only (no dlopen); no runtime plugin loading; code review required (Phase 18) |

### Vulnerability Checklist (Per-Phase Gate)

Each phase release MUST pass all v1.0.0 checks plus:

- [ ] No compiler warnings with `-Wall -Wextra -Werror -pedantic` on all 4 platforms
- [ ] Valgrind memcheck: zero errors, zero leaks (definite + indirect)
- [ ] All crypto operations use constant-time comparison (`crypto_memcmp()`)
- [ ] All crypto key material zeroed with `explicit_bzero()` / `SecureZeroMemory()` before free
- [ ] TLS key pages locked with `mlock()` (POSIX) / `VirtualLock()` (Windows)
- [ ] No secret-dependent branches in crypto code (verify with timing analysis)
- [ ] Storage engine validates CRC32 on every page read
- [ ] WAL replay rejects records with CRC mismatch
- [ ] Multi-process worker count hard-limited to MAX_WORKERS (64)
- [ ] QUIC retry tokens are HMAC-validated and time-limited
- [ ] Configuration values are length-bounded (MAX_CONFIG_VALUE_LEN = 4096)
- [ ] No `dlopen()` or runtime code loading — plugins are compile-time only
- [ ] Fuzz testing produces zero crashes after 1M iterations per harness
- [ ] All `malloc()` return values checked; all `snprintf()` bounded
- [ ] No information leakage in error responses (status code only)

### Access Control Matrix (v2.0.0)

| Component | Read Access | Write Access | Notes |
|-----------|------------|-------------|-------|
| TLS private key | TLS handshake module only | Loaded once, before `http_server_listen()` | Zeroed on server destroy and never logged. `mlock()` is **planned, not implemented** (Phase 21 W58) |
| TLS session keys | TLS record layer only | TLS handshake only | Per-connection; zeroed on close |
| Storage B-tree | Storage API only | Transaction commit only | Page-level CRC validation |
| Storage WAL | Recovery module only | Transaction module only | Append-only; fsync on commit |
| Worker process table | Master process only | Master process only | PID tracking; signal-based control |
| QUIC connection IDs | QUIC transport only | QUIC handshake only | HMAC-validated; server-generated |
| Configuration data | `config_get()` API only | `config_load()` only (startup) | Immutable after load |

---

## 12. Risk Mitigation Notes

| Risk | Probability | Impact | Mitigation Strategy |
|------|------------|--------|-------------------|
| Pure C TLS 1.3 has security bugs | HIGH | CRITICAL | Comprehensive test vectors (NIST + RFC); constant-time-by-construction primitive choice; fuzz testing; TLS is **opt-in** behind the `WEBLIB_ENABLE_TLS` CMake option (default **OFF** — with it off no `src/tls` code is compiled and the build is byte-identical to a pre-TLS build); the shipped implementation is **EXPERIMENTAL and UNAUDITED**, not for production use without an external cryptographic audit (Phase 21 W51); recommend terminating TLS at a reverse proxy (nginx) for any deployment you care about |
| `WEBLIB_TLS_TEST_HOOKS` reaches production | LOW | CRITICAL | The option is default OFF and exists only to give `TlsHttpTests` a deterministic RNG seam. It must never be enabled in a shipped build — a production server built with it would have predictable handshake randomness |
| X25519 field arithmetic bugs | MEDIUM | CRITICAL | RFC 7748 test vectors; comparison against known-good implementation output; timing analysis for side channels. *Status (v2.0.0): vectors pass and `src/tls/field25519.c` is written to be branch-free, but no external timing analysis has been performed* |
| HTTP/2 complexity causes regressions | MEDIUM | HIGH | Feature-flagged (`http_server_enable_http2()`); disabled by default; h2spec conformance testing |
| QUIC implementation incomplete or buggy | HIGH | HIGH | Feature-flagged; disabled by default; extensive quic-interop-runner testing; clearly marked as experimental in v2.0 |
| B-tree data corruption | LOW | CRITICAL | CRC32 checksums; WAL replay; fsync discipline; crash recovery test (kill -9 + restart) |
| Multi-process deadlock | LOW | HIGH | Shared-nothing design (no shared memory by default); each worker is independent; master only signals |
| Windows IOCP divergence | MEDIUM | MEDIUM | Platform abstraction layer; CI validates Windows build; Windows-specific integration tests |
| Performance regression vs v1.0.0 | MEDIUM | MEDIUM | Benchmark CI gate (±10%); per-phase benchmark comparison; optimize hot paths. *Status: the benchmark suite exists; the CI gate does not yet* |
| API breaking changes v2→v3 | MEDIUM | MEDIUM | Migration guide; deprecated APIs retained with `WEBLIB_DEPRECATED` macro; compile-time warnings |
| Scope creep delays the remaining phases | MEDIUM | MEDIUM | Strict phase gating; each phase ships independently. *This risk already materialised once: Phases 11 and 12 were planned as two releases (v1.1.0, v1.2.0) and landed together in v2.0.0, with AES-GCM, RSA/ECDSA certificates and browser interop cut from scope to get anything shipped. The cut scope is Phase 21, not a silent deletion* |

---

## 13. Estimated Timeline Summary

**Version targets were re-baselined on 2026-07-27.** The original plan spent v1.1.0 through v2.0.0
on Phases 11–20. What actually happened is that Phases 11 and 12 landed together in **v2.0.0**, so
every later phase needed a new target. Phases 13–20 keep their numbers and their week ranges; only
the version column moved.

| Phase | Original target | Current target | Duration | Cumulative | Key Deliverable |
|-------|-----------------|----------------|----------|-----------|-----------------|
| Phase 11 | v1.1.0 | **✅ shipped in v2.0.0** | 6 weeks | W1–W6 | Crypto primitives — SHA-256/512, HMAC, HKDF, ChaCha20-Poly1305, X25519, Ed25519 (**no AES-GCM**) |
| Phase 12 | v1.2.0 | **✅ shipped in v2.0.0** | 6 weeks | W7–W12 | TLS 1.3 **server** handshake + HTTPS — EXPERIMENTAL · UNAUDITED |
| Phase 13 | v1.3.0 | v2.1.0 | 5 weeks | W13–W17 | HTTP/2 protocol (framing, HPACK, streams, push) |
| Phase 14 | v1.4.0 | v2.2.0 | 5 weeks | W18–W22 | Persistent storage engine (B-tree, WAL, transactions) |
| Phase 15 | v1.5.0 | v2.3.0 | 4 weeks | W23–W26 | Advanced middleware (directory listing, SSE, route groups) |
| Phase 16 | v1.6.0 | v2.4.0 | 5 weeks | W27–W31 | Multi-process architecture (fork, reload, metrics) |
| Phase 17 | v1.7.0 | v2.5.0 | 4 weeks | W32–W35 | Cross-platform (Windows IOCP, BSD, MSVC, CI matrix) |
| Phase 18 | v1.8.0 | v2.6.0 | 5 weeks | W36–W40 | Developer experience (config, plugins, templates, debug) |
| Phase 19 | v1.9.0 | v2.7.0 | 6 weeks | W41–W46 | HTTP/3 & QUIC (UDP, transport, handshake, streams) |
| Phase 20 | v2.0.0 | v3.0.0 | 4 weeks | W47–W50 | Release engineering (CLI, observability, fuzz, release) |
| Phase 21 | — (new) | unscheduled | 8 weeks | W51–W58 | Security residuals & TLS hardening — external audit, RSA/ECDSA certs, AES-GCM, async/WS TLS, resumption, client mode, PBKDF2, request-ID and IP-access middleware |
| **Total** | | | **58 weeks** | | **W1–W12 delivered in v2.0.0 (2026-07-27); W13–W58 (~11 months) remain, re-baselined from that date** |

> Phase 21 is written last but is not lowest priority. The external audit (W51) is the only thing
> that can remove the word **UNAUDITED** from the TLS layer, and until it happens the honest advice
> for production traffic is to terminate TLS at a reverse proxy.

---

## Appendix A: v1.0.0 Phase Reference (Phases 1–10)

| Phase | Version | Date | Highlights |
|-------|---------|------|------------|
| Phase 1 | v0.1.0 | 2024-12 | HTTP server, event loop, routing, middleware, JSON, CMake |
| Phase 2 | v0.2.0 | 2025-01 | WebSocket RFC 6455 (handshake, framing, control frames) |
| Phase 3 | v0.3.0 | 2025-11 | WebSocket threaded mode, persistent connections, ping/pong |
| Phase 4 | v0.4.0 | 2026-02 | HTTP parser hardening, headers, JSON arrays, connections |
| Phase 5 | v0.5.0 | 2026-02 | Body parsing, cookies, CORS, rate limiting, static files |
| Phase 6 | v0.6.0 | 2026-02 | Sessions, templates, auth (Basic/JWT/API-Key), DB pooling |
| Phase 7 | v0.7.0 | 2026-02 | Socket timeouts, thread pool, graceful shutdown, CI |
| Phase 8 | v0.8.0 | 2026-02 | CSRF, logging, error handler, input validation, health check |
| Phase 9 | v0.9.0 | 2026-02 | Compression, caching, metrics, async WebSocket, benchmarking |
| Phase 10 | v1.0.0 | 2026-02 | REST API example, tutorials, documentation, release |

---

## Revision History

| Version | Date | Changes |
|---------|------|---------|
| 1.0 | 2025-01-12 | Initial roadmap (Phases 4–6) |
| 2.0 | 2026-02-19 | Complete rewrite for Phases 7–10: first-principles design, adversarial review, atomic task breakdown, security threat model |
| 3.0 | 2026-03-02 | Phase 10.1 delivered (security utilities, headers middleware, secure secrets); Phase 11 planned (TLS, password hashing, HKDF, fuzz testing) |
| 4.0 | 2026-07-27 | Roadmap re-baselined against shipped work for the **v2.0.0** release. WebAssembly/Emscripten and Cloudflare Workers (KV, R2, D1, Queues) targets shipped Apr 2026 — supersedes the "WebAssembly compilation target" non-goal in §2. Phases 11 and 12 delivered together in v2.0.0 as an experimental pure-C TLS 1.3 **server**: EXPERIMENTAL · UNAUDITED, native-only, threaded-mode only, OFF by default behind `WEBLIB_ENABLE_TLS`; `TLS_CHACHA20_POLY1305_SHA256` / X25519 / Ed25519 only; `http_server_enable_tls()`; `openssl s_client` interop, deterministic fuzzer, `tls-check` CI job with ASan/UBSan. Narrower than planned: no AES-GCM, no SHA-384, no RSA/ECDSA certificate verification, no client side, no session resumption, no SNI; manual checkpoint M1 (browser page-load) **not met**. That residual scope is carried forward as the new **Phase 21**, and the version targets for Phases 13–20 were re-baselined onto v2.1.0–v3.0.0. Note "4.0" is this *document's* revision number, not a project version |

---

**Maintained by**: MCWL Core Team
**Last Updated**: 2026-07-27
**Status**: Phases 11–12 delivered in **v2.0.0** — the hand-written pure-C **TLS 1.3 server**
(server-side only; `TLS_CHACHA20_POLY1305_SHA256` + X25519 + Ed25519) is **EXPERIMENTAL · UNAUDITED**,
opt-in via `-DWEBLIB_ENABLE_TLS=ON`, threaded-mode and native-only — see
[`src/tls/README.md`](src/tls/README.md). Not for production without an external cryptographic audit.
Still open from those phases (now **Phase 21**): external audit, RSA/ECDSA certificates and browser
interop, AES-GCM, TLS in async mode, WebSocket over TLS, session resumption, SNI, client mode,
PBKDF2 password hashing, request-ID and IP-access middleware. Next protocol phase: HTTP/2 (Phase 13, v2.1.0).
**License**: MIT (see LICENSE file)

For questions or discussions about this roadmap, please open an issue on GitHub or contact the maintainers.
