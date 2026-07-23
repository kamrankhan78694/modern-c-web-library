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
    - Production-ready for real-time applications
  - **Frame Processing (Async Mode)** - Completed
    - Integration with event loop (epoll/kqueue/poll)
    - Non-blocking WebSocket I/O
    - Single-threaded high-concurrency support
    - Write queue for non-blocking sends
  
- [ ] 🎯 **SSL/TLS Support** - Secure HTTPS connections *(Phase 11–12, v1.1.0–v1.2.0)*
  - Pure C crypto primitives (SHA-256/384, AES-256-GCM, ChaCha20-Poly1305, X25519)
  - TLS 1.3 record layer and handshake state machine (RFC 8446)
  - X.509 certificate chain validation (DER/PEM parsing)
  - ALPN negotiation (h2 + http/1.1)
  - SNI support for virtual hosting
  - `http_server_enable_tls(server, cert_path, key_path)` API

- [ ] 🔧 **HTTP/2 Support** - Implement HTTP/2 protocol *(Phase 13, v1.3.0)*
  - Binary framing layer (RFC 7540, all 10 frame types)
  - HPACK header compression (RFC 7541, static + dynamic tables)
  - Stream multiplexing with priority and flow control
  - Server push (PUSH_PROMISE)
  - `http_server_enable_http2()` API
  - h2 (TLS) + h2c (cleartext) support

- [ ] 💡 **HTTP/3 / QUIC Support** - Next-generation HTTP protocol *(Phase 19, v1.9.0)*
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

- [ ] 🔧 **Directory Listing** - Auto-generate directory indexes *(Phase 15, v1.5.0)*
  - Configurable templates
  - File size formatting
  - Sorting options

### Template & View Engines

- [x] ✅ **Template Engine** - Server-side rendering
  - Variable substitution (`{{ variable }}` syntax)
  - Template file loading
  - Context-based rendering
  - HTTP response integration

- [ ] 💡 **Multiple Template Formats** - Support various template languages *(Phase 18, v1.8.0)*
  - Mustache templates (sections, partials, inheritance)
  - Auto-escaping (HTML/URL/JS context-aware)
  - Template includes and compiled template caching

### Data Storage

- [ ] 🔧 **SQLite Integration** - Lightweight embedded database *(see Phase 14 for custom storage engine)*
  - Direct SQLite C API usage (SQLite source code vendored/embedded, no external dependency)
  - Connection pooling
  - Transaction management
  - Query builder helpers

- [ ] 💡 **Custom File-Based Storage** - Simple data persistence *(Phase 14, v1.4.0)*
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

### Security — Phase 11 (Planned)

- [ ] 🎯 **SSL/TLS 1.2+ Support** - Pure C transport encryption
  - Custom TLS implementation (zero external dependencies)
  - AES-128/256-GCM cipher suites
  - RSA and ECDSA certificate support
  - PEM certificate/key parsing
  - SNI (Server Name Indication)
  - `http_server_enable_tls(server, cert_path, key_path)` API
  - TLS private key memory protection (`mlock()` + `secure_zero()`)

- [ ] 🎯 **Password Hashing** - Secure credential storage (pure C)
  - PBKDF2-HMAC-SHA256 (RFC 2898) with configurable iterations
  - `password_hash_create()` / `password_hash_verify()` API
  - Automatic salt generation via `secure_random_bytes()`
  - Timing-safe verification via `secure_compare()`
  - Tunable work factor for future-proofing

- [ ] 🎯 **Key Derivation Functions** - Derive keys from secrets
  - HKDF (RFC 5869) — extract-then-expand key derivation
  - PBKDF2 (RFC 2898) — password-based key derivation
  - Used internally by TLS and password hashing modules

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
  - Built-in fuzz testing harness for HTTP parser
  - Memory sanitizer (ASan/MSan) CI integration
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

- [ ] 🔧 **Load Balancing** - Distribute traffic *(Phase 16, v1.6.0)*
  - Multi-process master-worker model (fork-based)
  - SO_REUSEPORT per-worker accept
  - Worker supervision and auto-restart
  - Per-worker health monitoring

- [ ] 💡 **Worker Pool** - Process management *(Phase 16, v1.6.0)*
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

- [ ] 🔧 **Hot Reload** - Automatic server restart on code changes *(Phase 16, v1.6.0)*
- [ ] 🔧 **Debug Mode** - Enhanced debugging features *(Phase 18, v1.8.0)*
  - Verbose logging with request/response headers
  - Request/response inspection with per-middleware timing
  - Memory allocation tracking
  
- [ ] 💡 **CLI Tools** - Command-line utilities *(Phase 20, v2.0.0)*
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

- [ ] 🔧 **Tutorial Series** - Step-by-step guides *(Phase 20, v2.0.0)*
  - Getting started tutorial ✅ (completed in v1.0.0)
  - Building a REST API ✅ (completed in v1.0.0)
  - Real-time applications ✅ (completed in v1.0.0)
  - Production deployment
  - TLS/HTTPS setup *(Phase 12)*
  - Storage engine usage *(Phase 14)*

