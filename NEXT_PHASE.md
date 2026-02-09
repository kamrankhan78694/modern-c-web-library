# Next Phase Roadmap - Modern C Web Library v0.4.0+

## Overview

The Modern C Web Library (MCWL) has successfully reached **v0.3.0** with a solid foundation:
- Production-ready HTTP server with threaded and async I/O modes
- Cross-platform event loop (epoll/kqueue/poll)
- Advanced routing with path parameters
- Middleware chain architecture
- JSON parser/serializer
- RFC 6455 compliant WebSocket support (threaded mode)
- Comprehensive test suite (21/21 passing)

**Where we're headed**: The next three releases (v0.4.0, v0.5.0, v0.6.0) will transform MCWL from a capable web framework into a **production-grade web server platform** with enterprise features, robust error handling, and security hardening — all while maintaining our commitment to **pure C with zero external dependencies**.

---

## Phase 4: HTTP Foundation Hardening (v0.4.0 Target)

**Goal**: Strengthen the HTTP protocol implementation and core server reliability. These are foundational improvements that unlock all subsequent features.

### 4.1 Complete HTTP Parser

**Why First**: A robust parser is the gatekeeper for all incoming requests. Without proper method validation, header parsing, and malformed request rejection, the server is vulnerable to crashes and security issues.

**Implementation**:
- Extend HTTP method support beyond GET/POST (PUT, DELETE, PATCH, OPTIONS, HEAD, TRACE)
- Implement RFC-compliant header parsing with multi-line header support
- Chunked transfer encoding (`Transfer-Encoding: chunked`) for request bodies
- Malformed request detection and rejection with appropriate 4xx responses
- Request line parsing hardening (validate HTTP version, URI length limits)

**Files Affected**:
- `src/http_server.c` - Core parsing logic
- `include/weblib.h` - New `http_method_t` enum, `http_header_t` struct
- `tests/test_weblib.c` - Parser edge case tests

**Acceptance Criteria**:
- Parse all standard HTTP methods
- Handle headers up to 8KB total size
- Reject invalid requests with 400 Bad Request
- Support chunked request bodies up to configurable limit

---

### 4.2 Header & Parameter Storage

**Why First**: Current implementation lacks persistent storage for parsed headers and route parameters. This forces middleware and handlers to re-parse data or lose context.

**Implementation**:
- Request header storage: dynamic key-value map accessible via `http_request_get_header(req, "Content-Type")`
- Header mutation API: `http_request_set_header()`, `http_request_remove_header()`
- Route parameter persistence: store `:param` values in request context
- Response header management: `http_response_set_header()`, `http_response_add_header()`
- Header serialization for HTTP response formatting

**Files Affected**:
- `src/http_server.c` - Header storage in `http_request_t` / `http_response_t`
- `include/weblib.h` - New header management APIs
- `src/router.c` - Route parameter extraction and storage
- `tests/test_weblib.c` - Header CRUD tests

**Data Structures**:
```c
typedef struct {
    char *key;
    char *value;
} http_header_t;

// In http_request_t / http_response_t:
http_header_t *headers;
size_t header_count;
size_t header_capacity;
```

---

### 4.3 Robust Connection Handling

**Why First**: Current implementation may not fully drain socket buffers or properly implement HTTP/1.1 keep-alive, leading to connection leaks and client timeouts.

**Implementation**:
- Looping read/write operations with partial I/O handling
- HTTP/1.1 persistent connections (`Connection: keep-alive` support)
- Proper connection state machine (idle → reading → processing → writing → closed)
- Deterministic socket teardown (graceful close with `SO_LINGER`)
- Timeout handling for slow clients (read/write timeouts)

**Files Affected**:
- `src/http_server.c` - Connection lifecycle management
- `src/event_loop.c` - Timeout tracking integration
- `include/weblib.h` - Connection state enums

**Key Improvements**:
- Loop on `recv()` until full request received or timeout
- Loop on `send()` until full response transmitted
- Respect `Connection: close` header
- Default to keep-alive for HTTP/1.1 clients

---

