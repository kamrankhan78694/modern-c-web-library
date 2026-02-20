# Next Phase Roadmap — Modern C Web Library v0.7.0+

> **Methodology**: First-principles engineering workflow.
> Every decision below traces back to a verifiable technical constraint, not convention.

---

## 1. Idea Intake

**Core problem in one sentence**: The library has shipped a feature-rich v0.6.0 but lacks the connection-level hardening, transport security, CI automation, and observability required for unsupervised production deployment.

---

## 2. Crystallized Brief

| Dimension | Detail |
|-----------|--------|
| **Target users** | C developers building HTTP/WebSocket backends who refuse external dependencies |
| **Desired outcomes** | A server that survives hostile network conditions, encrypts traffic, shuts down without data loss, and proves correctness through automated CI on every commit |
| **Non-goals** | HTTP/3 / QUIC (premature without TLS); framework-level ORM; language bindings; GUI tooling |

---

## 3. Completed Phases (Reference)

| Phase | Version | Status | Highlights |
|-------|---------|--------|------------|
| Phase 4 | v0.4.0 | ✅ Complete | HTTP parser hardening, header storage, JSON arrays, connection handling |
| Phase 5 | v0.5.0 | ✅ Complete | Body parsing, cookies, CORS, rate limiting, static file serving |
| Phase 6 | v0.6.0 | ✅ Complete | Sessions, template engine, auth middleware (Basic/JWT/API-Key), DB pooling, API docs |
| Phase 7 | v0.7.0 | ✅ Complete | Socket timeouts, thread pool, graceful shutdown, GitHub Actions CI, integration tests |
| Phase 8 | v0.8.0 | ✅ Complete | CSRF middleware, logging, error handler, input validation, health check |
| Phase 9 | v0.9.0 | ✅ Complete | Response compression, caching layer, metrics middleware, async WebSocket, benchmarking suite |

**Current state**: 71/71 unit tests passing · zero compiler warnings · 14 source modules · 4 example servers

---

## 4. Grounded First-Principles Design

### What makes a network server production-safe?

Working from the socket up, not from features down:

1. **Timeout enforcement** — A server without socket timeouts is vulnerable to Slowloris and connection exhaustion. The current `recv()` call in the connection loop blocks indefinitely.
2. **Transport encryption** — Plaintext HTTP is rejected by browsers and load balancers. TLS 1.2+ is non-negotiable for production.
3. **Graceful lifecycle** — Crash-stopping drops in-flight requests. A state machine (running → draining → stopped) with connection drain timeout is required.
4. **Automated proof** — Without CI running tests on every push, regressions ship undetected. GitHub Actions with build + test + Valgrind is the minimum.
5. **Observability** — A server without structured logging is a black box under incident. Logging middleware feeds debugging and monitoring.
6. **Defensive parsing** — Edge cases in chunked encoding, duplicate headers, and request smuggling need targeted hardening.

### Architecture Decision Records

| Decision | Rationale |
|----------|-----------|
| Socket timeouts via `setsockopt(SO_RCVTIMEO/SO_SNDTIMEO)` | Portable POSIX, no extra threads, prevents indefinite blocking |
| Pure C TLS (not OpenSSL) | Aligns with zero-dependency principle; vendor `src/tls/` subdirectory |
| Thread pool instead of thread-per-connection | Bounded resource usage; prevents fork-bomb under load |
| Structured logging to `FILE *` stream | Zero-allocation hot path; configurable destination (stderr, file, custom) |
| GitHub Actions CI matrix: Linux + macOS | Covers epoll + kqueue backends; Windows IOCP deferred to Phase 10 |

---

## 5. Adversarial Review

| Attack Vector / Failure Mode | Current Exposure | Mitigation (Phase) |
|------------------------------|-----------------|---------------------|
| **Slowloris** (slow headers) | CRITICAL: `recv()` blocks forever | Socket read timeout (Phase 7) |
| **Slow POST** (slow body) | CRITICAL: body read has no deadline | Body read timeout (Phase 7) |
| **Connection exhaustion** | HIGH: thread-per-connection, no cap | Thread pool with bounded queue (Phase 7) |
| **Plaintext credentials** | HIGH: no TLS | Pure C TLS 1.2+ (Phase 8) |
| **CSRF** | MEDIUM: no token validation | CSRF middleware with double-submit cookie (Phase 8) |
| **Request smuggling** | LOW: Content-Length + Transfer-Encoding conflict checked | Add duplicate Transfer-Encoding detection (Phase 7) |
| **Memory leaks under error paths** | LOW: parser malloc on line 1046 may leak on early return | Valgrind CI gate (Phase 7) |
| **No regression detection** | HIGH: no CI pipeline | GitHub Actions on every push (Phase 7) |
| **Silent failures** | MEDIUM: errors go to fprintf(stderr) | Structured logging middleware (Phase 8) |
| **Stale connections after shutdown** | MEDIUM: no drain phase | Graceful shutdown state machine (Phase 7) |