### Testing & Quality

- [x] ✅ **Networking Integration Tests** - Exercise live socket workflows
  - Automated sync request regression suite (GET, POST, JSON, 404, malformed)
  - Coverage for malformed input and sequential connections
  - Baseline concurrency smoke tests

- [ ] 🔧 **Comprehensive Test Suite** - Expand test coverage *(Phase 20, v2.0.0)*
  - Unit tests for all modules
  - Integration tests
  - Performance tests
  - Stress tests
  - Fuzz testing (HTTP, JSON, TLS, HPACK, QUIC parsers)

- [x] ✅ **Continuous Integration** - Automated testing
  - GitHub Actions CI (Linux GCC, Linux Clang, macOS Clang)
  - Valgrind memory check gate
  - Multi-platform build matrix

- [x] ✅ **Benchmarking Suite** - Performance benchmarks
  - Throughput tests (requests/sec)
  - Latency percentiles (p50/p95/p99)
  - Live server integration

### Cross-Platform

- [ ] 🔧 **Windows Improvements** - Better Windows support *(Phase 17, v1.7.0)*
  - IOCP event loop backend (`src/event_loop_iocp.c`)
  - MSVC build support (CMake generator)
  - Windows-specific CI runner
  - Platform abstraction layer (`src/platform.h`)

- [ ] 💡 **BSD Support** - Explicit BSD testing and support *(Phase 17, v1.7.0)*
  - FreeBSD CI testing
  - OpenBSD CI testing
  - NetBSD CI testing
  - Platform compatibility matrix documentation

### Monitoring & Observability

- [ ] 💡 **Prometheus Metrics** - Metrics export *(Phase 20, v2.0.0)*
  - Prometheus exposition format endpoint (`/metrics`)
  - HTTP counters, gauges, histograms
  - Custom metric labels
  - Per-worker metrics aggregation *(Phase 16)*

- [x] ✅ **Health Check Endpoint** - Service health monitoring
  - GET /healthz with JSON status and uptime
  - Suitable for load-balancer and Kubernetes probes

- [ ] 💡 **OpenTelemetry Support** - Distributed tracing *(Phase 20, v2.0.0)*
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

## Community Requests

This section will track feature requests from the community. Please open an issue to suggest new features!

---

## Next Phase Roadmap

For a detailed, phased implementation plan with timelines, priorities, and implementation guidance, see **[NEXT_PHASE.md](NEXT_PHASE.md)**.

### v1.0.0 Completed Phases

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
- **Phase 11 (v1.1.0)**: 🚧 Advanced Security — TLS 1.2+ (pure C) 🎯, password hashing (PBKDF2) 🎯, key derivation (HKDF) 🎯, request ID 🔧, IP allowlist/denylist 🔧, fuzz testing 💡

### v2.0.0 Planned Phases

- **Phase 11 (v1.1.0)**: 🎯 TLS Foundation — SHA-256/384, AES-256-GCM, ChaCha20-Poly1305, X25519 key exchange, HKDF key derivation
- **Phase 12 (v1.2.0)**: 🎯 TLS 1.3 Handshake & HTTPS — record layer, handshake state machine, certificate parsing, ALPN, `http_server_enable_tls()` API
- **Phase 13 (v1.3.0)**: 🎯 HTTP/2 Protocol — binary framing, HPACK compression, stream multiplexing, flow control, server push
- **Phase 14 (v1.4.0)**: 🔧 Persistent Storage Engine — B-tree key-value store, write-ahead log, transactions, crash recovery, iterator API
- **Phase 15 (v1.5.0)**: 🔧 Advanced Middleware & Content — directory listing, Server-Sent Events, content negotiation, route groups, regex routes
- **Phase 16 (v1.6.0)**: 🔧 Multi-Process Architecture — master-worker fork model, SO_REUSEPORT, zero-downtime reload, per-worker metrics
- **Phase 17 (v1.7.0)**: 🔧 Cross-Platform Hardening — platform abstraction layer, Windows IOCP, BSD testing, MSVC build, CI matrix expansion
- **Phase 18 (v1.8.0)**: 🔧 Developer Experience & Configuration — INI config parser, plugin architecture, advanced templates, debug mode, API versioning
- **Phase 19 (v1.9.0)**: 💡 HTTP/3 & QUIC — UDP transport, QUIC protocol (RFC 9000), connection migration, HTTP/3 framing, QPACK compression
- **Phase 20 (v2.0.0)**: 💡 Release Engineering & Ecosystem — CLI tools, Prometheus metrics, OpenTelemetry tracing, fuzz testing, v2.0.0 release

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

**Last Updated**: 2026-03-02  
**Maintainer**: [@kamrankhan78694](https://github.com/kamrankhan78694)
