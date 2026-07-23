# Next Phase Roadmap — Modern C Web Library v2.0.0

> **Methodology**: First-principles engineering workflow.
> Every decision below traces back to a verifiable technical constraint, not convention.

---

## Part I: v1.0.0 Completed Phases (Reference)

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

**v1.0.0 baseline**: 129/129 unit tests · zero compiler warnings · 25 source modules · 5 example servers

---

## Part II: v2.0.0 Roadmap — Phases 11–20

---

## 1. Idea Intake

**Core problem in one sentence**: The library has achieved production-grade HTTP/1.1 functionality but lacks transport encryption (TLS), modern protocol support (HTTP/2, HTTP/3), persistent storage, multi-process scalability, cross-platform parity, and the developer tooling necessary to compete with frameworks like nginx and libuv as a self-contained, zero-dependency C web platform.

---

## 2. Crystallized Brief

| Dimension | Detail |
|-----------|--------|
| **Target users** | C developers building production HTTP/HTTPS backends, microservices, and real-time systems who require zero external dependencies and full source-level control |
| **Desired outcomes** | A library that encrypts traffic (TLS 1.3), speaks HTTP/2 and HTTP/3, persists data without external databases, scales across CPU cores via multi-process architecture, runs identically on Linux/macOS/Windows/BSD, and provides developer tooling for rapid iteration |
| **Non-goals** | Full ORM abstraction; language bindings (Python/Go/Rust wrappers); GUI tools; package manager integration (apt/brew/vcpkg); WebAssembly compilation target |

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
| Phase 10.1 | v1.0.1 | ✅ Complete | Security utilities, security headers middleware, secure secret handling |

**Current state**: 146/146 unit tests passing · zero compiler warnings · 28 source modules · 5 example servers

1. **Transport encryption is non-negotiable** — Every production deployment requires HTTPS. Without TLS, browsers refuse connections, load balancers reject backends, and credentials travel in plaintext. Pure C TLS (not OpenSSL) is the single highest-impact v2 feature.

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
| AES-256-GCM + ChaCha20-Poly1305 ciphers | Two cipher suites cover all platforms; AES-GCM for hardware-accelerated systems, ChaCha20 for ARM/embedded |
| HTTP/2 via ALPN negotiation over TLS | HTTP/2 cleartext (h2c) rarely used in practice; TLS-based negotiation is the standard path |
| HPACK with static table only (initially) | Dynamic table adds complexity and memory-based attacks (HPACK bomb); static table covers 90% of headers |
| Embedded B-tree key-value store | Simplest data structure that supports ordered iteration, range queries, and O(log n) access; avoids LSM complexity |
| Multi-process via `fork()` + shared-nothing | No shared memory races; each worker is independent; master process handles signals and restarts; proven model (nginx, Redis) |
| Windows IOCP via abstraction layer | `src/platform.h` abstracts epoll/kqueue/IOCP behind common interface; compile-time selection |
| Configuration via C struct + optional INI parser | No YAML/JSON config dependency; INI is trivially parseable in C; C struct provides type safety |

---

## 4. Adversarial Review

| Attack Vector / Failure Mode | Current Exposure (v1.0.0) | Mitigation (Phase) |
|------------------------------|--------------------------|---------------------|
| **Plaintext credential exposure** | CRITICAL: no TLS at all | Pure C TLS 1.3 (Phase 11) |
| **TLS implementation bugs** | N/A (not yet implemented) | NIST test vectors + known-answer tests; constant-time operations; fuzz testing (Phase 11-12) |
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
Phase 11 (v1.1.0): TLS Foundation — Crypto Primitives
   ├── SHA-256 / SHA-384 (FIPS 180-4)
   ├── AES-256-GCM (NIST SP 800-38D)
   ├── ChaCha20-Poly1305 (RFC 8439)
   ├── X25519 key exchange (RFC 7748)
   ├── HKDF key derivation (RFC 5869)
   ├── Pure C big-integer arithmetic for X25519
   └── NIST/RFC test vector validation for every primitive

Phase 12 (v1.2.0): TLS 1.3 Handshake & HTTPS
   ├── TLS 1.3 record layer (RFC 8446)
   ├── Handshake state machine (ClientHello → ServerHello → Finished)
   ├── Certificate chain validation (X.509 DER parsing)
   ├── PEM file loading (certificate + private key)
   ├── ALPN negotiation (for HTTP/2 in Phase 13)
   ├── `http_server_enable_tls()` API
   └── HTTPS example server with self-signed certificate

Phase 13 (v1.3.0): HTTP/2 Protocol
   ├── Binary framing layer (RFC 7540)
   ├── HPACK header compression (RFC 7541, static table)
   ├── Stream multiplexing with priority
   ├── Flow control (connection-level + stream-level)
   ├── Server push
   ├── `http_server_enable_http2()` API
   └── h2 + h2c (cleartext) support

Phase 14 (v1.4.0): Persistent Storage Engine
   ├── B-tree key-value store (on-disk, memory-mapped)
   ├── Write-ahead log (WAL) for crash recovery
   ├── Transaction support (begin/commit/rollback)
   ├── Page-level CRC32 checksums
   ├── Iterator API for range queries
   ├── `storage_open()` / `storage_close()` lifecycle
   └── Integration with session store + cache persistence

Phase 15 (v1.5.0): Advanced Middleware & Content
   ├── Directory listing (auto-generated HTML indexes)
   ├── Server-Sent Events (SSE) for streaming
   ├── Content negotiation (Accept header parsing)
   ├── Request/response streaming (chunked transfer)
   ├── Route groups with scoped middleware
   ├── Regex-based route matching
   └── ETag generation improvements (weak/strong)

