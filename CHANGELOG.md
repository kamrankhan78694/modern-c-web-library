# Changelog

All notable changes to the Modern C Web Library will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Planned — v2.0.0 Roadmap (Phases 11–20)

The following phases are planned for the v2.0.0 release cycle. Each phase will be documented here upon completion.

- **Phase 11 (v1.1.0)**: Memory Architecture Revolution — Arena/slab allocator, object pools, cache-line alignment
- **Phase 12 (v1.2.0)**: io_uring & Zero-Copy I/O — io_uring backend, registered buffers, sendfile(), 1M+ RPS target
- **Phase 13 (v1.3.0)**: SIMD-Accelerated HTTP Parser — SSE4.2/AVX2/NEON vectorized parsing
- **Phase 14 (v1.4.0)**: Pure C TLS 1.3 — RFC 8446, AES-NI/ARM-CE, X25519, certificate parsing
- **Phase 15 (v1.5.0)**: HTTP/2 Protocol Engine — Binary framing, HPACK, stream multiplexing
- **Phase 16 (v1.6.0)**: AI Inference Serving Primitives — SSE streaming, batch coalescing, tensor protocol
- **Phase 17 (v1.7.0)**: Lock-Free Concurrency — MPMC queue, work-stealing scheduler, RCU routing
- **Phase 18 (v1.8.0)**: AI Agent Orchestration — JSON-RPC 2.0, agent routing, tool calling protocol
- **Phase 19 (v1.9.0)**: Observability & Profiling — Prometheus, OpenTelemetry, structured logging, profiler
- **Phase 20 (v2.0.0)**: World's Fastest AI-Native C Web Library — TechEmpower benchmarks, release

## [1.0.0] - 2026-02-22

### Added
- **Phase 10: Release Readiness (v1.0.0)**
- **REST API example** (`examples/rest_api_server.c`) — full CRUD operations with input validation, JSON responses, and production middleware (logging, CORS, rate limiting, error handling, health check, metrics)
- **Tutorial documentation** (`docs/tutorials/`) — step-by-step guides:
  - Getting Started tutorial
  - Building a REST API tutorial
  - Real-time WebSocket Applications tutorial