### 4.4 Complete JSON Support

**Why First**: Current JSON parser lacks array support and has edge cases in string escaping. This blocks advanced API responses and request body parsing.

**Implementation**:
- JSON array parsing: `json_array_get(json, index)`, `json_array_length(json)`
- Array serialization: `json_array_create()`, `json_array_append()`
- Unicode escape sequence handling (`\uXXXX`)
- Standard escape sequences (`\"`, `\\`, `\/`, `\b`, `\f`, `\n`, `\r`, `\t`)
- Number parsing hardening (exponent notation, overflow detection)
- Nested object/array support (recursive descent parser)

**Files Affected**:
- `src/json.c` - Parser/serializer enhancements
- `include/weblib.h` - Array API functions
- `tests/test_weblib.c` - JSON edge case tests (nested arrays, Unicode, large numbers)

**API Extensions**:
```c
json_value_t* json_array_create(void);
json_value_t* json_array_get(json_value_t *arr, size_t index);
bool json_array_append(json_value_t *arr, json_value_t *val);
size_t json_array_length(json_value_t *arr);
```

---

### 4.5 Graceful Shutdown & Thread Management

**Why First**: Production servers must shutdown cleanly without dropping in-flight requests or leaking resources.

**Implementation**:
- Shutdown signal handling (SIGTERM, SIGINT on POSIX; Ctrl+C handler on Windows)
- Close listening socket to stop accepting new connections
- Drain existing connections (wait for handlers to complete with timeout)
- Thread pool shutdown: signal worker threads, join all threads
- Platform-specific guards for Windows vs. POSIX thread APIs
- Server state machine (running → draining → stopped)

**Files Affected**:
- `src/http_server.c` - Shutdown orchestration
- `include/weblib.h` - `http_server_shutdown()` API
- `tests/test_weblib.c` - Shutdown test with in-flight request

**Implementation Notes**:
- Use `sig_atomic_t` volatile flag for signal safety
- Set socket option `SO_REUSEADDR` to allow quick restarts
- Timeout for draining phase (default 30 seconds)

---

### 4.6 Networking Integration Tests

**Why First**: Unit tests alone don't catch integration issues like race conditions, protocol violations, or resource leaks under load.

**Implementation**:
- Automated HTTP request/response tests (via `curl` or raw sockets)
- Malformed input test suite (invalid methods, oversized headers, bad chunking)
- Concurrent connection tests (100+ simultaneous clients)
- Keep-alive and pipelining tests
- Load test smoke test (1000 req/sec for 10 seconds)
- Memory leak detection (Valgrind integration in CI)

**Files Affected**:
- `tests/integration/` (new directory)
  - `test_http_protocol.c`
  - `test_malformed_requests.c`
  - `test_concurrent_clients.c`
- `CMakeLists.txt` - Integration test target
- `.github/workflows/` - CI integration

**Test Framework**:
- Pure C test client (no external dependencies)
- Fork/spawn test server before each test suite
- Validate response codes, headers, body content

---

## Phase 5: Request Processing & Security (v0.5.0 Target)

**Goal**: Enable rich request handling and implement security layers essential for production APIs.

### 5.1 Request Body Parsing

**Description**: Parse and expose structured request bodies to handlers.

**Features**:
- `application/x-www-form-urlencoded` parsing → key-value map
- `multipart/form-data` parsing (RFC 7578) with boundary detection
- File upload handling: stream to disk, size limits, filename sanitization
- Streaming body API for large payloads
- Body size limits (configurable per-route)

**Files Affected**:
- `src/http_server.c` - Body parser dispatcher
- `src/body_parser.c` (new) - Encoding-specific parsers
- `include/weblib.h` - `http_request_get_form_field()`, `http_request_get_file()`

**API Example**:
```c
const char *username = http_request_get_form_field(req, "username");
http_uploaded_file_t *avatar = http_request_get_file(req, "avatar");
```

---

### 5.2 Cookie Handling

**Description**: Full RFC 6265 cookie support for session management.