Phase 16 (v1.6.0): Multi-Process Architecture
   ├── Master-worker process model (fork-based)
   ├── Worker supervision and auto-restart
   ├── SO_REUSEPORT per-worker accept
   ├── Signal-based worker management (SIGUSR1/SIGUSR2)
   ├── Zero-downtime reload (hot restart)
   ├── `http_server_set_workers(n)` API
   └── Per-worker metrics aggregation

Phase 17 (v1.7.0): Cross-Platform Hardening
   ├── Platform abstraction layer (`src/platform.h`)
   ├── Windows IOCP event loop backend
   ├── Windows named pipes for IPC
   ├── BSD (FreeBSD/OpenBSD/NetBSD) testing + CI
   ├── MSVC build support (CMake generator)
   ├── CI matrix: Linux (GCC/Clang) + macOS (Clang) + Windows (MSVC) + FreeBSD
   └── Platform compatibility documentation

Phase 18 (v1.8.0): Developer Experience & Configuration
   ├── INI configuration file parser
   ├── Plugin/extension architecture (compile-time modules)
   ├── Multiple template formats (Mustache-style, includes, inheritance)
   ├── Auto-escaping (HTML/URL/JS context-aware)
   ├── Debug mode (verbose logging, request inspection, timing)
   ├── API versioning support (URL prefix + header-based)
   └── Configuration validation and hot-reload

Phase 19 (v1.9.0): HTTP/3 & QUIC
   ├── UDP socket layer
   ├── QUIC transport protocol (RFC 9000)
   ├── QUIC handshake (integrates Phase 11-12 TLS 1.3)
   ├── Connection migration
   ├── HTTP/3 framing (RFC 9114)
   ├── QPACK header compression (RFC 9204)
   └── `http_server_enable_http3()` API

Phase 20 (v2.0.0): Release Engineering & Ecosystem
   ├── CLI tool (project scaffolding, route listing, config validator)
   ├── Prometheus-compatible metrics export endpoint
   ├── OpenTelemetry-compatible trace context propagation
   ├── Comprehensive fuzz testing suite
   ├── Performance regression CI gate
   ├── Complete v2.0.0 documentation + migration guide
   └── Semantic versioning enforcement + release automation
