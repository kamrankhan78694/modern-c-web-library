# TODO - Future Enhancements

This document tracks planned features, enhancements, and improvements for the Modern C Web Library. Contributors are welcome to work on any of these items!

## Legend
- 🎯 High Priority
- 🔧 Medium Priority
- 💡 Nice to Have
- ✅ Completed
- 🚧 In Progress

## Core Features

### Protocol Support
- [x] ✅ **WebSocket Support** - Enable real-time bidirectional communication
  - WebSocket handshake (RFC 6455 compliant)
  - Frame encoding/decoding with masking
  - Text and binary messages
  - Ping/pong support (automatic pong responses)
  - Message fragmentation handling
  - Close frames with status codes
  - **Frame Processing (Threaded Mode)** - Completed 2025-01-12
    - Persistent WebSocket connections after HTTP upgrade
    - Full frame processing loop with recv() and websocket_process_data()
    - Multiple concurrent connections support
    - Comprehensive test suite (all tests passing)
    - Plaintext `ws://` only — a WebSocket upgrade on a TLS connection is refused with 503; see the TLS entry below
  - **Frame Processing (Async Mode)** - Completed
    - Integration with event loop (epoll/kqueue/poll)
    - Non-blocking WebSocket I/O
    - Single-threaded high-concurrency support
    - Write queue for non-blocking sends
  
- [x] ✅ **TLS 1.3 Support** - Server-side HTTPS *(Phases 11–12, shipped in v2.0.0 — see [NEXT_PHASE.md](NEXT_PHASE.md) §7)*
  - ⚠️ **EXPERIMENTAL and UNAUDITED** — not for production without an external cryptographic audit. See [`src/tls/README.md`](src/tls/README.md)
  - Opt-in at build time: `-DWEBLIB_ENABLE_TLS=ON` (default OFF — with it off, no `src/tls` code is compiled)
  - Pure C, zero dependencies: SHA-256 + HMAC-SHA256 (`src/crypto/sha256.c`), SHA-512, HKDF, ChaCha20, Poly1305, ChaCha20-Poly1305 AEAD, X25519, Ed25519 (`src/tls/`) — each with RFC known-answer tests
  - One profile only: `TLS_CHACHA20_POLY1305_SHA256` + X25519 + Ed25519
  - Record layer with the 2^14 plaintext limit and fragmentation (`src/tls/record.c`)
  - Server handshake state machine incl. HelloRetryRequest (RFC 8446 §4.1.4), with the §4.4.1 synthetic `message_hash` transcript rewrite on that path (`src/tls/server_handshake.c`)
  - DER/ASN.1 and PEM *parsing* for the PKCS#8 Ed25519 private key (`src/tls/der.c`, `pem.c`, `ed25519_key.c`); the server certificate is base64-decoded from PEM to DER and sent opaquely — it is never parsed as X.509
  - ALPN negotiating `http/1.1` (RFC 7301)
  - Sans-IO connection engine + blocking-socket adapter (`src/tls/tls_khannection.c`, `tls_transport.c`)
  - `http_server_enable_tls(server, cert_pem, cert_len, key_pem, key_len)` — PEM **buffers with lengths**, not file paths (`include/kamran.k`); example in `examples/tls_server.c`
  - Deterministic fuzzer over the untrusted-input path plus a real `openssl s_client` TLS 1.3 interop test (`TlsFuzzTests`, `TlsInteropOpenssl`)
  - **Not delivered** — tracked as Phase 21 in [NEXT_PHASE.md](NEXT_PHASE.md):
    - [ ] External cryptographic audit
    - [ ] AES-256-GCM or any second cipher suite; SHA-384
    - [ ] RSA / ECDSA certificates and X.509 chain *validation* — and with them browser page-load, which Ed25519-only certificates cannot reach
    - [ ] SNI support for virtual hosting
    - [ ] Session resumption / PSK / 0-RTT
    - [ ] TLS client mode (server-side only today)
    - [ ] TLS in async mode (`http_server_enable_tls()` returns -1 there) and WebSocket over TLS (a WS upgrade on a TLS connection is answered with 503)
    - [ ] TLS on the WASM and Cloudflare Workers targets (native-only today)
    - [ ] ALPN `h2` — waits on Phase 13
    - [ ] TLS 1.2 fallback — currently rejected by design; reopening it would need a new decision record