**Features**:
- Request cookie parsing: `http_request_get_cookie(req, "session_id")`
- Response cookie setting: `http_response_set_cookie(res, name, value, options)`
- Cookie attributes: `Secure`, `HttpOnly`, `SameSite`, `Domain`, `Path`, `Max-Age`, `Expires`
- Cookie deletion helper: `http_response_delete_cookie(res, name)`

**Files Affected**:
- `src/http_server.c` - Cookie parsing/serialization
- `include/weblib.h` - Cookie API and `cookie_options_t` struct

---

### 5.3 CORS Support

**Description**: Middleware for Cross-Origin Resource Sharing.

**Features**:
- Configurable allowed origins (whitelist, wildcards)
- Preflight request handling (OPTIONS method)
- CORS headers: `Access-Control-Allow-Origin`, `Access-Control-Allow-Methods`, etc.
- Credentials support (`Access-Control-Allow-Credentials`)
- Per-route CORS configuration override

**Files Affected**:
- `src/middleware_cors.c` (new)
- `include/weblib.h` - `cors_middleware_create()`, `cors_options_t`

**Usage**:
```c
cors_options_t cors = {
    .allowed_origins = (const char*[]){"https://app.example.com", NULL},
    .allow_credentials = true
};
http_server_use(server, cors_middleware_create(&cors));
```

---

### 5.4 Rate Limiting

**Description**: Protect APIs from abuse and DoS attacks.

**Features**:
- IP-based rate limiting (extract from socket peer address)
- Token bucket algorithm (constant rate with burst allowance)
- Sliding window log algorithm (precise but memory-intensive)
- Per-route rate limit configuration
- Response headers: `X-RateLimit-Limit`, `X-RateLimit-Remaining`, `X-RateLimit-Reset`
- 429 Too Many Requests response

**Files Affected**:
- `src/middleware_ratelimit.c` (new)
- `include/weblib.h` - `ratelimit_middleware_create()`, `ratelimit_config_t`

---

### 5.5 Static File Serving

**Description**: Efficient static asset delivery (HTML, CSS, JS, images).

**Features**:
- MIME type detection (by file extension)
- `Accept-Ranges` and `Range` request support (byte ranges)
- `ETag` generation (hash of file content or mtime)
- Conditional requests: `If-None-Match`, `If-Modified-Since` → 304 Not Modified
- Cache control headers: `Cache-Control`, `Expires`
- Security: path traversal prevention (`../` attacks)

**Files Affected**:
- `src/middleware_static.c` (new)
- `include/weblib.h` - `static_file_middleware_create(root_dir)`

**API Example**:
```c
http_server_use(server, static_file_middleware_create("./public"));
```

---

## Phase 6: Production Readiness (v0.6.0 Target)

**Goal**: Enterprise-grade features for secure, observable, and maintainable deployments.

### 6.1 SSL/TLS Support (Custom Pure C Implementation)

**Description**: HTTPS support without external dependencies (no OpenSSL).

**Features**:
- TLS 1.2 and TLS 1.3 support
- Certificate parsing (X.509 PEM format)
- RSA and ECDSA key support
- Handshake state machine (ClientHello → ServerHello → Finished)
- Cipher suites: `AES-GCM`, `ChaCha20-Poly1305`
- Certificate verification (chain validation, hostname matching)

**Files Affected**:
- `src/tls/` (new directory)
  - `tls_context.c` - TLS session management
  - `tls_handshake.c` - Handshake protocol
  - `crypto_aes.c` - AES block cipher
  - `crypto_sha.c` - SHA-256/384 hash
  - `crypto_rsa.c` - RSA signature/encryption
- `include/weblib.h` - `http_server_enable_tls(server, cert_path, key_path)`

**Note**: This is a **major** undertaking. Estimated 3-4 weeks of focused development. Alternative: Phase 6.1a could be "OpenSSL Integration" as a pragmatic first step, with pure C TLS in v0.7.0.

---

### 6.2 Session Management

**Description**: Server-side session storage with cookie-based session IDs.