```

---

## 6. Milestone Roadmap (1–2 Week Slices)

### Phase 11: TLS Foundation — Crypto Primitives — v1.1.0 (6 weeks)

| Week | Milestone | Deliverables | Dependencies |
|------|-----------|-------------|--------------|
| **W1** | SHA-256 / SHA-384 | `src/tls/sha256.c` — FIPS 180-4 compliant; NIST test vectors (short, long, Monte Carlo); `sha256()` and `sha384()` APIs | None |
| **W2** | AES-256-GCM | `src/tls/aes_gcm.c` — NIST SP 800-38D compliant; key schedule, GCM encrypt/decrypt with authentication tag; NIST test vectors | None |
| **W3** | ChaCha20-Poly1305 | `src/tls/chacha20_poly1305.c` — RFC 8439 compliant; stream cipher + AEAD construction; IETF test vectors | None |
| **W4** | X25519 Key Exchange | `src/tls/x25519.c` — RFC 7748 compliant; pure C big-integer field arithmetic (mod 2^255-19); scalar multiplication; RFC test vectors | None |
| **W5** | HKDF + Key Derivation | `src/tls/hkdf.c` — RFC 5869 compliant; HKDF-Extract + HKDF-Expand using HMAC-SHA256/SHA384; TLS 1.3 key schedule helper functions | W1 (SHA-256) |
| **W6** | Integration & Hardening | Constant-time comparison for all crypto ops; `explicit_bzero()` on key material; Valgrind clean; 60+ crypto unit tests; `src/tls/crypto.h` unified header | W1–W5 |

### Phase 12: TLS 1.3 Handshake & HTTPS — v1.2.0 (6 weeks)

| Week | Milestone | Deliverables | Dependencies |
|------|-----------|-------------|--------------|
| **W7** | TLS Record Layer | `src/tls/tls_record.c` — record framing (type, version, length); content type encryption; padding; record size limits (16KB) | Phase 11 (AES-GCM / ChaCha20) |
| **W8** | TLS Handshake Part 1 | `src/tls/tls_handshake.c` — ClientHello parsing; ServerHello generation; supported_versions extension; key_share extension (X25519) | W7, Phase 11 (X25519) |
| **W9** | TLS Handshake Part 2 | EncryptedExtensions; server Certificate message; CertificateVerify; Finished message; handshake transcript hash | W8 |
| **W10** | Certificate Handling | `src/tls/x509.c` — DER/PEM parser for X.509 certificates; certificate chain building; RSA/ECDSA signature verification for certificates | Phase 11 (SHA-256) |
| **W11** | HTTPS Server Integration | `http_server_enable_tls(server, cert_path, key_path)` API; ALPN negotiation; `examples/https_server.c`; SNI callback support | W7–W10 |
| **W12** | TLS Testing & Security Audit | Test against `curl --tlsv1.3`; browser compatibility; session resumption (0-RTT optional); 40+ TLS unit tests; Valgrind + timing leak analysis | W7–W11 |

### Phase 13: HTTP/2 Protocol — v1.3.0 (5 weeks)

| Week | Milestone | Deliverables | Dependencies |
|------|-----------|-------------|--------------|
| **W13** | Binary Framing | `src/http2/frame.c` — frame parser/serializer for all 10 frame types (DATA, HEADERS, PRIORITY, RST_STREAM, SETTINGS, PUSH_PROMISE, PING, GOAWAY, WINDOW_UPDATE, CONTINUATION) | None |
| **W14** | HPACK Compression | `src/http2/hpack.c` — static table (61 entries); Huffman coding; integer encoding/decoding; header block encoding/decoding; HPACK test vectors | None |
| **W15** | Stream Multiplexing | `src/http2/stream.c` — stream state machine (idle→open→half-closed→closed); stream priority tree; connection-level + stream-level flow control; MAX_CONCURRENT_STREAMS enforcement | W13 |
| **W16** | HTTP/2 Server Integration | `http_server_enable_http2()` API; connection preface handling; settings negotiation; integration with existing router; server push API; h2c upgrade support | W13–W15, Phase 12 (ALPN) |
| **W17** | HTTP/2 Testing | `curl --http2` validation; multiplexed request tests; flow control tests; HPACK bomb protection; 50+ HTTP/2 unit tests; performance comparison vs HTTP/1.1 | W13–W16 |

### Phase 14: Persistent Storage Engine — v1.4.0 (5 weeks)

| Week | Milestone | Deliverables | Dependencies |
|------|-----------|-------------|--------------|
| **W18** | B-tree Core | `src/storage/btree.c` — on-disk B-tree with configurable page size (4KB default); page allocation; node split/merge; key-value insert/lookup/delete | None |
| **W19** | Write-Ahead Log | `src/storage/wal.c` — append-only WAL; fsync-on-commit; CRC32 checksums per record; WAL replay on crash recovery; WAL truncation after checkpoint | W18 |
| **W20** | Transaction Support | `src/storage/transaction.c` — begin/commit/rollback; MVCC snapshot isolation; read-only transactions without locks; deadlock detection timeout | W18–W19 |
| **W21** | Iterator & Query Interface | `storage_iterator_t` — forward/reverse iteration; range queries (start_key, end_key); prefix scan; cursor-based pagination | W18 |
| **W22** | Integration & Persistence APIs | `storage_open(path)` / `storage_close()` lifecycle; `storage_get()` / `storage_put()` / `storage_delete()`; session store backend; cache persistence backend; 40+ storage tests | W18–W21 |

### Phase 15: Advanced Middleware & Content — v1.5.0 (4 weeks)

| Week | Milestone | Deliverables | Dependencies |
|------|-----------|-------------|--------------|
| **W23** | Directory Listing & SSE | `src/middleware_dirlist.c` — auto-generated HTML directory indexes with sortable columns; `src/sse.c` — Server-Sent Events (SSE) with event ID, retry, multi-line data | None |
| **W24** | Content Negotiation & Streaming | `Accept` / `Accept-Language` / `Accept-Encoding` parsing with quality values; request body streaming (chunked read callback); response streaming (chunked write callback) | None |
| **W25** | Route Groups & Regex Routes | `router_group_create(prefix)` — scoped middleware per group; `router_add_regex_route()` — POSIX `regcomp()`/`regexec()` based pattern matching; named captures | None |
| **W26** | Testing & Integration | 30+ middleware tests; SSE example (`examples/sse_server.c`); directory listing example; streaming upload example; backward-compatible with existing router API | W23–W25 |

### Phase 16: Multi-Process Architecture — v1.6.0 (5 weeks)

| Week | Milestone | Deliverables | Dependencies |
|------|-----------|-------------|--------------|
| **W27** | Master-Worker Model | `src/worker.c` — `fork()`-based worker spawning; master process signal handling (SIGCHLD, SIGTERM, SIGHUP); worker PID tracking; automatic restart on crash | None |
| **W28** | Socket Sharing & Accept | `SO_REUSEPORT` per-worker accept (Linux 3.9+); fallback to master-distributes-fd via `sendmsg()`/`SCM_RIGHTS` on older kernels/macOS; accept mutex for thundering herd prevention | W27 |
| **W29** | Zero-Downtime Reload | SIGHUP triggers: master forks new workers → new workers start accepting → old workers drain and exit; binary upgrade via `execve()` with inherited listening socket | W27–W28 |
| **W30** | Metrics Aggregation | Per-worker metrics collection; master aggregates via shared memory or pipe; `/metrics` endpoint serves combined JSON; worker health monitoring | W27, Phase 9 (metrics) |
| **W31** | Testing & Stabilization | `http_server_set_workers(n)` API; multi-worker stress tests (10 workers, 10K requests); graceful shutdown of all workers; Valgrind on single-worker mode; 25+ worker tests | W27–W30 |

### Phase 17: Cross-Platform Hardening — v1.7.0 (4 weeks)

| Week | Milestone | Deliverables | Dependencies |
|------|-----------|-------------|--------------|
| **W32** | Platform Abstraction Layer | `src/platform.h` + `src/platform_posix.c` + `src/platform_win32.c` — unified API for: socket operations, file I/O, threading, time, random bytes, memory-mapped files | None |
| **W33** | Windows IOCP Backend | `src/event_loop_iocp.c` — IOCP-based event loop; overlapped I/O; completion port per-thread; integration with platform abstraction; Windows socket initialization (`WSAStartup`) | W32 |
| **W34** | BSD & CI Matrix | FreeBSD/OpenBSD CI runners (or cross-compilation); BSD-specific `kqueue` flags; `arc4random_buf()` for random bytes; platform compatibility matrix documentation | W32 |
| **W35** | MSVC Build & Testing | CMake MSVC generator support; `#pragma` warning suppression mapping; Windows-specific test adaptations; end-to-end CI: Linux (GCC/Clang) + macOS (Clang) + Windows (MSVC) + FreeBSD | W32–W34 |