- [ ] 🔧 **HTTP/2 Support** - Implement HTTP/2 protocol *(Phase 13, v2.1.0)*
  - Binary framing layer (RFC 7540, all 10 frame types)
  - HPACK header compression (RFC 7541, static + dynamic tables)
  - Stream multiplexing with priority and flow control
  - Server push (PUSH_PROMISE)
  - `http_server_enable_http2()` API
  - h2 (TLS) + h2c (cleartext) support

- [ ] 💡 **HTTP/3 / QUIC Support** - Next-generation HTTP protocol *(Phase 19, v2.7.0)*
  - UDP socket layer with batch I/O
  - QUIC transport protocol (RFC 9000) with connection migration
  - HTTP/3 framing (RFC 9114) over QUIC streams
  - QPACK header compression (RFC 9204)
  - `http_server_enable_http3()` API

### Request/Response Handling

- [x] ✅ **Complete HTTP Parser** - Fully parse and validate incoming requests
  - Method support beyond GET with explicit error responses
  - Header parsing, canonicalization, and lookup APIs
  - Content-Length and chunked body handling with size safeguards
  - Clear rejection of malformed or oversized payloads

- [x] ✅ **Header & Parameter Storage** - Back middleware and handlers with real data
  - Implement request header access and mutation
  - Persist route parameters for `/path/:id` patterns
  - Support response header setting and serialization

- [x] ✅ **Robust Connection Handling** - Hardening for sync and async servers
  - Looping reads/writes with back-pressure awareness
  - HTTP/1.1 keep-alive negotiation and cleanup
  - Deterministic connection teardown on timeouts and errors

- [x] ✅ **Request Body Parsing** - Handle different content types
  - URL-encoded form data
  - Multipart form data
  - File upload handling
  - Streaming large bodies

- [x] ✅ **Cookie Handling** - Full cookie support
  - Cookie parsing
  - Cookie serialization
  - Secure/HttpOnly flags
  - SameSite attribute

- [x] ✅ **Session Management** - User session handling
  - In-memory session store
  - Cookie-based session transport
  - Session expiration and cleanup
  - Key-value data storage per session

- [x] ✅ **Response Compression** - Reduce bandwidth usage
  - gzip compression (pure C DEFLATE + gzip wrapper)
  - Accept-Encoding content negotiation
  - Smart content-type filtering
  - Automatic compression threshold

### JSON Handling

- [x] ✅ **Complete JSON Support** - Finish parser/serializer edge cases
  - Implement array parsing and serialization ✅
  - JSON array create/append/get/length APIs ✅
  - Nested array support ✅
  - Escape control characters and Unicode consistently
  - Harden number parsing and error signaling

### Static Content

- [x] ✅ **Static File Serving** - Serve static assets
  - Efficient file serving
  - MIME type detection
  - Range requests (partial content)
  - ETag support
  - Cache headers

- [ ] 🔧 **Directory Listing** - Auto-generate directory indexes *(Phase 15, v2.3.0)*
  - Configurable templates
  - File size formatting
  - Sorting options

### Template & View Engines

- [x] ✅ **Template Engine** - Server-side rendering
  - Variable substitution (`{{ variable }}` syntax)
  - Template file loading
  - Context-based rendering
  - HTTP response integration

- [ ] 💡 **Multiple Template Formats** - Support various template languages *(Phase 18, v2.6.0)*
  - Mustache templates (sections, partials, inheritance)
  - Auto-escaping (HTML/URL/JS context-aware)
  - Template includes and compiled template caching

### Data Storage