---

## 6. Design Iteration (Refined Architecture)

Based on adversarial review, the phases are ordered by **blast radius** — the damage caused if the gap is not closed:

```
Phase 7 (v0.7.0): Server Hardening & CI
   ├── Socket timeouts (blocks Slowloris/Slow-POST)
   ├── Thread pool (bounds resource usage)
   ├── Graceful shutdown (drain + timeout)
   ├── Connection hardening (keep-alive limits, partial I/O loops)
   ├── GitHub Actions CI (build + test + Valgrind on Linux/macOS)
   └── Networking integration tests

Phase 8 (v0.8.0): Security & Observability
   ├── Pure C TLS 1.2 (AES-GCM, SHA-256, RSA/ECDSA)
   ├── CSRF middleware (double-submit cookie)
   ├── Logging middleware (structured, configurable levels)
   ├── Error handler middleware (centralized 4xx/5xx responses)
   └── Input validation helpers (sanitization, length checks)

Phase 9 (v0.9.0): Performance & Protocol
   ├── HTTP/2 binary framing + stream multiplexing
   ├── Response compression (gzip/deflate, pure C)
   ├── In-memory caching layer (LRU, TTL)
   ├── Async WebSocket mode (event loop integration)
   └── Benchmarking suite

Phase 10 (v1.0.0): Release Readiness
   ├── Tutorial series (REST API, WebSocket chat, file upload)
   ├── REST API example application
   ├── Windows IOCP support
   ├── BSD platform testing
   ├── Health check endpoint
   └── CHANGELOG + semantic versioning enforcement
```

---

## 7. Milestone Roadmap (1–2 Week Slices)

### Phase 7: Server Hardening & CI — v0.7.0 (4 weeks)

| Week | Milestone | Deliverables | Dependencies |
|------|-----------|-------------|--------------|
| **W1** | Socket Timeouts & Connection Hardening | `setsockopt(SO_RCVTIMEO)` on accept; partial-I/O `recv()`/`send()` loops; keep-alive request limit (100 req/conn default) | None |
| **W2** | Thread Pool & Graceful Shutdown | Bounded thread pool (configurable size, default 16); server state machine (running→draining→stopped); `http_server_shutdown()` API; SIGTERM/SIGINT handler | W1 (timeout feeds drain logic) |
| **W3** | GitHub Actions CI + Integration Tests | `.github/workflows/ci.yml` (Linux gcc + macOS clang); Valgrind memcheck gate; `tests/integration/` with raw-socket HTTP client; malformed input + concurrent connection tests | W1+W2 (stable server to test against) |
| **W4** | Parser Hardening & Stabilization | Duplicate Transfer-Encoding detection; `Expect: 100-continue` handling; chunked-size overflow clamping; comprehensive edge-case unit tests; full regression pass | W1-W3 (CI catches regressions) |

### Phase 8: Security & Observability — v0.8.0 (5 weeks)