### Phase 18: Developer Experience & Configuration — v1.8.0 (5 weeks)

| Week | Milestone | Deliverables | Dependencies |
|------|-----------|-------------|--------------|
| **W36** | INI Configuration Parser | `src/config.c` — INI file parser with sections, key=value pairs, comments (`#`, `;`); `config_load(path)` / `config_get(section, key)` / `config_get_int()` / `config_get_bool()`; max value length enforcement | None |
| **W37** | Plugin Architecture | `src/plugin.c` — compile-time plugin registration; `plugin_register(name, init_fn, cleanup_fn)` macro; plugin lifecycle hooks (on_server_start, on_request, on_response, on_server_stop); plugin dependency ordering | None |
| **W38** | Advanced Templates | `src/template_v2.c` — Mustache-compatible syntax (`{{#section}}`, `{{/section}}`, `{{>partial}}`); template inheritance (`{{<base}}`); auto-escaping (HTML context by default, `{{{raw}}}` for unescaped); compiled template caching | None |
| **W39** | Debug Mode & API Versioning | `http_server_set_debug(true)` — verbose request/response logging with headers, timing per middleware, memory allocation tracking; `router_version_group("v1", router_v1)` — URL-prefix and `Accept-Version` header-based API versioning | None |
| **W40** | Testing & Documentation | 35+ developer experience tests; configuration example; plugin example; advanced template example; debug mode documentation; API versioning tutorial | W36–W39 |

### Phase 19: HTTP/3 & QUIC — v1.9.0 (6 weeks)

| Week | Milestone | Deliverables | Dependencies |
|------|-----------|-------------|--------------|
| **W41** | UDP Socket Layer | `src/quic/udp.c` — UDP socket creation; `recvmmsg()`/`sendmmsg()` for batch I/O; `SO_REUSEPORT` for multi-worker UDP; GSO/GRO support (Linux); event loop integration for UDP readability | None |
| **W42** | QUIC Transport Core | `src/quic/transport.c` — QUIC packet parsing (Initial, Handshake, 0-RTT, 1-RTT); variable-length integer encoding; connection ID management; packet number encryption/decryption | Phase 11 (AES-GCM, ChaCha20) |
| **W43** | QUIC Handshake | `src/quic/handshake.c` — integrates TLS 1.3 (Phase 12) as QUIC crypto; Initial packet protection; handshake completion; retry tokens for address validation; anti-amplification limits | Phase 12 (TLS 1.3), W42 |
| **W44** | QUIC Streams & Flow Control | `src/quic/stream.c` — bidirectional and unidirectional streams; stream-level and connection-level flow control; MAX_STREAMS enforcement; stream prioritization | W42 |
| **W45** | HTTP/3 Framing & QPACK | `src/http3/frame.c` — HTTP/3 frame types (DATA, HEADERS, CANCEL_PUSH, SETTINGS, PUSH_PROMISE, GOAWAY); `src/http3/qpack.c` — QPACK header compression (static table + dynamic table with encoder/decoder streams) | W44 |
| **W46** | HTTP/3 Server Integration | `http_server_enable_http3()` API; Alt-Svc header for HTTP/3 discovery; connection migration support; `examples/http3_server.c`; 50+ QUIC/HTTP3 unit tests | W41–W45 |

### Phase 20: Release Engineering & Ecosystem — v2.0.0 (4 weeks)

| Week | Milestone | Deliverables | Dependencies |
|------|-----------|-------------|--------------|
| **W47** | CLI Tooling | `tools/weblib-cli.c` — `weblib init` (project scaffolding with CMakeLists.txt + main.c + Dockerfile); `weblib routes` (list registered routes from compiled server); `weblib config validate` (check INI file syntax) | Phase 18 (config) |
| **W48** | Observability Export | `src/prometheus.c` — `/metrics` endpoint in Prometheus exposition format (counters, gauges, histograms); `src/tracing.c` — W3C Trace Context (`traceparent` header) propagation; span creation/completion; trace ID in log output | Phase 9 (metrics) |
| **W49** | Fuzz Testing & Performance CI | `tests/fuzz/` — fuzz harnesses for HTTP parser, JSON parser, TLS handshake, HPACK decoder, QUIC packet parser (using libFuzzer-compatible API); benchmark regression CI gate (±10% tolerance on req/s) | All phases |
| **W50** | v2.0.0 Release | `CHANGELOG.md` finalization; migration guide (v1→v2 breaking changes); complete API reference update; `docs/architecture.md` (system design document); semantic version tag; GitHub Release with binary artifacts | All phases |

---

## 7. Atomic Task Breakdown

### Phase 11 — TLS Foundation: Crypto Primitives

#### 11.1 SHA-256 / SHA-384
| # | Task | File(s) | Acceptance Criteria | Est. |
|---|------|---------|-------------------|------|
| 11.1.1 | Implement SHA-256 core (init, update, final) | `src/tls/sha256.c`, `src/tls/sha256.h` | NIST short message test vectors pass | 4h |
| 11.1.2 | Implement SHA-384 (truncated SHA-512) | `src/tls/sha384.c` | NIST SHA-384 test vectors pass | 3h |
| 11.1.3 | HMAC-SHA256 / HMAC-SHA384 | `src/tls/hmac.c` | RFC 4231 test vectors pass | 2h |
| 11.1.4 | Monte Carlo test (1M iterations) | `tests/test_crypto.c` | Final digest matches NIST expected value | 2h |
| 11.1.5 | Constant-time comparison utility | `src/tls/crypto_util.c` | No early-exit on mismatch; timing-safe | 1h |