- [ ] 🔧 **SQLite Integration** - Lightweight embedded database *(see Phase 14 for custom storage engine)*
  - Direct SQLite C API usage (SQLite source code vendored/embedded, no external dependency)
  - Connection pooling
  - Transaction management
  - Query builder helpers

- [ ] 💡 **Custom File-Based Storage** - Simple data persistence *(Phase 14, v2.2.0)*
  - B-tree key-value store (on-disk, memory-mapped)
  - Write-ahead log for crash recovery
  - Transaction support (begin/commit/rollback)
  - Iterator API for range queries

### Security

- [x] ✅ **Rate Limiting** - Prevent abuse
  - IP-based rate limiting
  - Token bucket algorithm
  - Sliding window algorithm
  - Per-route limits

- [x] ✅ **CORS Support** - Cross-origin resource sharing
  - Configurable origins
  - Preflight handling
  - Credential support

- [x] ✅ **Authentication Middleware** - Common auth patterns
  - Basic authentication
  - JWT token validation (HMAC-SHA256)
  - API key authentication

- [x] ✅ **CSRF Protection** - Cross-site request forgery prevention
  - Token generation
  - Token validation
  - Cookie-based tokens (double-submit pattern)

- [x] ✅ **Input Validation** - Request validation helpers
  - Length, charset, integer range validation
  - Email format validation
  - HTML sanitization (XSS prevention)
  - Alphanumeric check

- [x] ✅ **Security Headers Middleware** - Defense-in-depth HTTP headers
  - Content-Security-Policy (XSS prevention)
  - X-Content-Type-Options: nosniff (MIME sniffing prevention)
  - X-Frame-Options (clickjacking protection)
  - Strict-Transport-Security / HSTS (opt-in HTTPS enforcement)
  - Referrer-Policy (referrer leakage control)
  - Permissions-Policy (browser feature restriction)
  - Configurable per-header overrides

- [x] ✅ **Security Utilities** - Core cryptographic primitives
  - `secure_zero()` — compiler-barrier memory wipe (volatile / memset_s)
  - `secure_compare()` — constant-time comparison (timing attack prevention)
  - `secure_random_bytes()` — CSPRNG via /dev/urandom or BCryptGenRandom

- [x] ✅ **Secure Secret Handling** - Protect keys in memory
  - `env_config_get_secure()` — heap-isolated secret buffers
  - `env_secure_value_free()` — scrubs memory before free
  - `env_config_redact()` — log-safe masking of secrets
  - `env_config_is_set()` — presence check without value exposure

### Security — Phase 21: TLS hardening & security residuals

> Everything here is open work. It is the scope that the delivered TLS layer (Phases 11–12, v2.0.0)
> did **not** cover, plus the security middleware that was planned alongside it and never built.
> Tracked as Phase 21 in [NEXT_PHASE.md](NEXT_PHASE.md).

- [x] ✅ **TLS transport encryption** — see the TLS 1.3 entry under *Protocol Support* above.
  Shipped in v2.0.0 as a TLS **1.3** server, EXPERIMENTAL and UNAUDITED. The original plan on this
  line called for TLS 1.2+, AES-128/256-GCM, and RSA/ECDSA certificates; none of that was built, and
  TLS 1.2 was rejected by design. What remains open from it is listed below rather than dropped:
  - [ ] External cryptographic audit of `src/tls/` — the gate that removes "UNAUDITED"
  - [ ] AES-128/256-GCM cipher suites
  - [ ] RSA and ECDSA certificate support, plus X.509 chain validation (unlocks browser page-load)
  - [ ] SNI (Server Name Indication) for virtual hosting
  - [ ] TLS private key memory protection — `secure_zero()` ships; `mlock()` on key pages does not
  - [ ] TLS in async mode, and WebSocket over TLS
  - [ ] TLS client mode, session resumption / 0-RTT

