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
  - **Frame Processing (Async Mode)** - TODO
    - Integration with event loop (epoll/kqueue/poll)
    - Non-blocking WebSocket I/O
    - Single-threaded high-concurrency support
  
- [ ] 🎯 **SSL/TLS Support** - Secure HTTPS connections
  - Custom TLS implementation in pure C
  - Certificate management
  - SNI support
  - TLS 1.2+ support

- [ ] 🔧 **HTTP/2 Support** - Implement HTTP/2 protocol
  - Binary framing
  - Stream multiplexing
  - Server push
  - Header compression (HPACK)

- [ ] 💡 **HTTP/3 / QUIC Support** - Next-generation HTTP protocol
  - UDP-based transport
  - Built-in TLS
  - Improved performance

### Request/Response Handling

- [ ] 🎯 **Complete HTTP Parser** - Fully parse and validate incoming requests
  - Method support beyond GET with explicit error responses
  - Header parsing, canonicalization, and lookup APIs
  - Content-Length and chunked body handling with size safeguards
  - Clear rejection of malformed or oversized payloads

- [ ] 🎯 **Header & Parameter Storage** - Back middleware and handlers with real data
  - Implement request header access and mutation
  - Persist route parameters for `/path/:id` patterns
  - Support response header setting and serialization

- [ ] 🎯 **Robust Connection Handling** - Hardening for sync and async servers
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

- [ ] 🔧 **Response Compression** - Reduce bandwidth usage
  - gzip compression
  - deflate compression
  - brotli compression
  - Automatic content negotiation

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

- [ ] 🔧 **Directory Listing** - Auto-generate directory indexes
  - Configurable templates
  - File size formatting
  - Sorting options

### Template & View Engines

- [x] ✅ **Template Engine** - Server-side rendering
  - Variable substitution (`{{ variable }}` syntax)
  - Template file loading
  - Context-based rendering
  - HTTP response integration

- [ ] 💡 **Multiple Template Formats** - Support various template languages
  - Mustache templates
  - Jinja2-style templates
  - Custom template syntax

### Data Storage

- [ ] 🔧 **SQLite Integration** - Lightweight embedded database
  - Direct SQLite C API usage (SQLite source code vendored/embedded, no external dependency)
  - Connection pooling
  - Transaction management
  - Query builder helpers

- [ ] 💡 **Custom File-Based Storage** - Simple data persistence
  - Key-value store implementation
  - Index structures
  - Transaction log
  - Query interface

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

### Server Lifecycle

- [x] ✅ **Graceful Shutdown & Thread Management** - Reliable server teardown
  - Close listening sockets before joining worker threads
  - Bounded thread pool with configurable worker count (default 16)
  - Server state machine: STOPPED → RUNNING → DRAINING → STOPPED
  - `http_server_shutdown()` API with drain timeout
  - Socket timeouts (`SO_RCVTIMEO`/`SO_SNDTIMEO`) with configurable values
  - `http_server_set_timeout()` and `http_server_set_thread_count()` APIs

### Performance

- [ ] 🔧 **Caching Layer** - Performance optimization
  - In-memory cache implementation
  - LRU eviction policy
  - Cache invalidation
  - TTL support

- [ ] 🔧 **Load Balancing** - Distribute traffic
  - Round-robin
  - Least connections
  - IP hash
  - Health checks

- [ ] 💡 **Worker Pool** - Process management
  - Multi-process model
  - Process supervision
  - Hot reload

### Middleware

- [x] ✅ **CORS Middleware** - Ready-to-use CORS handler
- [x] ✅ **Logging Middleware** - Request/response logging
- [x] ✅ **Body Parser Middleware** - Automatic body parsing
- [x] ✅ **Error Handler Middleware** - Centralized error handling
- [ ] 💡 **Metrics Middleware** - Request metrics collection

### Developer Experience