#### 11.2 AES-256-GCM
| # | Task | File(s) | Acceptance Criteria | Est. |
|---|------|---------|-------------------|------|
| 11.2.1 | AES key schedule (128/256-bit) | `src/tls/aes.c` | Encrypt single block matches NIST AESAVS vectors | 4h |
| 11.2.2 | GCM mode (GHASH + CTR) | `src/tls/aes_gcm.c` | NIST SP 800-38D test cases 1–18 pass | 6h |
| 11.2.3 | AEAD encrypt/decrypt API | `src/tls/aes_gcm.c` | Authenticated decryption rejects tampered ciphertext | 2h |
| 11.2.4 | GCM tag verification (constant-time) | `src/tls/aes_gcm.c` | No timing side-channel on tag comparison | 1h |

#### 11.3 ChaCha20-Poly1305
| # | Task | File(s) | Acceptance Criteria | Est. |
|---|------|---------|-------------------|------|
| 11.3.1 | ChaCha20 quarter-round + block function | `src/tls/chacha20.c` | RFC 8439 §2.1–2.3 test vectors pass | 4h |
| 11.3.2 | ChaCha20 stream cipher | `src/tls/chacha20.c` | RFC 8439 §2.4 test vector (encryption) | 2h |
| 11.3.3 | Poly1305 one-time authenticator | `src/tls/poly1305.c` | RFC 8439 §2.5 test vectors pass | 4h |
| 11.3.4 | AEAD_CHACHA20_POLY1305 construction | `src/tls/chacha20_poly1305.c` | RFC 8439 §2.8 AEAD test vector passes | 3h |

#### 11.4 X25519 Key Exchange
| # | Task | File(s) | Acceptance Criteria | Est. |
|---|------|---------|-------------------|------|
| 11.4.1 | Field arithmetic (mod 2^255-19) | `src/tls/x25519.c` | Add, sub, mul, square, invert operations; test vectors | 8h |
| 11.4.2 | Montgomery ladder scalar multiplication | `src/tls/x25519.c` | RFC 7748 §5.2 test vectors (Alice/Bob key exchange) | 6h |
| 11.4.3 | Key generation (clamp + multiply) | `src/tls/x25519.c` | Generated shared secret matches RFC expected output | 2h |
| 11.4.4 | Timing-safe implementation audit | `src/tls/x25519.c` | No secret-dependent branches or array indexing | 2h |

#### 11.5 HKDF Key Derivation
| # | Task | File(s) | Acceptance Criteria | Est. |
|---|------|---------|-------------------|------|
| 11.5.1 | HKDF-Extract | `src/tls/hkdf.c` | RFC 5869 test vectors (SHA-256) pass | 2h |
| 11.5.2 | HKDF-Expand | `src/tls/hkdf.c` | RFC 5869 test vectors pass; output length up to 255*HashLen | 2h |
| 11.5.3 | TLS 1.3 key schedule helpers | `src/tls/hkdf.c` | `derive_secret()`, `hkdf_expand_label()` per RFC 8446 §7.1 | 3h |

---

### Phase 12 — TLS 1.3 Handshake & HTTPS

#### 12.1 TLS Record Layer
| # | Task | File(s) | Acceptance Criteria | Est. |
|---|------|---------|-------------------|------|
| 12.1.1 | Record framing (read/write) | `src/tls/tls_record.c` | Parse TLS record header; enforce 16KB max payload | 4h |
| 12.1.2 | Record encryption (AEAD) | `src/tls/tls_record.c` | Encrypt/decrypt with per-record nonce; content type hiding | 4h |
| 12.1.3 | Record layer buffering | `src/tls/tls_record.c` | Handle partial TCP reads; reassemble multi-record messages | 3h |

#### 12.2 Handshake State Machine
| # | Task | File(s) | Acceptance Criteria | Est. |
|---|------|---------|-------------------|------|
| 7.1.1 | Add `setsockopt(SO_RCVTIMEO)` on accepted client sockets | `src/http_server.c` | Read timeout triggers after configurable seconds (default 30s) | 2h |
| 7.1.2 | Add `setsockopt(SO_SNDTIMEO)` on accepted client sockets | `src/http_server.c` | Write timeout triggers after configurable seconds (default 30s) | 1h |
| 7.1.3 | Add `http_server_set_timeout(server, read_sec, write_sec)` API | `include/kamran.k`, `src/http_server.c` | API documented, validated (rejects negative values) | 2h |
| 7.1.4 | Handle `EAGAIN`/`EWOULDBLOCK` in recv/send loops | `src/http_server.c` | Partial reads/writes retried; timeout returns error code | 3h |
| 7.1.5 | Unit tests for timeout behavior | `tests/test_weblib.c` | Test verifies server rejects slow client simulation | 2h |