- **Complete API reference** (`docs/api/README.md`) — updated to v1.0.0 with all Phase 7-9 APIs
- Updated `TODO.md` — marked HTTP parser, header/parameter storage, and connection handling as complete
- Updated `docs/TECHNICAL_DEBT.md` — resolved stale entries for keep-alive (#6) and compression (#10)

### Changed
- Version bump from 0.9.0 to 1.0.0
- Updated all documentation to reflect v1.0.0 release status
- Updated `ACHIEVEMENTS.md` with current test counts (129 tests) and complete feature list
- Updated `NEXT_PHASE.md` — all phases marked as complete
- Updated `README.md` — comprehensive project structure, features list, and roadmap

## [0.9.0] - 2026-02-20

### Added
- **Phase 9: Performance & Observability**
- **In-Memory Cache** — LRU eviction, TTL support, thread-safe hash table (`src/cache.c`)
- **Metrics Middleware** — request counting, per-method tracking, status code ranges, JSON `/metrics` endpoint (`src/middleware_metrics.c`)
- **Response Compression** — pure C gzip (RFC 1952) with DEFLATE (RFC 1951), `Accept-Encoding` negotiation (`src/compression.c`)
- **Async WebSocket** — event loop integration, non-blocking I/O, write queue, connection manager (`src/async_websocket.c`)
- **Benchmarking Suite** — high-resolution timing, throughput/latency measurement, percentile statistics (`src/benchmark.c`)
- 20 new unit tests for Phase 9 features

## [0.8.0] - 2026-02-19

### Added
- **Phase 8: Security & Observability**
- **CSRF Middleware** — double-submit cookie pattern with constant-time comparison (`src/middleware_csrf.c`)
- **Logging Middleware** — configurable log levels (DEBUG/INFO/WARN/ERROR), timestamp format (`src/middleware_log.c`)
- **Error Handler Middleware** — centralized 4xx/5xx JSON error responses (`src/middleware_error.c`)
- **Input Validation** — length, charset, integer range, email format validation, HTML sanitization (`src/input_validation.c`)
- **Health Check Endpoint** — `GET /healthz` with JSON status and uptime (`src/health_check.c`)
- 12 new unit tests for Phase 8 features

## [0.7.0] - 2026-02-18

### Added
- **Phase 7: Server Hardening & CI**
- **Socket Timeouts** — `setsockopt(SO_RCVTIMEO/SO_SNDTIMEO)` with `http_server_set_timeout()` API
- **Thread Pool** — bounded thread pool replacing thread-per-connection model (`src/thread_pool.c`)
- **Graceful Shutdown** — server state machine (STOPPED → RUNNING → DRAINING → STOPPED), `http_server_shutdown()` API
- **GitHub Actions CI** — Linux (GCC + Clang) and macOS (Clang) matrix with Valgrind memcheck gate
- **Networking Integration Tests** — raw-socket HTTP client, GET/POST/404/malformed/concurrent tests
- **Parser Hardening** — duplicate Transfer-Encoding detection, `Expect: 100-continue` handling

## [0.6.0] - 2026-02-17

### Added
- **Phase 6: Production Readiness (v0.6.0)**
- **Session Management** (Phase 6.2) - Server-side session store
  - `session_store_create()` / `session_store_destroy()` - Session store lifecycle
  - `session_create()` - Create session with configurable max_age
  - `session_get()` - Retrieve session by ID with expiration check
  - `session_destroy()` - Remove session from store
  - `session_set_data()` / `session_get_data()` / `session_remove_data()` - Key-value data storage
  - `session_get_id()` - Get session identifier
  - `session_is_expired()` - Check session expiration
  - `session_cleanup_expired()` - Remove expired sessions
  - `session_from_request()` - Extract session from request cookie
  - `session_set_cookie()` - Set session cookie on response
  - Cookie-based session transport with HttpOnly and SameSite=Lax
  - Resolves PR #13 merge conflicts
- **Template Engine** (Phase 6.3) - Dynamic HTML generation
  - `template_context_create()` / `template_context_destroy()` - Context lifecycle
  - `template_context_set()` / `template_context_get()` - Variable management
  - `template_render()` - Render templates with `{{ variable }}` syntax
  - `template_load_file()` - Load templates from files
  - `http_response_send_template()` - Send rendered template as response
  - Hash map storage (256 buckets) for O(1) variable lookups
  - Resolves PR #15 merge conflicts
- **Authentication Middleware** (Phase 6.4) - Pluggable auth
  - `basic_auth_middleware_create()` / `basic_auth_middleware_destroy()` - HTTP Basic Auth
  - `apikey_auth_middleware_create()` / `apikey_auth_middleware_destroy()` - API Key validation
  - `jwt_auth_middleware_create()` / `jwt_auth_middleware_destroy()` - JWT (HMAC-SHA256)
  - Pure C SHA-256 implementation (FIPS 180-4)
  - Pure C HMAC-SHA256 (RFC 2104)
  - Pure C Base64/Base64URL decode
  - Constant-time signature comparison for timing attack prevention
  - New types: `basic_auth_config_t`, `apikey_auth_config_t`, `jwt_auth_config_t`
- **Database Connection Pool** (Phase 6.5) - Thread-safe pooling
  - `db_pool_create()` / `db_pool_destroy()` - Pool lifecycle
  - `db_pool_acquire()` / `db_pool_release()` - Connection management
  - `db_pool_get_stats()` - Pool statistics
  - `db_pool_close_idle()` - Close idle connections
  - Configurable min/max connections, timeouts, and validation
  - Pluggable backend callbacks for custom database types
  - New header: `include/db_pool.h`
  - Resolves PR #17 merge conflicts
- **API Documentation** (Phase 6.6) - `docs/api/README.md` comprehensive reference
- **17 new unit tests** for Phase 6 features (60/60 total passing)
- **Resolved merge conflicts** from PRs #13, #15, #17
- **Request Body Parsing** (Phase 5.1) - Parse HTTP request bodies
  - `http_request_parse_body()` - Auto-detect and parse body based on Content-Type
  - `http_request_get_form_field()` - Get URL-encoded or multipart form field value
  - `http_request_get_file()` - Get uploaded file from multipart form data
  - `body_parser_data_free()` - Free body parser resources
  - URL-encoded form data parsing with percent-decoding
  - Multipart form data parsing (RFC 7578) with boundary detection
  - File upload handling with size limits and filename sanitization
  - New types: `http_uploaded_file_t`, `http_form_field_t`, `body_parser_data_t`
- **Cookie Handling** (Phase 5.2) - RFC 6265 cookie support
  - `http_request_get_cookie()` - Parse and retrieve cookies from request
  - `http_response_set_cookie()` - Set cookies with full attribute support
  - `http_response_delete_cookie()` - Delete cookies via Max-Age=0
  - New type: `cookie_options_t` with Domain, Path, Max-Age, Secure, HttpOnly, SameSite
- **CORS Middleware** (Phase 5.3) - Cross-Origin Resource Sharing
  - `cors_middleware_create()` - Create configurable CORS middleware
  - `cors_middleware_destroy()` - Free CORS middleware resources
  - Preflight OPTIONS request handling with 204 No Content
  - Configurable allowed origins, methods, headers, credentials, max-age
  - New type: `cors_options_t`
- **Rate Limiting Middleware** (Phase 5.4) - IP-based rate limiting
  - `ratelimit_middleware_create()` - Create rate limiter with token bucket algorithm
  - `ratelimit_middleware_destroy()` - Free rate limiter resources
  - IP-based tracking via hash table with automatic cleanup
  - Rate limit headers: X-RateLimit-Limit, X-RateLimit-Remaining, X-RateLimit-Reset
  - 429 Too Many Requests response with Retry-After header
  - New type: `ratelimit_config_t`
- **Static File Serving** (Phase 5.5) - Efficient static asset delivery
  - `static_file_middleware_create()` - Create static file middleware
  - `static_file_middleware_destroy()` - Free static file middleware resources
  - MIME type detection for 17 common file types
  - Path traversal prevention via realpath() validation
  - ETag generation and conditional request support (304 Not Modified)
  - Cache-Control and Last-Modified headers
  - New type: `static_file_config_t`
- **New HTTP Status Codes** - `HTTP_NOT_MODIFIED` (304) and `HTTP_TOO_MANY_REQUESTS` (429)
- **15 new unit tests** for Phase 5 features (43/43 total passing)
- **Complete JSON Array Support** (Phase 4.4) - Full array parsing, serialization, and manipulation
  - `json_array_create()` - Create empty JSON array
  - `json_array_append()` - Append element to JSON array
  - `json_array_get()` - Get element by index
  - `json_array_length()` - Get number of elements
  - Full array parsing from JSON strings (nested arrays, mixed types)
  - Full array serialization via `json_stringify()`
  - 7 new unit tests (28/28 total passing)
- **`HTTP_SWITCHING_PROTOCOLS` enum value** - Added 101 status to `http_status_t` enum
- **Next Phase Roadmap** (NEXT_PHASE.md) - Detailed phased plan for v0.4.0–v0.6.0

### Fixed
- Compiler warning for `case 101` not in enumerated type `http_status_t`
- Replaced all magic number `101` references with `HTTP_SWITCHING_PROTOCOLS` enum value

### Changed
- Updated TODO.md with cross-references to NEXT_PHASE.md roadmap

## [0.3.0] - 2025-11-14

### Added
- **WebSocket frame processing in threaded mode** - Production-ready real-time bidirectional communication
  - Automatic ping/pong handling with <0.001s latency
  - Text and binary message echo support
  - Multiple concurrent WebSocket connections
  - Graceful connection close with status codes (1000-1015)
  - Persistent connections after HTTP upgrade
- Socket fd exposure to route handlers via `http_request_t.socket_fd`
- `handle_websocket_connection()` function for WebSocket frame processing loop
- Comprehensive WebSocket test suite (test_ping.py, test_handshake.py, test_basic_ws.py)
- Complete CHANGELOG.md following Keep a Changelog format

### Fixed
- WebSocket handshake response not being sent (removed `res->sent = true` in `websocket_handle_upgrade`)
- HTTP status 101 returning "OK" instead of "Switching Protocols"
- Connection header being overridden for WebSocket upgrade responses
- Example server not maintaining connections after WebSocket upgrade
- Server threading issue causing immediate shutdown after handshake

### Changed
- Updated `websocket_echo_server.c` example with working frame processing
- Simplified WebSocket connection management (removed manual tracking)
- Improved signal handling in example server
- Enhanced documentation with production-ready status indicators

## [0.2.0] - 2025-01-12

### Added
- Complete RFC 6455 WebSocket protocol implementation
  - WebSocket handshake (SHA-1 + Base64 accept key generation)
  - Frame encoding/decoding with masking/unmasking
  - Text and binary messages
  - Control frames (ping, pong, close)
  - Message fragmentation and reassembly
  - Close frames with status codes (1000-1015)
- WebSocket API with 14 functions in public header
- `websocket_echo_server.c` example with interactive HTML client
- Comprehensive WebSocket documentation (docs/WEBSOCKET.md)
- 3 WebSocket unit tests (21/21 tests passing)

### Documentation
- Added `WEBSOCKET.md` with complete usage guide (527 lines)
- Updated README with WebSocket usage examples
- Updated TODO.md marking WebSocket support as completed

## [0.1.0] - 2024-12-XX

### Added
- HTTP server with threaded and async I/O modes
- Event loop with epoll (Linux), kqueue (macOS/BSD), and poll (fallback)
- Flexible routing system with parameter support (`/users/:id`)
- Middleware chain support
- JSON parser and serializer
- Cross-platform build system (CMake)
- Example servers (simple_server, async_server)
- Basic unit test infrastructure
- Docker support with multi-stage builds
- Comprehensive documentation

### Core Features
- Pure C implementation with zero external dependencies
- ISO C99/C11 compliance
- Platform support: Linux, macOS, Windows
- Connection limits: 128 connections, 256 routes, 32 middlewares
- Buffer size: 8KB read buffer

---

## Release Notes

### v1.0.0 — Production Release (Current)

This release marks the first production-ready version of the Modern C Web Library.
All planned phases (4-10) are complete with comprehensive documentation and tutorials.

**Highlights:**
- ✅ 129 unit tests passing with 100% success rate
- ✅ 25 source modules covering HTTP, WebSocket, JSON, middleware, and more
- ✅ 5 example servers including REST API and WebSocket echo
- ✅ Complete tutorial documentation (Getting Started, REST API, WebSocket)
- ✅ Comprehensive API reference for all modules
- ✅ GitHub Actions CI with Linux (GCC, Clang) and macOS (Clang)
- ✅ Zero compiler warnings, Valgrind-clean

**Architecture:**
- Threaded mode: Bounded thread pool with configurable worker count
- Async mode: Event loop with epoll/kqueue/poll backends
- Both modes are production-ready for deployment

---

## Version History

- **2.0.x**: AI-Native Performance — the world's fastest pure C web library for AI workloads
- **1.1.x–1.9.x**: Distributive Innovation Phases — memory architecture, io_uring, SIMD, TLS 1.3, HTTP/2, AI inference, lock-free concurrency, agent orchestration, observability
- **1.0.x**: Production release — all phases complete, tutorials, full documentation
- **0.9.x**: Performance & Observability — compression, caching, metrics, async WebSocket, benchmarking
- **0.8.x**: Security & Observability — CSRF, logging, error handler, input validation, health check
- **0.7.x**: Server Hardening & CI — timeouts, thread pool, graceful shutdown, CI, integration tests
- **0.6.x**: Production Readiness — sessions, templates, auth, DB pooling, API docs
- **0.5.x**: Request Processing & Security — body parsing, cookies, CORS, rate limiting, static files
- **0.4.x**: HTTP Foundation Hardening — parser, headers, JSON arrays, connections
- **0.3.x**: WebSocket frame processing (Production-ready threaded mode)
- **0.2.x**: WebSocket support (RFC 6455 compliant)
- **0.1.x**: Initial HTTP server implementation with event loop

[Unreleased]: https://github.com/kamrankhan78694/modern-c-web-library/compare/v1.0.0...HEAD
[1.0.0]: https://github.com/kamrankhan78694/modern-c-web-library/compare/v0.9.0...v1.0.0
[0.9.0]: https://github.com/kamrankhan78694/modern-c-web-library/compare/v0.8.0...v0.9.0
[0.8.0]: https://github.com/kamrankhan78694/modern-c-web-library/compare/v0.7.0...v0.8.0
[0.7.0]: https://github.com/kamrankhan78694/modern-c-web-library/compare/v0.6.0...v0.7.0
[0.6.0]: https://github.com/kamrankhan78694/modern-c-web-library/compare/v0.3.0...v0.6.0
[0.3.0]: https://github.com/kamrankhan78694/modern-c-web-library/releases/tag/v0.3.0
[0.2.0]: https://github.com/kamrankhan78694/modern-c-web-library/releases/tag/v0.2.0
[0.1.0]: https://github.com/kamrankhan78694/modern-c-web-library/releases/tag/v0.1.0