**Features**:
- Session store: in-memory hash map (or pluggable backend interface)
- Session creation: generate cryptographically secure session ID
- Session middleware: automatically load session from cookie
- Session API: `http_request_session_set()`, `http_request_session_get()`
- Session expiration and cleanup (background thread or lazy eviction)

**Files Affected**:
- `src/session.c` (new)
- `include/weblib.h` - Session API

---

### 6.3 Authentication Middleware

**Description**: Pre-built authentication strategies.

**Features**:
- **Basic Auth**: Parse `Authorization: Basic` header
- **JWT**: Parse and verify JSON Web Tokens (HMAC-SHA256 signature)
- **API Key**: Validate `X-API-Key` header against whitelist
- Pluggable authentication callback for custom logic

**Files Affected**:
- `src/middleware_auth.c` (new)
- `include/weblib.h` - `basic_auth_middleware()`, `jwt_auth_middleware()`, `apikey_auth_middleware()`

---

### 6.4 Async WebSocket Mode

**Description**: Integrate WebSocket into event loop (non-blocking mode).

**Features**:
- WebSocket frame parsing in async I/O callbacks
- Non-blocking sends (queue frames if socket not writable)
- Integration with existing event loop (epoll/kqueue)
- Message fragmentation handling
- Control frame handling (ping/pong, close) in async context

**Files Affected**:
- `src/websocket.c` - Async mode implementation
- `src/event_loop.c` - WebSocket event registration
- `include/weblib.h` - `ws_connection_set_async(conn, true)`

---

### 6.5 API Documentation

**Description**: Comprehensive developer documentation.

**Deliverables**:
- Function documentation in `include/weblib.h` (Doxygen-compatible comments)
- Auto-generated HTML docs via Doxygen
- API reference guide (markdown in `docs/api/`)
- Tutorial: "Building Your First REST API" (step-by-step guide)
- Architecture document (event loop, threading model, memory management)

**Files Affected**:
- `include/weblib.h` - Add `/** @brief ... */` comments
- `docs/api/` (new directory)
- `Doxyfile` (new) - Doxygen configuration
- `README.md` - Link to docs

---

### 6.6 Comprehensive Test Suite Expansion

**Description**: Achieve >90% code coverage.

**Additions**:
- Edge case tests for all new features
- Fuzz testing for parsers (HTTP, JSON, WebSocket, TLS)
- Performance regression tests (benchmark suite)
- Cross-platform tests (Linux, macOS, Windows CI)
- Stress tests (high concurrency, memory pressure)

**Files Affected**:
- `tests/test_weblib.c` - Expand unit tests
- `tests/fuzz/` (new) - Fuzzing harnesses
- `tests/benchmark/` (new) - Performance tests

---

## Implementation Guidelines

All development must adhere to the following principles:

### Language & Dependencies
- **Pure C** (C99 minimum, C11 features allowed)
- **Zero external libraries** (except standard C library and platform APIs: POSIX, Win32, BSD sockets)
- Platform abstractions via `#ifdef _WIN32` / `#ifdef __linux__` / etc.

### Code Organization
- All public APIs declared in `include/weblib.h`
- Internal APIs in `src/*.h` (not installed)
- Implementation in `src/*.c`
- Tests in `tests/test_weblib.c` and `tests/integration/*.c`

### Naming Conventions
- Functions: `snake_case` (e.g., `http_server_create`)
- Types: `snake_case_t` (e.g., `http_request_t`)
- Constants/Enums: `UPPER_SNAKE_CASE` (e.g., `HTTP_METHOD_GET`)
- Private functions: `_snake_case` prefix (e.g., `_parse_http_header`)

### Memory Management
- Every `*_create()` must have a paired `*_destroy()`
- Document ownership transfer in function comments
- No naked `malloc()` in application code (use create functions)
- Avoid memory leaks: Valgrind clean in tests

### Error Handling
- Return `NULL` or `-1` on error (consistent with C conventions)
- Set `errno` where appropriate (platform-specific errors)
- Log errors to stderr (use `fprintf(stderr, ...)` or logging abstraction)
- Never crash on invalid input (validate everything)