#### 12.4 HTTPS Integration
| # | Task | File(s) | Acceptance Criteria | Est. |
|---|------|---------|-------------------|------|
| 7.2.1 | Implement `thread_pool_t` with work queue | `src/thread_pool.c` (new), `src/thread_pool.h` (new) | Create/destroy; submit work items; bounded queue (default 256) | 4h |
| 7.2.2 | Mutex + condition variable synchronization | `src/thread_pool.c` | No data races under concurrent submit; Valgrind clean | 3h |
| 7.2.3 | Integrate thread pool into `http_server_t` | `src/http_server.c` | Threaded mode uses pool instead of thread-per-connection | 3h |
| 7.2.4 | Add `http_server_set_thread_count(server, n)` API | `include/kamran.k`, `src/http_server.c` | Configurable thread count; default 16; min 1, max 256 | 1h |
| 7.2.5 | Unit tests for thread pool | `tests/test_weblib.c` | Create/destroy; submit 100 items; all complete; no leaks | 2h |

#### 14.1 B-tree Core
| # | Task | File(s) | Acceptance Criteria | Est. |
|---|------|---------|-------------------|------|
| 7.3.1 | Add server state enum (RUNNING, DRAINING, STOPPED) | `include/kamran.k`, `src/http_server.c` | State transitions are atomic (`sig_atomic_t`) | 1h |
| 7.3.2 | Implement `http_server_shutdown(server, timeout_sec)` | `src/http_server.c`, `include/kamran.k` | Closes listening socket; waits for in-flight requests up to timeout | 3h |
| 7.3.3 | SIGTERM/SIGINT handler (POSIX) | `src/http_server.c` | Signal triggers state → DRAINING; second signal → immediate exit | 2h |
| 7.3.4 | Thread pool drain on shutdown | `src/thread_pool.c` | All queued work items complete or are cancelled; threads join | 2h |
| 7.3.5 | Unit test: shutdown with active connections | `tests/test_weblib.c` | Server shuts down cleanly; no leaked sockets; Valgrind clean | 3h |

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
| 20.6.1–20.6.3 | v2.0.0 release (CHANGELOG, migration guide, API docs, release automation) | 2d |

### Phase 11 — Advanced Security Hardening — v1.1.0 (6 weeks)

> **Vision**: No one should ever worry about compromising their keys.
> Phase 11 closes every remaining security gap identified in the threat model,
> bringing the library to state-of-the-art security without a single external dependency.

| Week | Milestone | Deliverables | Dependencies |
|------|-----------|-------------|--------------|
| **W18** | Crypto Primitives (Foundation) | `src/crypto/sha256.c` (NIST FIPS 180-4 test vectors); `src/crypto/hmac_sha256.c` (RFC 2104); `src/crypto/aes_gcm.c` (NIST SP 800-38D, 128+256-bit keys) | None — standalone modules |
| **W19** | Key Derivation & Password Hashing | `src/crypto/pbkdf2.c` (RFC 2898, HMAC-SHA256, configurable iterations); `src/crypto/hkdf.c` (RFC 5869 extract+expand); `password_hash_create()` / `password_hash_verify()` API; automatic salt via `secure_random_bytes()` | W18 (SHA-256, HMAC) |
| **W20** | TLS Record Layer & Handshake (Part 1) | `src/tls/tls_record.c` (TLS 1.2 record framing); `src/tls/tls_handshake.c` (ClientHello/ServerHello state machine); PEM certificate parser; private key loader with `mlock()` + `secure_zero()` | W18 (AES-GCM, SHA-256) |
| **W21** | TLS Handshake (Part 2) & Server Integration | Complete handshake; `http_server_enable_tls(server, cert, key)` API; HTTPS example; `curl --tlsv1.2 https://localhost:8443/` validation; TLS key material wiped on `http_server_destroy()` | W20 |
| **W22** | Request ID & IP Access Control Middleware | `src/middleware_request_id.c` (UUID v4 / hex, `X-Request-Id` header, logging integration); `src/middleware_ip_access.c` (allowlist/denylist with CIDR support) | None |
| **W23** | Security Audit Tooling & Hardening | Fuzz testing harness for HTTP parser (`tests/fuzz/`); ASan/MSan CI integration; per-route body size limits; `Content-Length` enforcement before buffering; full regression pass | All prior phases |

#### Phase 11 — Atomic Task Breakdown

##### 11.1 Crypto Primitives
| # | Task | File(s) | Acceptance Criteria | Est. |
|---|------|---------|-------------------|------|
| 11.1.1 | SHA-256 implementation | `src/crypto/sha256.c` | NIST FIPS 180-4 short/long/Monte Carlo test vectors pass | 2d |
| 11.1.2 | HMAC-SHA256 implementation | `src/crypto/hmac_sha256.c` | RFC 4231 test vectors pass; constant-time via `secure_compare()` | 1d |
| 11.1.3 | AES-128/256-GCM implementation | `src/crypto/aes_gcm.c` | NIST SP 800-38D test vectors pass (encrypt + decrypt + auth tag) | 3d |
| 11.1.4 | Unit tests + CI gate | `tests/test_crypto.c` | All NIST vectors pass; Valgrind clean; key material zeroed after use | 1d |

##### 11.2 Password Hashing & Key Derivation
| # | Task | File(s) | Acceptance Criteria | Est. |
|---|------|---------|-------------------|------|
| 11.2.1 | PBKDF2-HMAC-SHA256 | `src/crypto/pbkdf2.c` | RFC 6070 test vectors pass; configurable iteration count (default 600,000) | 2d |
| 11.2.2 | HKDF (extract + expand) | `src/crypto/hkdf.c` | RFC 5869 Appendix A test vectors pass | 1d |
| 11.2.3 | `password_hash_create/verify` API | `src/password.c`, `include/kamran.k` | Salt auto-generated; timing-safe verify; hash format includes iteration count | 2d |
| 11.2.4 | Unit tests | `tests/test_weblib.c` | Round-trip hash/verify; wrong password fails; different salts produce different hashes | 1d |

