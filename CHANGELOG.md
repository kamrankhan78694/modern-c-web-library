# Changelog

All notable changes to the Modern C Web Library will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added
- **Phase 5: Request Processing & Security (v0.5.0)**
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

### WebSocket Frame Processing (Current)

This release completes the WebSocket implementation by adding full frame processing
capabilities in threaded mode. Previously, the server only performed the handshake
and immediately closed connections. Now:

**What Works:**
- ✅ Complete RFC 6455 WebSocket handshake
- ✅ Persistent WebSocket connections after upgrade
- ✅ Text message echo
- ✅ Binary message support
- ✅ Automatic ping/pong handling
- ✅ Multiple concurrent connections
- ✅ Graceful close with status codes

**Test Results:**
```
[Test 1] Sending text message...
✓ Received echo: Hello, WebSocket!

[Test 2] Testing ping/pong...
✓ Pong received! Latency: 0.000s

[Test 3] Sending another message...
✓ Received echo: Testing after ping

[Test 4] Sending multiple pings...
  Ping 1: 0.000s
  Ping 2: 0.000s
  Ping 3: 0.000s
✓ All pongs received!

✅ All tests passed!
```

**Architecture:**
- Threaded mode: One pthread per WebSocket connection (blocking I/O)
- Suitable for moderate connection counts (<1000)
- Production-ready for real-time applications

**Next Steps:**
- Async mode WebSocket support (epoll/kqueue/poll integration)
- WebSocket compression extensions
- Per-message deflate
- Enhanced connection tracking and metrics

---

## Version History

- **0.3.x**: WebSocket frame processing (Production-ready threaded mode)
- **0.2.x**: WebSocket support (RFC 6455 compliant)
- **0.1.x**: Initial HTTP server implementation with event loop

[Unreleased]: https://github.com/kamrankhan78694/modern-c-web-library/compare/v0.3.0...HEAD
[0.3.0]: https://github.com/kamrankhan78694/modern-c-web-library/releases/tag/v0.3.0
[0.2.0]: https://github.com/kamrankhan78694/modern-c-web-library/releases/tag/v0.2.0
[0.1.0]: https://github.com/kamrankhan78694/modern-c-web-library/releases/tag/v0.1.0