- [ ] 🎯 **Password Hashing** - Secure credential storage (pure C)
  - PBKDF2-HMAC-SHA256 (RFC 2898) with configurable iterations
  - `password_hash_create()` / `password_hash_verify()` API
  - Automatic salt generation via `secure_random_bytes()`
  - Timing-safe verification via `secure_compare()`
  - Tunable work factor for future-proofing

- [x] ✅ **HKDF Key Derivation (RFC 5869)** - Extract-then-expand, TLS-internal *(shipped in v2.0.0)*
  - `src/tls/hkdf.c` — HKDF-Extract / HKDF-Expand over HMAC-SHA256 (`src/crypto/sha256.c`); SHA-256 only
  - `hkdf_expand_label()` — TLS 1.3 HKDF-Expand-Label (RFC 8446 §7.1)
  - TLS 1.3 key schedule in `src/tls/key_schedule.c`
  - RFC 5869 and RFC 8448 known-answer tests (`TlsCryptoTests`)
  - EXPERIMENTAL / UNAUDITED; native-only; built only with `-DWEBLIB_ENABLE_TLS=ON` (default OFF), and not exposed as a public header

- [ ] 🎯 **PBKDF2 Key Derivation (RFC 2898)** - Password-based key derivation
  - Prerequisite for the password hashing module above; no PBKDF2 source exists yet

- [ ] 🔧 **Request ID Middleware** - Trace correlation
  - Generate unique request ID per request (UUID v4 or random hex)
  - Set `X-Request-Id` response header
  - Propagate incoming `X-Request-Id` if present
  - Integrate with logging middleware

- [ ] 🔧 **IP Allowlist / Denylist Middleware** - Network-level access control
  - Configurable IP allowlists and denylists
  - CIDR range support
  - Per-route or global application

- [ ] 🔧 **Content-Length Enforcement** - Body size hardening
  - Per-route maximum body size configuration
  - Reject oversized payloads before buffering
  - Streaming rejection (close connection early)

- [ ] 💡 **Certificate Pinning** - Advanced TLS verification
  - Pin expected server certificate fingerprints
  - Detect MITM attacks on outbound connections

- [ ] 💡 **Security Audit Tooling** - Automated vulnerability detection
  - Built-in fuzz testing harness for HTTP parser *(the TLS untrusted-input path already has one — `tests/test_tls_fuzz.c` / `TlsFuzzTests`)*
  - Memory sanitizer CI integration *(partly done: the `tls-check` CI job runs an ASan/UBSan build over the 7 TLS suites; the rest of the library is covered by Valgrind, not sanitizers)*
  - Static analysis rules for common C vulnerabilities

### Server Lifecycle

- [x] ✅ **Graceful Shutdown & Thread Management** - Reliable server teardown
  - Close listening sockets before joining worker threads
  - Bounded thread pool with configurable worker count (default 16)
  - Server state machine: STOPPED → RUNNING → DRAINING → STOPPED
  - `http_server_shutdown()` API with drain timeout
  - Socket timeouts (`SO_RCVTIMEO`/`SO_SNDTIMEO`) with configurable values
  - `http_server_set_timeout()` and `http_server_set_thread_count()` APIs

### Performance

- [x] ✅ **Caching Layer** - Performance optimization
  - In-memory cache implementation
  - LRU eviction policy
  - Cache invalidation
  - TTL support

- [ ] 🔧 **Load Balancing** - Distribute traffic *(Phase 16, v2.4.0)*
  - Multi-process master-worker model (fork-based)
  - SO_REUSEPORT per-worker accept
  - Worker supervision and auto-restart
  - Per-worker health monitoring

- [ ] 💡 **Worker Pool** - Process management *(Phase 16, v2.4.0)*
  - Multi-process model via fork()
  - Process supervision with auto-restart
  - Zero-downtime hot reload (SIGHUP)
  - Signal-based worker management

### Middleware