| Week | Milestone | Deliverables | Dependencies |
|------|-----------|-------------|--------------|
| **W5** | Crypto Primitives | `src/tls/crypto_sha256.c` (SHA-256); `src/tls/crypto_aes_gcm.c` (AES-128/256-GCM); `src/tls/crypto_rsa.c` (RSA PKCS#1 v1.5 + OAEP) | None (standalone modules) |
| **W6** | TLS Record Layer & Handshake (Part 1) | `src/tls/tls_record.c` (record framing); `src/tls/tls_handshake.c` (ClientHello/ServerHello); PEM certificate parser; key loading | W5 (crypto primitives) |
| **W7** | TLS Handshake (Part 2) & Integration | Complete handshake state machine; `http_server_enable_tls(server, cert, key)` API; HTTPS example server; TLS unit tests with test vectors | W6 |
| **W8** | Logging & Error Handler Middleware | `src/middleware_log.c` (configurable log levels: DEBUG/INFO/WARN/ERROR; format: timestamp, method, path, status, duration); `src/middleware_error.c` (centralized error pages, custom error callbacks) | None |
| **W9** | CSRF Middleware & Input Validation | `src/middleware_csrf.c` (double-submit cookie, per-request token generation, constant-time comparison); `src/input_validation.c` (string length check, allowed-character filter, integer range validation) | Phase 7 CI (validates correctness) |

### Phase 9: Performance & Protocol — v0.9.0 (5 weeks)

| Week | Milestone | Deliverables | Dependencies |
|------|-----------|-------------|--------------|
| **W10** | HTTP/2 Framing & Streams | `src/http2/` directory; binary frame parser; stream multiplexing; HPACK header compression (static + dynamic tables); settings frame handling | Phase 7 (hardened connection layer) |
| **W11** | HTTP/2 Integration | Server push; stream prioritization; integration with existing router; `http_server_enable_http2()` API; HTTP/2 unit tests | W10 |
| **W12** | Response Compression | Pure C gzip (DEFLATE + gzip header, based on RFC 1951/1952); `Accept-Encoding` negotiation; `Content-Encoding: gzip` header; minimum size threshold (default 1KB) | None |
| **W13** | Caching Layer & Async WebSocket | `src/cache.c` (LRU eviction, TTL, configurable max size); async WebSocket frame processing in event loop; non-blocking WebSocket sends with write queue | Phase 7 (event loop stability) |
| **W14** | Benchmarking Suite | `tests/benchmark/` directory; throughput test (requests/sec); latency percentiles (p50/p95/p99); memory usage tracking; comparison scripts; CI integration for regression detection | All prior phases |

### Phase 10: Release Readiness — v1.0.0 (3 weeks)

| Week | Milestone | Deliverables | Dependencies |
|------|-----------|-------------|--------------|
| **W15** | Examples & Tutorials | `examples/rest_api_server.c` (full CRUD); `examples/websocket_chat.c`; `examples/file_upload.c`; step-by-step tutorial docs in `docs/tutorials/` | All features complete |
| **W16** | Platform Hardening | Windows IOCP event loop backend; BSD (FreeBSD/OpenBSD) CI testing; platform-specific CI matrix expansion; `docs/PLATFORM.md` compatibility matrix | Phase 7 CI infrastructure |
| **W17** | Release Engineering | Semantic versioning enforcement; `CHANGELOG.md` finalization; health check endpoint (`/healthz`); release automation in GitHub Actions; v1.0.0 tag and release | All prior phases |

---

## 8. Atomic Task Breakdown

### Phase 7 — Server Hardening & CI

#### 7.1 Socket Timeouts
| # | Task | File(s) | Acceptance Criteria | Est. |
|---|------|---------|-------------------|------|
| 7.1.1 | Add `setsockopt(SO_RCVTIMEO)` on accepted client sockets | `src/http_server.c` | Read timeout triggers after configurable seconds (default 30s) | 2h |
| 7.1.2 | Add `setsockopt(SO_SNDTIMEO)` on accepted client sockets | `src/http_server.c` | Write timeout triggers after configurable seconds (default 30s) | 1h |
| 7.1.3 | Add `http_server_set_timeout(server, read_sec, write_sec)` API | `include/weblib.h`, `src/http_server.c` | API documented, validated (rejects negative values) | 2h |
| 7.1.4 | Handle `EAGAIN`/`EWOULDBLOCK` in recv/send loops | `src/http_server.c` | Partial reads/writes retried; timeout returns error code | 3h |
| 7.1.5 | Unit tests for timeout behavior | `tests/test_weblib.c` | Test verifies server rejects slow client simulation | 2h |

#### 7.2 Thread Pool
| # | Task | File(s) | Acceptance Criteria | Est. |
|---|------|---------|-------------------|------|
| 7.2.1 | Implement `thread_pool_t` with work queue | `src/thread_pool.c` (new), `src/thread_pool.h` (new) | Create/destroy; submit work items; bounded queue (default 256) | 4h |
| 7.2.2 | Mutex + condition variable synchronization | `src/thread_pool.c` | No data races under concurrent submit; Valgrind clean | 3h |
| 7.2.3 | Integrate thread pool into `http_server_t` | `src/http_server.c` | Threaded mode uses pool instead of thread-per-connection | 3h |
| 7.2.4 | Add `http_server_set_thread_count(server, n)` API | `include/weblib.h`, `src/http_server.c` | Configurable thread count; default 16; min 1, max 256 | 1h |
| 7.2.5 | Unit tests for thread pool | `tests/test_weblib.c` | Create/destroy; submit 100 items; all complete; no leaks | 2h |

#### 7.3 Graceful Shutdown
| # | Task | File(s) | Acceptance Criteria | Est. |
|---|------|---------|-------------------|------|
| 7.3.1 | Add server state enum (RUNNING, DRAINING, STOPPED) | `include/weblib.h`, `src/http_server.c` | State transitions are atomic (`sig_atomic_t`) | 1h |
| 7.3.2 | Implement `http_server_shutdown(server, timeout_sec)` | `src/http_server.c`, `include/weblib.h` | Closes listening socket; waits for in-flight requests up to timeout | 3h |
| 7.3.3 | SIGTERM/SIGINT handler (POSIX) | `src/http_server.c` | Signal triggers state → DRAINING; second signal → immediate exit | 2h |
| 7.3.4 | Thread pool drain on shutdown | `src/thread_pool.c` | All queued work items complete or are cancelled; threads join | 2h |
| 7.3.5 | Unit test: shutdown with active connections | `tests/test_weblib.c` | Server shuts down cleanly; no leaked sockets; Valgrind clean | 3h |

#### 7.4 GitHub Actions CI
| # | Task | File(s) | Acceptance Criteria | Est. |
|---|------|---------|-------------------|------|
| 7.4.1 | Create `.github/workflows/ci.yml` | `.github/workflows/ci.yml` (new) | Triggers on push + PR to main; Linux (gcc) + macOS (clang) matrix | 2h |
| 7.4.2 | Build step: cmake + make | `.github/workflows/ci.yml` | Zero warnings (`-Werror`); both static and shared lib | 1h |
| 7.4.3 | Test step: run `test_weblib` | `.github/workflows/ci.yml` | All tests pass; non-zero exit on failure | 1h |
| 7.4.4 | Valgrind memcheck step (Linux only) | `.github/workflows/ci.yml` | Zero errors, zero leaks; fail build on Valgrind error | 2h |
| 7.4.5 | Cache cmake build directory | `.github/workflows/ci.yml` | Incremental builds use cache; CI time < 5 min | 1h |

#### 7.5 Integration Tests
| # | Task | File(s) | Acceptance Criteria | Est. |
|---|------|---------|-------------------|------|
| 7.5.1 | Pure C test client (raw socket HTTP sender) | `tests/integration/test_client.c` (new) | Connect, send request, read response, verify status code | 4h |
| 7.5.2 | HTTP protocol conformance tests | `tests/integration/test_http_protocol.c` (new) | GET/POST/PUT/DELETE; keep-alive; chunked encoding; correct headers | 4h |
| 7.5.3 | Malformed request tests | `tests/integration/test_malformed.c` (new) | Oversized headers → 431; invalid method → 501; missing Host → 400 | 3h |
| 7.5.4 | Concurrent connection tests | `tests/integration/test_concurrent.c` (new) | 100 simultaneous connections; all get correct responses; no crashes | 4h |
| 7.5.5 | CMake integration test target | `CMakeLists.txt`, `tests/integration/CMakeLists.txt` (new) | `make integration_tests` builds and runs all integration tests | 2h |

#### 7.6 Parser Hardening
| # | Task | File(s) | Acceptance Criteria | Est. |
|---|------|---------|-------------------|------|
| 7.6.1 | Detect duplicate `Transfer-Encoding` headers | `src/http_server.c` | Returns 400 on duplicate; test case added | 2h |
| 7.6.2 | Handle `Expect: 100-continue` | `src/http_server.c` | Send `100 Continue` response before reading body | 2h |
| 7.6.3 | Clamp chunked size to `MAX_BODY_BYTES` | `src/http_server.c` | `strtoul()` result checked against limit; returns 413 on overflow | 1h |
| 7.6.4 | Add request line length validation | `src/http_server.c` | URI > 8192 bytes → 414 (already exists, verify edge cases) | 1h |
| 7.6.5 | Edge-case unit tests (20+ cases) | `tests/test_weblib.c` | Empty body, zero-length chunks, trailing whitespace, null bytes | 4h |

---

### Phase 8 — Security & Observability (Summary)

| # | Task | Est. |
|---|------|------|
| 8.1.1–8.1.5 | SHA-256 implementation + NIST test vectors | 3d |
| 8.2.1–8.2.5 | AES-128/256-GCM implementation + NIST test vectors | 4d |
| 8.3.1–8.3.4 | RSA PKCS#1 v1.5 sign/verify + test vectors | 3d |
| 8.4.1–8.4.6 | TLS 1.2 record layer + handshake state machine | 5d |
| 8.5.1–8.5.3 | PEM certificate parser + key loading | 2d |
| 8.6.1–8.6.3 | `http_server_enable_tls()` API + integration | 2d |
| 8.7.1–8.7.4 | Logging middleware (levels, format, destination) | 2d |
| 8.8.1–8.8.3 | Error handler middleware (centralized 4xx/5xx) | 1d |
| 8.9.1–8.9.4 | CSRF middleware (token gen, validation, cookie) | 2d |
| 8.10.1–8.10.3 | Input validation helpers (length, charset, range) | 1d |

### Phase 9 — Performance & Protocol (Summary)

| # | Task | Est. |
|---|------|------|
| 9.1.1–9.1.6 | HTTP/2 binary framing + HPACK compression | 5d |
| 9.2.1–9.2.4 | HTTP/2 stream multiplexing + server push | 4d |
| 9.3.1–9.3.4 | Pure C DEFLATE/gzip compression | 4d |
| 9.4.1–9.4.4 | LRU cache with TTL | 3d |
| 9.5.1–9.5.4 | Async WebSocket event loop integration | 3d |
| 9.6.1–9.6.3 | Benchmarking suite + CI regression tracking | 3d |

### Phase 10 — Release Readiness (Summary)

| # | Task | Est. |
|---|------|------|
| 10.1.1–10.1.3 | REST API example + WebSocket chat example | 3d |
| 10.2.1–10.2.3 | Tutorial docs (getting started, REST API, real-time) | 3d |
| 10.3.1–10.3.3 | Windows IOCP event loop backend | 4d |
| 10.4.1–10.4.2 | BSD testing + CI matrix expansion | 2d |
| 10.5.1–10.5.3 | Health check endpoint + release automation | 2d |

---

## 9. Parallel Build Strategy

Tasks that share no data dependencies can be developed simultaneously:

```
PARALLEL GROUP A (Week 1-2):
├── 7.1 Socket Timeouts       ← touches http_server.c (connection accept path)
├── 7.4 GitHub Actions CI      ← touches .github/workflows/ only
└── 7.6 Parser Hardening       ← touches http_server.c (parse path, non-overlapping)

PARALLEL GROUP B (Week 2-3):
├── 7.2 Thread Pool            ← new files (thread_pool.c/h)
└── 7.5 Integration Tests      ← new files (tests/integration/)

SEQUENTIAL:
└── 7.3 Graceful Shutdown      ← depends on 7.1 (timeouts) + 7.2 (thread pool drain)

PARALLEL GROUP C (Week 5-7):
├── 8.1-8.3 Crypto Primitives  ← new files (src/tls/crypto_*)
├── 8.7 Logging Middleware      ← new file (src/middleware_log.c)
└── 8.8 Error Handler          ← new file (src/middleware_error.c)

PARALLEL GROUP D (Week 10-13):
├── 9.1-9.2 HTTP/2             ← new directory (src/http2/)
├── 9.3 Compression            ← new file (src/compression.c)
└── 9.4 Caching Layer          ← new file (src/cache.c)
```

---

## 10. Build Validation — Success Criteria per Module

### Phase 7 Checkpoints

| Module | Unit Test Gate | Integration Test Gate | Performance Gate |
|--------|---------------|----------------------|-----------------|
| Socket Timeouts | Slow-client simulation completes with timeout error | Server rejects Slowloris-style connections | Accept-to-timeout < 31s (default 30s + 1s tolerance) |
| Thread Pool | 100 work items submitted and completed; zero leaks | 100 concurrent connections served correctly | Pool creation < 1ms; work item latency < 100μs overhead |
| Graceful Shutdown | In-flight request completes before server exits | Integration test: send request during shutdown → 200 OK | Shutdown completes in < 1s with 50 idle connections |
| CI Pipeline | All 71+ tests pass on Linux + macOS | Integration test suite green | CI total time < 5 minutes |
| Integration Tests | N/A (these ARE the tests) | All protocol conformance tests pass | 100 concurrent connections, zero failures |
| Parser Hardening | 20+ edge-case tests pass | Malformed requests return correct 4xx codes | Parser throughput ≥ 50,000 req/s (single-threaded) |

### Phase 8 Checkpoints

| Module | Validation |
|--------|-----------|
| SHA-256 | NIST FIPS 180-4 test vectors pass (short msg, long msg, Monte Carlo) |
| AES-GCM | NIST SP 800-38D test vectors pass (128-bit + 256-bit keys) |
| RSA | PKCS#1 v1.5 sign/verify test vectors pass |
| TLS 1.2 | `curl --tlsv1.2 https://localhost:8443/` returns 200; browser connects |
| Logging | Log output matches format spec; level filtering works; file rotation |
| CSRF | Double-submit cookie validates; missing token → 403; replay → 403 |

### Phase 9 Checkpoints

| Module | Validation |
|--------|-----------|
| HTTP/2 | `curl --http2 http://localhost:8080/` returns 200; multiplexed streams |
| Compression | `Accept-Encoding: gzip` → compressed response; size < 50% of original |
| Cache | Cache hit returns in < 1μs; LRU eviction correct; TTL expiry correct |
| Async WebSocket | 1000 concurrent WebSocket connections; echo latency < 1ms |

---

## 11. QA Pipeline

### Automated Testing (Every Commit)

```
┌─────────────────────────────────────────────────────┐
│  GitHub Actions CI Pipeline                          │
│                                                      │
│  1. Build (Linux gcc + macOS clang)                  │
│     └── cmake -DCMAKE_C_FLAGS="-Werror" ..           │
│                                                      │
│  2. Unit Tests                                       │
│     └── ./tests/test_weblib  (all must pass)         │
│                                                      │
│  3. Integration Tests                                │
│     └── ./tests/integration/run_all  (protocol +     │
│         malformed + concurrent)                      │
│                                                      │
│  4. Memory Safety (Linux only)                       │
│     └── valgrind --leak-check=full --error-          │
│         exitcode=1 ./tests/test_weblib               │
│                                                      │
│  5. Static Analysis (optional)                       │
│     └── cppcheck --enable=all --error-exitcode=1     │
│                                                      │
│  6. Benchmark Regression (Phase 9+)                  │
│     └── Compare req/s against baseline ±10%          │
└─────────────────────────────────────────────────────┘
```

### Manual Testing Checkpoints (Per Release)

| # | Test | Method | Pass Criteria |
|---|------|--------|--------------|
| M1 | Slow client resistance | `slowhttptest -c 1000` against server | Zero crashes; all slow connections timeout |
| M2 | Large file upload | `curl -F "file=@100MB.bin" http://...` | Server accepts up to configured limit; rejects larger |
| M3 | Browser TLS handshake | Chrome/Firefox navigate to `https://localhost:8443` | Green lock icon; certificate details visible |
| M4 | Graceful shutdown under load | Send SIGTERM during `wrk` benchmark run | All in-flight requests complete; zero dropped |
| M5 | Memory under sustained load | Run 1M requests via `wrk`; monitor RSS | RSS stable (no unbounded growth); Valgrind clean |
| M6 | Cross-platform build | Build on Linux (gcc), macOS (clang), Windows (MSVC) | Zero errors, zero warnings on all platforms |

---

## 12. Security Review — Threat Model

### Assets Under Protection

| Asset | Location | Sensitivity |
|-------|----------|-------------|
| HTTP request data | `http_request_t` in memory | Contains credentials (cookies, auth headers) |
| Session store | `session_store_t` in-process memory | Session IDs = authentication tokens |
| TLS private keys | Loaded from PEM file at startup | Compromise = full traffic decryption |
| User file uploads | Temporary buffer in `body_parser_data_t` | User-supplied; potential malware |

### Threat Model (STRIDE)

| Threat | Category | Target | Mitigation |
|--------|----------|--------|-----------|
| **Slowloris / Slow POST** | Denial of Service | Connection pool | Socket timeouts (Phase 7.1); thread pool bounds (Phase 7.2) |
| **Request smuggling** | Tampering | HTTP parser | Duplicate header detection (Phase 7.6); strict parser mode |
| **Path traversal** | Information Disclosure | Static file middleware | Already mitigated (`../` prevention in `middleware_static.c`); add canonicalization |
| **Session hijacking** | Spoofing | Session cookies | `Secure` + `HttpOnly` + `SameSite=Strict` flags (already in v0.6.0); add CSRF tokens (Phase 8) |
| **Credential exposure** | Information Disclosure | HTTP transport | TLS encryption (Phase 8); HSTS header |
| **Buffer overflow** | Elevation of Privilege | All parsers | `MAX_HEADER_BYTES`, `MAX_BODY_BYTES` limits (already enforced); add fuzz testing (Phase 9) |
| **Timing attacks** | Information Disclosure | Auth middleware | Constant-time comparison for JWT signature verification (verify in Phase 8) |
| **Memory disclosure** | Information Disclosure | Error responses | Never include stack traces or internal paths in HTTP error bodies |
| **TLS downgrade** | Tampering | TLS handshake | Reject protocols < TLS 1.2; no SSLv3/TLS 1.0/1.1 (Phase 8) |
| **Private key theft** | Information Disclosure | TLS key material | Zero key material in logs; `mlock()` key pages; zero on free (Phase 8) |

### Vulnerability Checklist (Per-Phase Gate)

Each phase release MUST pass:

- [ ] No compiler warnings with `-Wall -Wextra -Werror -pedantic`
- [ ] Valgrind memcheck: zero errors, zero leaks (definite + indirect)
- [ ] No unbounded allocations from user input
- [ ] All `malloc()` return values checked
- [ ] All `sprintf()` replaced with `snprintf()` (bounded writes)
- [ ] No `strcpy()` — only `strncpy()` or manual bounds-checked copies
- [ ] Session IDs generated from `/dev/urandom` (or `CryptGenRandom` on Windows)
- [ ] TLS key material zeroed with `explicit_bzero()` / `SecureZeroMemory()` before `free()`
- [ ] All public APIs validate NULL pointers and return error codes
- [ ] No information leakage in error responses (status code only, no internals)

### Access Control Matrix

| Component | Read Access | Write Access | Notes |
|-----------|------------|-------------|-------|
| `http_request_t` fields | Route handlers, middleware | Parser only (immutable after parse) | Enforce via const pointers in API |
| Session data | `session_get_data()` only | `session_set_data()` only | Key-scoped; no bulk enumeration API |
| TLS private key | TLS handshake module only | Loaded once at startup | Never exposed via any API |
| Rate limit counters | Rate limit middleware | Rate limit middleware | IP-scoped; auto-expire |

---

## 13. Risk Mitigation Notes

| Risk | Probability | Impact | Mitigation Strategy |
|------|------------|--------|-------------------|
| Pure C TLS is insecure / buggy | HIGH | CRITICAL | Extensive test vector validation; optional compile-time flag to disable; security audit before v1.0 |
| HTTP/2 complexity causes regressions | MEDIUM | HIGH | Feature-flag behind `http_server_enable_http2()`; default off; comprehensive integration tests |
| Thread pool deadlock | LOW | HIGH | No nested locks; work items never wait on pool; timeout on condition variable wait |
| Windows platform divergence | MEDIUM | MEDIUM | Abstract platform APIs behind `src/platform.h`; CI matrix validates Windows build |
| Performance regression between versions | MEDIUM | MEDIUM | Benchmark suite in CI with ±10% tolerance gate |
| API breaking changes | LOW | HIGH | Semantic versioning; deprecated API kept for one major version |

---

## Revision History

| Version | Date | Changes |
|---------|------|---------|
| 1.0 | 2025-01-12 | Initial roadmap (Phases 4–6) |
| 2.0 | 2026-02-19 | Complete rewrite for Phases 7–10: first-principles design, adversarial review, atomic task breakdown, security threat model |

---

**Maintained by**: MCWL Core Team
**Last Updated**: 2026-02-20
**Status**: Active Development — Phase 10 next
**License**: MIT (see LICENSE file)

For questions or discussions about this roadmap, please open an issue on GitHub or contact the maintainers.