- [ ] 🔧 **Hot Reload** - Automatic server restart on code changes
- [ ] 🔧 **Debug Mode** - Enhanced debugging features
  - Verbose logging
  - Request/response inspection
  - Stack traces
  
- [ ] 💡 **CLI Tools** - Command-line utilities
  - Project scaffolding
  - Route listing
  - Configuration validator

### Documentation & Examples

- [x] ✅ **API Documentation** - Complete API reference
  - Function documentation
  - Parameter descriptions
  - Return value documentation
  - Usage examples

- [ ] 🔧 **More Examples** - Additional example applications
  - REST API example
  - WebSocket chat example
  - File upload example
  - Authentication example
  - Data persistence example

- [ ] 🔧 **Tutorial Series** - Step-by-step guides
  - Getting started tutorial
  - Building a REST API
  - Real-time applications
  - Production deployment

### Testing & Quality

- [x] ✅ **Networking Integration Tests** - Exercise live socket workflows
  - Automated sync request regression suite (GET, POST, JSON, 404, malformed)
  - Coverage for malformed input and sequential connections
  - Baseline concurrency smoke tests

- [ ] 🔧 **Comprehensive Test Suite** - Expand test coverage
  - Unit tests for all modules
  - Integration tests
  - Performance tests
  - Stress tests

- [x] ✅ **Continuous Integration** - Automated testing
  - GitHub Actions CI (Linux GCC, Linux Clang, macOS Clang)
  - Valgrind memory check gate
  - Multi-platform build matrix

- [ ] 💡 **Benchmarking Suite** - Performance benchmarks
  - Throughput tests
  - Latency tests
  - Comparison with other frameworks

### Cross-Platform

- [ ] 🔧 **Windows Improvements** - Better Windows support
  - IOCP support (Windows async I/O)
  - Native Windows builds
  - Windows-specific optimizations

- [ ] 💡 **BSD Support** - Explicit BSD testing and support
  - FreeBSD
  - OpenBSD
  - NetBSD

### Monitoring & Observability

- [ ] 💡 **Prometheus Metrics** - Metrics export
  - HTTP metrics
  - Custom metrics
  - Metric labels

- [x] ✅ **Health Check Endpoint** - Service health monitoring
  - GET /healthz with JSON status and uptime
  - Suitable for load-balancer and Kubernetes probes

- [ ] 💡 **OpenTelemetry Support** - Distributed tracing
  - Trace context propagation
  - Span creation
  - Exporter integration

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

## Community Requests

This section will track feature requests from the community. Please open an issue to suggest new features!

---

## Next Phase Roadmap

For a detailed, phased implementation plan with timelines, priorities, and implementation guidance, see **[NEXT_PHASE.md](NEXT_PHASE.md)**.

- **Phase 4 (v0.4.0)**: ✅ HTTP Foundation Hardening — parser, headers, connections, JSON arrays, graceful shutdown
- **Phase 5 (v0.5.0)**: ✅ Request Processing & Security — body parsing, cookies, CORS, rate limiting, static files
- **Phase 6 (v0.6.0)**: ✅ Production Readiness — sessions, template engine, auth middleware, db pooling, API docs
- **Phase 7 (v0.7.0)**: ✅ Server Hardening & CI — socket timeouts, thread pool, graceful shutdown, CI pipeline, integration tests, parser hardening
- **Phase 8 (v0.8.0)**: 🚧 Security & Observability — CSRF middleware ✅, logging ✅, error handler ✅, input validation ✅, health check ✅, pure C TLS 1.2 (planned)
- **Phase 9 (v0.9.0)**: Performance & Protocol — HTTP/2, response compression, caching layer, async WebSocket, benchmarks
- **Phase 10 (v1.0.0)**: Release Readiness — tutorials, examples, Windows IOCP, BSD testing, release automation

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

**Last Updated**: 2026-02-19  
**Maintainer**: [@kamrankhan78694](https://github.com/kamrankhan78694)