- [x] ✅ **CORS Middleware** - Ready-to-use CORS handler
- [x] ✅ **Logging Middleware** - Request/response logging
- [x] ✅ **Body Parser Middleware** - Automatic body parsing
- [x] ✅ **Error Handler Middleware** - Centralized error handling
- [x] ✅ **Metrics Middleware** - Request metrics collection

### Developer Experience

- [ ] 🔧 **Hot Reload** - Automatic server restart on code changes *(Phase 16, v2.4.0)*
- [ ] 🔧 **Debug Mode** - Enhanced debugging features *(Phase 18, v2.6.0)*
  - Verbose logging with request/response headers
  - Request/response inspection with per-middleware timing
  - Memory allocation tracking
  
- [ ] 💡 **CLI Tools** - Command-line utilities *(Phase 20, v3.0.0)*
  - Project scaffolding (`weblib init`)
  - Route listing (`weblib routes`)
  - Configuration validator (`weblib config validate`)

### Documentation & Examples

- [x] ✅ **API Documentation** - Complete API reference
  - Function documentation
  - Parameter descriptions
  - Return value documentation
  - Usage examples

- [ ] 🔧 **More Examples** - Additional example applications *(Phase 15–18)*
  - REST API example ✅ (completed in v1.0.0)
  - WebSocket chat example
  - File upload example
  - Authentication example
  - Data persistence example *(Phase 14)*
  - SSE streaming example *(Phase 15)*

- [ ] 🔧 **Tutorial Series** - Step-by-step guides *(Phase 20, v3.0.0)*
  - Getting started tutorial ✅ (completed in v1.0.0)
  - Building a REST API ✅ (completed in v1.0.0)
  - Real-time applications ✅ (completed in v1.0.0)
  - Production deployment
  - TLS/HTTPS setup — unblocked: the TLS layer shipped in v2.0.0 (`examples/tls_server.c` is the worked example). The tutorial must lead with the EXPERIMENTAL/UNAUDITED caveat and the `-DWEBLIB_ENABLE_TLS=ON` build step
  - Storage engine usage *(Phase 14)*

### Testing & Quality

- [x] ✅ **Networking Integration Tests** - Exercise live socket workflows
  - Automated sync request regression suite (GET, POST, JSON, 404, malformed)
  - Coverage for malformed input and sequential connections
  - Baseline concurrency smoke tests

- [ ] 🔧 **Comprehensive Test Suite** - Expand test coverage *(Phase 20, v3.0.0)*
  - Unit tests for all modules
  - Integration tests
  - Performance tests
  - Stress tests
  - Fuzz testing — TLS untrusted-input path done (`TlsFuzzTests`); HTTP, JSON, HPACK and QUIC parsers still to do

- [x] ✅ **Continuous Integration** - Automated testing
  - GitHub Actions CI: `primary-checks` (Docker GCC build + full ctest + Valgrind), `clang-check`, `macos-check` (pull requests only), `docker-image-check`
  - `tls-check` — a RelWithDebInfo build with `-DWEBLIB_ENABLE_TLS=ON -DWEBLIB_TLS_TEST_HOOKS=ON` running all 13 suites, plus an ASan/UBSan build running the 7 TLS suites
  - Valgrind memory check gate in `primary-checks` (every test binary; a leak in any one fails the job)
  - Platform coverage today is Linux (GCC + Clang) and macOS (Clang, pull requests only); Windows and BSD runners are still open *(Phase 17, v2.5.0)*

- [x] ✅ **Benchmarking Suite** - Performance benchmarks
  - Throughput tests (requests/sec)
  - Latency percentiles (p50/p95/p99)
  - Live server integration

### Cross-Platform

- [ ] 🔧 **Windows Improvements** - Better Windows support *(Phase 17, v2.5.0)*
  - IOCP event loop backend (`src/event_loop_iocp.c`)
  - MSVC build support (CMake generator)
  - Windows-specific CI runner
  - Platform abstraction layer (`src/platform.h`)

- [ ] 💡 **BSD Support** - Explicit BSD testing and support *(Phase 17, v2.5.0)*
  - FreeBSD CI testing
  - OpenBSD CI testing
  - NetBSD CI testing
  - Platform compatibility matrix documentation