##### 11.3 TLS 1.2 Transport Encryption
| # | Task | File(s) | Acceptance Criteria | Est. |
|---|------|---------|-------------------|------|
| 11.3.1 | TLS record layer (framing) | `src/tls/tls_record.c` | Parse and serialize TLS records; handle fragmentation | 2d |
| 11.3.2 | TLS handshake state machine | `src/tls/tls_handshake.c` | ClientHello → ServerHello → Certificate → KeyExchange → Finished | 3d |
| 11.3.3 | PEM parser + key loader | `src/tls/pem_parser.c` | Load cert chain + private key from PEM files; `mlock()` key pages | 2d |
| 11.3.4 | Server API integration | `src/http_server.c`, `include/kamran.k` | `http_server_enable_tls()` API; private key zeroed on destroy | 2d |
| 11.3.5 | HTTPS example + tests | `examples/https_server.c`, `tests/test_tls.c` | `curl --tlsv1.2` returns 200; browser green lock | 2d |

##### 11.4 Request ID & IP Access Control
| # | Task | File(s) | Acceptance Criteria | Est. |
|---|------|---------|-------------------|------|
| 11.4.1 | Request ID middleware | `src/middleware_request_id.c` | Generates unique ID; sets `X-Request-Id` header; propagates if present | 1d |
| 11.4.2 | IP allowlist/denylist middleware | `src/middleware_ip_access.c` | Configurable lists; CIDR support; per-route or global | 2d |
| 11.4.3 | Unit tests | `tests/test_weblib.c` | Request ID unique across requests; blocked IPs get 403 | 1d |

##### 11.5 Security Audit Tooling
| # | Task | File(s) | Acceptance Criteria | Est. |
|---|------|---------|-------------------|------|
| 11.5.1 | HTTP parser fuzz harness | `tests/fuzz/fuzz_http_parser.c` | AFL/libFuzzer compatible; runs 1M iterations without crash | 2d |
| 11.5.2 | ASan/MSan CI integration | `.github/workflows/ci.yml` | Sanitizer builds run on every push; zero errors | 1d |
| 11.5.3 | Per-route body size limits | `src/http_server.c`, `include/kamran.k` | `router_set_max_body(route, bytes)` API; reject before buffering | 1d |

#### Phase 11 — Success Criteria

| Module | Validation |
|--------|-----------|
| SHA-256 | NIST FIPS 180-4 test vectors pass (short msg, long msg, Monte Carlo) |
| HMAC-SHA256 | RFC 4231 test vectors pass |
| AES-GCM | NIST SP 800-38D test vectors pass (128+256-bit keys) |
| PBKDF2 | RFC 6070 test vectors pass; 600K iterations in < 1s |
| HKDF | RFC 5869 Appendix A test vectors pass |
| Password hashing | Round-trip create/verify; wrong password → false; salts differ |
| TLS 1.2 | `curl --tlsv1.2 https://localhost:8443/` → 200; browser connects |
| TLS key safety | Private key in `mlock()`'d pages; `secure_zero()` on destroy; never logged |
| Request ID | Unique per request; `X-Request-Id` in response; round-trip propagation |
| IP access control | Allowlist passes; denylist blocks (403); CIDR ranges work |
| Fuzz testing | 1M iterations, zero crashes; zero ASan/MSan errors |

---

## 8. Parallel Build Strategy

Tasks with no data dependencies can be developed simultaneously:

```
PARALLEL GROUP A (W1–W4, Phase 11):
├── 11.1 SHA-256/384          ← src/tls/sha256.c (standalone)
├── 11.2 AES-256-GCM          ← src/tls/aes_gcm.c (standalone)
├── 11.3 ChaCha20-Poly1305    ← src/tls/chacha20_poly1305.c (standalone)
└── 11.4 X25519               ← src/tls/x25519.c (standalone)

SEQUENTIAL (W5–W6, Phase 11):
└── 11.5 HKDF                 ← depends on 11.1 (HMAC-SHA256)

PARALLEL GROUP B (W7–W10, Phase 12):
├── 12.1 TLS Record Layer     ← depends on Phase 11 (crypto)
├── 12.3 Certificate Handling  ← depends on Phase 11 (SHA-256)
└── 13.1 HTTP/2 Framing        ← src/http2/frame.c (standalone, no TLS dependency)

PARALLEL GROUP C (W8–W10, Phase 12+13):
├── 12.2 TLS Handshake        ← depends on 12.1 + 11.4 (X25519)
├── 13.2 HPACK Compression    ← src/http2/hpack.c (standalone)
└── 14.1 B-tree Core          ← src/storage/btree.c (standalone)

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

### Phase 11 Checkpoints (Crypto Primitives)

| Module | Unit Test Gate | Security Gate | Performance Gate |
|--------|---------------|--------------|-----------------|
| SHA-256 | NIST FIPS 180-4 test vectors (3 sets) | No timing side-channels in compression function | ≥ 200 MB/s on x86-64 |
| AES-256-GCM | NIST SP 800-38D test cases 1–18 | Constant-time tag verification | ≥ 100 MB/s (software) |
| ChaCha20-Poly1305 | RFC 8439 §A test vectors | No secret-dependent branches | ≥ 300 MB/s on x86-64 |
| X25519 | RFC 7748 §5.2 Alice/Bob test vectors | Montgomery ladder constant-time | Key exchange < 1ms |
| HKDF | RFC 5869 test vectors (3 test cases) | Key material zeroed after use | N/A (not a bottleneck) |

### Phase 12 Checkpoints (TLS 1.3)

| Module | Validation |
|--------|-----------|
| TLS Record Layer | Encrypt/decrypt round-trip; reject oversized records; handle partial TCP reads |
| TLS Handshake | Complete handshake with `curl --tlsv1.3`; browser green lock icon |
| Certificate Handling | Parse Let's Encrypt cert chain; verify signature; reject expired certs |
| HTTPS Integration | `https://localhost:8443/` returns 200; ALPN negotiates `h2` or `http/1.1` |

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
| v2.0.0 Release | All 250+ tests pass; zero warnings; CHANGELOG complete; migration guide accurate |