### Testing
- Every new API must have unit tests
- Integration tests for protocol-level behavior
- Run tests before commit: `make test` must pass

---

## Priority Matrix

| Feature | Phase | Priority | Complexity | Est. Time |
|---------|-------|----------|------------|-----------|
| Complete HTTP Parser | 4 | 🎯 Critical | Medium | 3-5 days |
| Header & Parameter Storage | 4 | 🎯 Critical | Medium | 4-6 days |
| Robust Connection Handling | 4 | 🎯 Critical | High | 5-7 days |
| Complete JSON Support | 4 | 🎯 Critical | Medium | 3-4 days |
| Graceful Shutdown | 4 | 🎯 Critical | Medium | 2-3 days |
| Networking Integration Tests | 4 | 🎯 Critical | Medium | 4-5 days |
| **Phase 4 Total** | | | | **~3-4 weeks** |
| Request Body Parsing | 5 | 🔧 High | High | 6-8 days |
| Cookie Handling | 5 | 🔧 High | Low | 2-3 days |
| CORS Support | 5 | 🔧 High | Low | 1-2 days |
| Rate Limiting | 5 | 🔧 High | Medium | 3-4 days |
| Static File Serving | 5 | 🔧 High | Medium | 4-5 days |
| **Phase 5 Total** | | | | **~3 weeks** |
| SSL/TLS Support | 6 | 💡 Nice-to-Have | Very High | 15-20 days |
| Session Management | 6 | 🔧 High | Medium | 3-4 days |
| Authentication Middleware | 6 | 🔧 High | Medium | 4-5 days |
| Async WebSocket Mode | 6 | 💡 Nice-to-Have | High | 5-7 days |
| API Documentation | 6 | 🔧 High | Low | 3-4 days |
| Test Suite Expansion | 6 | 🎯 Critical | Medium | 5-6 days |
| **Phase 6 Total** | | | | **~6-8 weeks** |

**Legend**:
- 🎯 **Critical**: Blocks other features or fixes production issues
- 🔧 **High**: Important for production use but not blocking
- 💡 **Nice-to-Have**: Enhances capabilities but can be deferred

---

## Success Metrics

### Phase 4 (v0.4.0)
- ✅ All HTTP methods supported and tested
- ✅ Header API covers 100% of use cases
- ✅ 0 connection leaks in 1-hour load test
- ✅ JSON parser handles all valid RFC 8259 inputs
- ✅ Graceful shutdown completes in <30s with 100 active connections
- ✅ Integration test suite runs in CI

### Phase 5 (v0.5.0)
- ✅ File uploads tested up to 100MB
- ✅ CORS middleware passes W3C test suite
- ✅ Rate limiter sustains 10,000 req/s with limits
- ✅ Static file server matches Nginx performance (±20%)

### Phase 6 (v0.6.0)
- ✅ TLS 1.2/1.3 handshake completes with major browsers
- ✅ JWT validation passes test vectors
- ✅ API docs cover >95% of public functions
- ✅ Code coverage >90% (measured via gcov)

---

## Notes for Contributors

1. **Start with Phase 4**: These are prerequisite improvements. Don't skip ahead.
2. **Branch naming**: Use `feature/phase4-http-parser`, `feature/phase5-cors`, etc.
3. **Commit messages**: Follow [Conventional Commits](https://www.conventionalcommits.org/) (e.g., `feat(http): add chunked encoding support`)
4. **Pull requests**: Reference this document (e.g., "Implements Phase 4.1 - Complete HTTP Parser")
5. **Breaking changes**: Avoid when possible. If necessary, document in `CHANGELOG.md` and bump major version.

---

## Revision History

| Version | Date | Changes |
|---------|------|---------|
| 1.0 | 2025-01-12 | Initial roadmap for v0.4.0-v0.6.0 |

---

**Maintained by**: MCWL Core Team  
**Last Updated**: 2025-01-12  
**Status**: Active Development  
**License**: MIT (see LICENSE file)

For questions or discussions about this roadmap, please open an issue on GitHub or contact the maintainers.