### Monitoring & Observability

- [ ] 💡 **Prometheus Metrics** - Metrics export *(Phase 20, v3.0.0)*
  - Prometheus exposition format endpoint (`/metrics`)
  - HTTP counters, gauges, histograms
  - Custom metric labels
  - Per-worker metrics aggregation *(Phase 16)*

- [x] ✅ **Health Check Endpoint** - Service health monitoring
  - GET /healthz with JSON status and uptime
  - Suitable for load-balancer and Kubernetes probes

- [ ] 💡 **OpenTelemetry Support** - Distributed tracing *(Phase 20, v3.0.0)*
  - W3C Trace Context (`traceparent` header) propagation
  - Span creation and completion
  - Trace ID in structured log output

## Completed Features

- ✅ **HTTP Server** - Basic HTTP server implementation
- ✅ **Async I/O** - Event loop with epoll/kqueue/poll
- ✅ **Event Loop** - High-performance non-blocking I/O
- ✅ **Routing** - Flexible routing with parameters
- ✅ **Middleware** - Middleware chain support
- ✅ **JSON Support** - JSON parser and serializer
- ✅ **Cross-Platform Build** - CMake-based build system
- ✅ **Basic Examples** - Simple and async server examples
- ✅ **Unit Tests** - Basic test infrastructure
- ✅ **WebSocket Support** - RFC 6455 compliant WebSocket implementation with full protocol support
- ✅ **Request Body Parsing** - URL-encoded and multipart form data parsing with file uploads
- ✅ **Cookie Handling** - Full RFC 6265 cookie support with all attributes
- ✅ **CORS Support** - Configurable Cross-Origin Resource Sharing middleware
- ✅ **Rate Limiting** - IP-based token bucket rate limiting middleware
- ✅ **Static File Serving** - MIME detection, ETag, caching, path traversal prevention
- ✅ **Session Management** - Cookie-based sessions with expiration, cleanup, and data storage
- ✅ **Template Engine** - `{{ variable }}` syntax with context and file loading
- ✅ **Authentication Middleware** - Basic Auth, API Key, JWT (HMAC-SHA256) — pure C
- ✅ **Database Connection Pool** - Thread-safe pooling with configurable lifecycle
- ✅ **API Documentation** - Comprehensive API reference in `docs/api/`
- ✅ **Thread Pool** - Bounded thread pool replacing thread-per-connection model
- ✅ **Socket Timeouts** - Configurable SO_RCVTIMEO/SO_SNDTIMEO on client connections
- ✅ **Graceful Shutdown** - Server state machine with drain timeout and thread pool cleanup
- ✅ **CSRF Protection** - Double-submit cookie pattern with constant-time comparison
- ✅ **Input Validation** - Length, charset, integer, email validation and HTML sanitization
- ✅ **Logging Middleware** - Configurable request logging with log levels
- ✅ **Error Handler Middleware** - Automatic JSON error responses for 4xx/5xx status codes
- ✅ **Health Check Endpoint** - GET /healthz with JSON status and uptime
- ✅ **Networking Integration Tests** - Live socket tests for HTTP protocol conformance
- ✅ **Deployment Documentation** - Production deployment guide with Docker, systemd, nginx patterns
- ✅ **In-Memory Cache** - LRU eviction, TTL support, thread-safe hash table implementation
- ✅ **Metrics Middleware** - Request counting, per-method tracking, status code ranges, JSON endpoint
- ✅ **Response Compression** - Pure C gzip (RFC 1952) with DEFLATE (RFC 1951), Accept-Encoding negotiation
- ✅ **Async WebSocket** - Event loop integration, non-blocking I/O, write queue, connection manager
- ✅ **Benchmarking Suite** - High-resolution timing, throughput/latency measurement, percentile statistics
- ✅ **Security Headers Middleware** - CSP, HSTS, X-Content-Type-Options, X-Frame-Options, Referrer-Policy, Permissions-Policy
- ✅ **Security Utilities** - `secure_zero()`, `secure_compare()`, `secure_random_bytes()` — core crypto primitives
- ✅ **Secure Secret Handling** - Heap-isolated secrets with memory wipe, log-safe redaction, presence checks
- ✅ **WebAssembly (WASM) Target** — Emscripten build path with a WASM-safe source subset (`WEBLIB_SOURCES_WASM_SAFE` in `CMakeLists.txt`), the `wasm_*` API (`src/wasm_runtime.c`), and the `WasmTests` suite. WebSocket, async WebSocket, the benchmark harness and the TLS layer are excluded on that target
- ✅ **Cloudflare Workers Runtime** — fetch-event bridge (`worker_*` API, `src/worker_runtime.c`) plus in-memory emulations of the KV, R2, D1 and Queues bindings (`src/worker_kv.c`, `worker_r2.c`, `worker_d1.c`, `worker_queues.c`), with the `WorkerTests` suite
- ✅ **TLS 1.3 Server (EXPERIMENTAL / UNAUDITED — not for production)** — pure-C, zero-dependency, server-side only, threaded mode only, native-only; `TLS_CHACHA20_POLY1305_SHA256` + X25519 + Ed25519 only; `http_server_enable_tls()`; opt-in via `-DWEBLIB_ENABLE_TLS=ON` (OFF by default). `openssl s_client` interop verified; browser page-load not achieved