---

## 10. QA Pipeline

### Automated Testing (Every Commit)

```
┌─────────────────────────────────────────────────────────────┐
│  GitHub Actions CI Pipeline (v2.0.0)                         │
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
| M1 | TLS browser compatibility | Chrome + Firefox → `https://localhost:8443` | Green lock; TLS 1.3 in certificate info |
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
| **TLS implementation bugs** | Tampering / Disclosure | TLS handshake | NIST test vectors; constant-time ops; fuzz testing; security audit (Phase 12) |
| **TLS downgrade to 1.2** | Tampering | TLS negotiation | Only support TLS 1.3; reject lower versions in ClientHello (Phase 12) |
| **TLS private key extraction** | Disclosure | Key material in memory | `mlock()` key pages; `explicit_bzero()` before free; never log key material (Phase 12) |
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
| TLS private key | TLS handshake module only | Loaded once at startup | `mlock()`'d; zeroed on server destroy |
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
| Pure C TLS 1.3 has security bugs | HIGH | CRITICAL | Comprehensive test vectors (NIST + RFC); constant-time audit; fuzz testing; optional compile-time `WEBLIB_DISABLE_TLS` flag; recommend reverse proxy (nginx) for high-security deployments |
| X25519 field arithmetic bugs | MEDIUM | CRITICAL | RFC 7748 test vectors; comparison against known-good implementation output; timing analysis for side channels |
| HTTP/2 complexity causes regressions | MEDIUM | HIGH | Feature-flagged (`http_server_enable_http2()`); disabled by default; h2spec conformance testing |
| QUIC implementation incomplete or buggy | HIGH | HIGH | Feature-flagged; disabled by default; extensive quic-interop-runner testing; clearly marked as experimental in v2.0 |
| B-tree data corruption | LOW | CRITICAL | CRC32 checksums; WAL replay; fsync discipline; crash recovery test (kill -9 + restart) |
| Multi-process deadlock | LOW | HIGH | Shared-nothing design (no shared memory by default); each worker is independent; master only signals |
| Windows IOCP divergence | MEDIUM | MEDIUM | Platform abstraction layer; CI validates Windows build; Windows-specific integration tests |
| Performance regression vs v1.0.0 | MEDIUM | MEDIUM | Benchmark CI gate (±10%); per-phase benchmark comparison; optimize hot paths |
| API breaking changes v1→v2 | MEDIUM | MEDIUM | Migration guide; deprecated v1 APIs retained with `WEBLIB_DEPRECATED` macro; compile-time warnings |
| Scope creep delays v2.0.0 | MEDIUM | MEDIUM | Strict phase gating; each phase ships independently; HTTP/3 can be deferred to v2.1 if needed |

---

## 13. Estimated Timeline Summary

| Phase | Version | Duration | Cumulative | Key Deliverable |
|-------|---------|----------|-----------|-----------------|
| Phase 11 | v1.1.0 | 6 weeks | W1–W6 | Crypto primitives (SHA, AES-GCM, ChaCha20, X25519, HKDF) |
| Phase 12 | v1.2.0 | 6 weeks | W7–W12 | TLS 1.3 handshake + HTTPS server |
| Phase 13 | v1.3.0 | 5 weeks | W13–W17 | HTTP/2 protocol (framing, HPACK, streams, push) |
| Phase 14 | v1.4.0 | 5 weeks | W18–W22 | Persistent storage engine (B-tree, WAL, transactions) |
| Phase 15 | v1.5.0 | 4 weeks | W23–W26 | Advanced middleware (directory listing, SSE, route groups) |
| Phase 16 | v1.6.0 | 5 weeks | W27–W31 | Multi-process architecture (fork, reload, metrics) |
| Phase 17 | v1.7.0 | 4 weeks | W32–W35 | Cross-platform (Windows IOCP, BSD, MSVC, CI matrix) |
| Phase 18 | v1.8.0 | 5 weeks | W36–W40 | Developer experience (config, plugins, templates, debug) |
| Phase 19 | v1.9.0 | 6 weeks | W41–W46 | HTTP/3 & QUIC (UDP, transport, handshake, streams) |
| Phase 20 | v2.0.0 | 4 weeks | W47–W50 | Release engineering (CLI, observability, fuzz, release) |
| **Total** | | **50 weeks** | | **~12 months from v1.0.0 to v2.0.0** |

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

---

**Maintained by**: MCWL Core Team
**Last Updated**: 2026-03-02
**Status**: Phase 11 planned — advanced security hardening
**License**: MIT (see LICENSE file)

For questions or discussions about this roadmap, please open an issue on GitHub or contact the maintainers.
