# Changelog

All notable changes to the Modern C Web Library will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

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