## Community Requests

This section will track feature requests from the community. Please open an issue to suggest new features!

---

## Next Phase Roadmap

For a detailed, phased implementation plan with timelines, priorities, and implementation guidance, see **[NEXT_PHASE.md](NEXT_PHASE.md)**.

### Completed Phases

- **Phase 1 (v0.1.0)**: ✅ Foundation — HTTP server, event loop (epoll/kqueue/poll), routing, middleware, JSON, CMake build
- **Phase 2 (v0.2.0)**: ✅ WebSocket Protocol — RFC 6455 handshake, framing, masking, fragmentation, control frames
- **Phase 3 (v0.3.0)**: ✅ WebSocket Production — threaded mode frame processing, persistent connections, ping/pong
- **Phase 4 (v0.4.0)**: ✅ HTTP Foundation Hardening — parser, headers, connections, JSON arrays, graceful shutdown
- **Phase 5 (v0.5.0)**: ✅ Request Processing & Security — body parsing, cookies, CORS, rate limiting, static files
- **Phase 6 (v0.6.0)**: ✅ Production Readiness — sessions, template engine, auth middleware, db pooling, API docs
- **Phase 7 (v0.7.0)**: ✅ Server Hardening & CI — socket timeouts, thread pool, graceful shutdown, CI pipeline, integration tests, parser hardening
- **Phase 8 (v0.8.0)**: ✅ Security & Observability — CSRF middleware ✅, logging ✅, error handler ✅, input validation ✅, health check ✅
- **Phase 9 (v0.9.0)**: ✅ Performance & Observability — caching layer ✅, metrics middleware ✅, response compression ✅, async WebSocket ✅, benchmarking suite ✅
- **Phase 10 (v1.0.0)**: ✅ Release Readiness — REST API example ✅, tutorials ✅, documentation ✅, CHANGELOG ✅, semantic versioning ✅
- **Phase 11 (v2.0.0)**: ✅ TLS Foundation — SHA-256, SHA-512, HMAC, HKDF, ChaCha20, Poly1305, ChaCha20-Poly1305 AEAD, X25519, Ed25519, all with RFC known-answer tests (`TlsCryptoTests`). **EXPERIMENTAL and UNAUDITED**, off by default (`-DWEBLIB_ENABLE_TLS=ON` to build it), native-only. Planned as v1.1.0; landed in v2.0.0 alongside Phase 12. **SHA-384 and AES-256-GCM were deliberately dropped** — see [`src/tls/README.md`](src/tls/README.md)
- **Phase 12 (v2.0.0)**: ✅ TLS 1.3 Handshake & HTTPS — record layer with the 2^14 plaintext limit and fragmentation (`src/tls/record.c`), server handshake state machine incl. HelloRetryRequest (RFC 8446 §4.1.4) with the §4.4.1 synthetic `message_hash` transcript rewrite (`src/tls/server_handshake.c`), DER/PEM certificate + PKCS#8 Ed25519 key parsing (`src/tls/der.c`, `pem.c`, `ed25519_key.c`), ALPN `http/1.1`, and `http_server_enable_tls()` (`include/kamran.k`). **EXPERIMENTAL and UNAUDITED**, server-side only, threaded mode only, native-only, one profile (`TLS_CHACHA20_POLY1305_SHA256` / X25519 / Ed25519). Real `openssl s_client` TLS 1.3 interop verified (`TlsInteropOpenssl`); browser page-load **not** achieved. Planned as v1.2.0; landed in v2.0.0

### Planned Phases

> Phases 11 and 12 shipped together in v2.0.0 rather than as separate v1.1.0 / v1.2.0 releases, so
> the version targets below have been re-baselined. Phase numbers and scope are unchanged.

- **Phase 13 (v2.1.0)**: 🎯 HTTP/2 Protocol — binary framing, HPACK compression, stream multiplexing, flow control, server push
- **Phase 14 (v2.2.0)**: 🔧 Persistent Storage Engine — B-tree key-value store, write-ahead log, transactions, crash recovery, iterator API
- **Phase 15 (v2.3.0)**: 🔧 Advanced Middleware & Content — directory listing, Server-Sent Events, content negotiation, route groups, regex routes
- **Phase 16 (v2.4.0)**: 🔧 Multi-Process Architecture — master-worker fork model, SO_REUSEPORT, zero-downtime reload, per-worker metrics
- **Phase 17 (v2.5.0)**: 🔧 Cross-Platform Hardening — platform abstraction layer, Windows IOCP, BSD testing, MSVC build, CI matrix expansion
- **Phase 18 (v2.6.0)**: 🔧 Developer Experience & Configuration — INI config parser, plugin architecture, advanced templates, debug mode, API versioning
- **Phase 19 (v2.7.0)**: 💡 HTTP/3 & QUIC — UDP transport, QUIC protocol (RFC 9000), connection migration, HTTP/3 framing, QPACK compression
- **Phase 20 (v3.0.0)**: 💡 Release Engineering & Ecosystem — CLI tools, Prometheus metrics, OpenTelemetry tracing, fuzz testing, v3.0.0 release
- **Phase 21 (unscheduled)**: 🎯 Security Residuals & TLS Hardening — external cryptographic audit of `src/tls/`, RSA/ECDSA certificates + X.509 chain validation (and with them browser interop), AES-256-GCM, TLS in async mode, WebSocket over TLS, session resumption, SNI, TLS client mode, PBKDF2 password hashing, request-ID middleware, IP allowlist/denylist

## How to Contribute

Interested in working on any of these features? Great!

1. Check if there's an existing issue for the feature
2. Review **[NEXT_PHASE.md](NEXT_PHASE.md)** for implementation details and priorities
3. If not, create a new issue to discuss the implementation
4. Fork the repository and create a feature branch
5. Implement the feature following our coding standards
6. Add tests and documentation
7. Submit a pull request

See [CONTRIBUTING.md](CONTRIBUTING.md) for detailed contribution guidelines.

## Priority Guidelines

- **🎯 High Priority**: Essential features for production use
- **🔧 Medium Priority**: Important enhancements that improve functionality
- **💡 Nice to Have**: Features that would be great but not critical

Priorities may change based on community feedback and project direction.

---

**Last Updated**: 2026-07-27 (v2.0.0)  
**Maintainer**: [@kamrankhan78694](https://github.com/kamrankhan78694)
