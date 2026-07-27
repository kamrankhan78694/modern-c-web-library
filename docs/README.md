# Modern C Web Library Documentation

**Version 2.0.0**  
[![DOI](https://zenodo.org/badge/DOI/10.5281/zenodo.18793559.svg)](https://doi.org/10.5281/zenodo.18793559)

Welcome to the documentation for Modern C Web Library, a pure C web framework with zero external dependencies.

The HTTP, WebSocket and middleware core is the stable, supported surface of the library. 2.0.0 is mostly a security-hardening release over v1.0.0, and it **breaks source compatibility in three places** — see [Upgrading from 1.0.0](#upgrading-from-100). The TLS 1.3 layer that 2.0.0 adds is **experimental, unaudited, and off by default** — see [New in 2.0.0](#new-in-200) before you consider it for anything real.

## Quick Links

### Getting Started
- **[Getting Started Tutorial](tutorials/getting-started.md)** — Your first web server in 5 minutes
- **[Complete API Reference](api/README.md)** — Comprehensive API documentation
- **[Deployment Guide](DEPLOYMENT.md)** — Production deployment best practices

### Tutorials
- **[Building a REST API](tutorials/rest-api.md)** — Complete CRUD API with middleware
- **[Real-Time WebSocket Applications](tutorials/websocket.md)** — Chat servers and live dashboards
- **[WebSocket Protocol Details](WEBSOCKET.md)** — Deep dive into WebSocket implementation

### Advanced Topics
- **[Debugging Guide](DEBUGGING.md)** — Debug tools and techniques
- **[Technical Debt](TECHNICAL_DEBT.md)** — Known limitations and future work
- **[Changelog](../CHANGELOG.md)** — What changed in each release, including the source-breaking changes in 2.0.0
- **[Cloudflare Workers API](WORKER_API.md)** — Worker request/response, the fetch handler, and the KV, R2, D1 and Queues bindings
- **[Experimental TLS 1.3 Layer](../src/tls/README.md)** — what the hand-written pure-C TLS server does and does not give you (EXPERIMENTAL · UNAUDITED; off by default, native builds only)

## Core Feature Set

The library's stable, supported surface. All of it shipped in v1.0.0; 2.0.0 hardened it rather than replacing it, with three source-level breaks noted under [Upgrading from 1.0.0](#upgrading-from-100):

### Core HTTP Server
- Multi-threaded and async I/O modes
- Routing with path parameters
- Middleware chain processing
- Request/response helpers
- JSON parser and serializer
- Cross-platform (Linux, macOS, Windows)

### WebSocket Support
- RFC 6455 compliant implementation
- Synchronous and asynchronous modes
- Event loop integration
- Text and binary messages
- Ping/pong and fragmentation support

### Middleware Stack
- **Authentication** — JWT, Basic Auth, API Key
- **CORS** — Configurable cross-origin resource sharing
- **CSRF** — Double-submit cookie protection
- **Rate Limiting** — Token bucket algorithm
- **Static Files** — MIME types, ETag, path traversal prevention
- **Logging** — Configurable log levels
- **Error Handler** — Centralized JSON error responses
- **Metrics** — Request counting and statistics

### Advanced Features
- **Body Parser** — JSON, URL-encoded, multipart with file uploads
- **Compression** — Pure C gzip/DEFLATE implementation
- **Session Management** — Cookie-based with expiration
- **Template Engine** — `{{ variable }}` syntax rendering (HTML-escaped by default since 2.0.0; `{{{ variable }}}` emits raw HTML)
- **Caching** — In-memory LRU cache with TTL
- **Thread Pool** — Bounded worker management
- **Database Pool** — Thread-safe connection pooling
- **Input Validation** — Length, charset, email, HTML sanitization
- **Health Check** — `/healthz` endpoint
- **Benchmarking** — Throughput and latency measurement

## New in 2.0.0

### Experimental TLS 1.3 Server (opt-in, off by default)

A hand-written TLS 1.3 server written in the same pure C as the rest of the library — no OpenSSL, no mbedTLS, and `PLATFORM_LIBS` gains no new link library. What that means for you:

- **You must opt in at build time.** Configure with `-DWEBLIB_ENABLE_TLS=ON`. With the option off (the default) not a single line of `src/tls/` is compiled and your build is exactly what it was before.
- **Server-side TLS 1.3 only, with one profile and no agility**: cipher suite `TLS_CHACHA20_POLY1305_SHA256`, X25519 key exchange, Ed25519 signatures, ALPN negotiating `http/1.1`, and HelloRetryRequest for a client that omits its key share. There is no TLS 1.2, no AES-GCM, and no RSA or ECDSA. That narrowness is deliberate: these three primitives are constant-time by construction and need no big-integer code, which is the only defensible position for crypto written from scratch.
- **Native builds only.** The WASM and Cloudflare Workers targets never compile it.
- **Threaded mode only.** `http_server_enable_tls()` returns `-1` if async mode is already set, and `http_server_set_async()` returns `-1` once TLS is on. A WebSocket upgrade over a TLS connection is refused with `503`.
- **Turn it on at runtime** before `http_server_listen()`. Note that it takes PEM **buffers with explicit lengths**, not file paths — read the files yourself, as `examples/tls_server.c` does:

  ```c
  int http_server_enable_tls(http_server_t *server,
                             const char *cert_pem, size_t cert_len,
                             const char *key_pem,  size_t key_len);
  ```

- **Interop today**: a real `openssl s_client` completes a TLS 1.3 handshake and a full HTTPS round-trip against it in CI, including a response larger than 16 KiB fragmented across records. Browser page-load is **not** achieved — Ed25519-only certificates have limited and inconsistent browser support.
- **EXPERIMENTAL and UNAUDITED.** It has had no external cryptographic audit. Do not rely on it for any security property in production; terminate TLS at a reverse proxy instead. The full scope and its limits are in [`../src/tls/README.md`](../src/tls/README.md).

### Security Headers Middleware

- `security_headers_middleware_create(&cfg)` sets the standard hardening headers on every response: `Content-Security-Policy`, `X-Content-Type-Options`, `X-Frame-Options`, `Referrer-Policy`, `Permissions-Policy` and `X-XSS-Protection: 0`. `Strict-Transport-Security` is opt-in via `cfg.enable_hsts`, since sending HSTS from a server reached over plain HTTP is a good way to lock yourself out.
- Pass `NULL` to take safe defaults (`default-src 'self'`, `X-Frame-Options: DENY`, `Referrer-Policy: strict-origin-when-cross-origin`). Call `security_headers_middleware_destroy()` at shutdown. Declared in `include/kamran.k`.

### WebAssembly and Cloudflare Workers targets

- **WebAssembly (Emscripten)** — a WASM-safe subset builds under `emcmake cmake`: JSON, router, template engine, input validation, cookies, body parsing and compression. Sockets, the shared library and TLS are excluded there. Exports for a JavaScript host (`wasm_weblib_version()`, `wasm_weblib_capabilities()`, `wasm_weblib_has_capability()` and thin JSON wrappers) plus `examples/wasm_example.c`.
- **Cloudflare Workers** — a fetch-event compatibility layer (`src/worker_runtime.c`) bridges a Worker's request/response model to the router, with pure-C APIs shaped like the KV, R2, D1 and Queues bindings. In a deployed Worker the JS glue bridges them to the real bindings; **in native and test builds they are backed by in-memory simulations** with fixed capacities, and `worker_d1_batch()` is not atomic the way Cloudflare's `env.DB.batch()` is. Details in [Cloudflare Workers API](WORKER_API.md).

### Upgrading from 1.0.0

2.0.0 breaks source compatibility in three places. The full list, with the reasoning behind each, is in [CHANGELOG.md](../CHANGELOG.md):

- **The public header is now `kamran.k`.** `include/weblib.h` no longer exists and there is no compatibility shim — change every `#include "weblib.h"` to `#include "kamran.k"`. This is the break that makes the release 2.0.0.
- **Session data access is keyed on `(store, session_id)`.** `session_set_data()`, `session_get_data()` and `session_remove_data()` now take a `session_store_t *` and a session id instead of a `session_t *` handle, and `session_get_data()` returns a freshly allocated copy you must `free()` rather than a borrowed pointer.
- **`{{ var }}` template interpolation HTML-escapes by default.** Use `{{{ var }}}` where you genuinely intend raw HTML — templates that relied on `{{ }}` to emit markup will render it escaped after upgrading.

Most of the rest of 2.0.0 is security hardening on the v1.0.0 core. Read the *Security* section of the changelog before deciding when to upgrade.

## Architecture Overview

The library follows a modular design with clean separation of concerns:

```
┌─────────────────────────────────────────────────┐
│              Application Layer                   │
│  (Your Routes, Handlers, Business Logic)        │
└─────────────────┬───────────────────────────────┘
                  │
┌─────────────────▼───────────────────────────────┐
│           Middleware Stack                       │
│  CORS │ Auth │ CSRF │ Rate Limit │ Logging     │
└─────────────────┬───────────────────────────────┘
                  │
┌─────────────────▼───────────────────────────────┐
│              Router                              │
│  Path Matching │ Parameter Extraction           │
└─────────────────┬───────────────────────────────┘
                  │
┌─────────────────▼───────────────────────────────┐
│          HTTP Server Core                        │
│  Request Parser │ Response Builder              │
└─────────────────┬───────────────────────────────┘
                  │
┌─────────────────▼───────────────────────────────┐
│          Event Loop / Threading                  │
│  epoll │ kqueue │ poll │ pthread                │
└─────────────────┬───────────────────────────────┘
                  │
┌─────────────────▼───────────────────────────────┐
│          Platform APIs                           │
│  Sockets │ Files │ Time │ Memory                │
└──────────────────────────────────────────────────┘
```

## Zero Dependencies Philosophy

This project is built entirely in pure C (C99/C11) with zero external dependencies:

- ❌ No OpenSSL
- ❌ No libcurl
- ❌ No cJSON
- ❌ No third-party libraries

Everything is implemented from scratch:
- ✅ HTTP parser
- ✅ JSON parser
- ✅ WebSocket protocol
- ✅ Compression (gzip/DEFLATE)
- ✅ JWT implementation (HMAC-SHA256)
- ✅ All middleware and features
- ⚗️ TLS 1.3 — including SHA-256/512, HMAC, HKDF, ChaCha20-Poly1305, X25519, Ed25519 and X.509/DER/PEM parsing (opt-in, EXPERIMENTAL and UNAUDITED)

**Why?** Maximum portability, transparency, and educational value.

Note that this cuts both ways. Hand-written crypto gets none of the scrutiny that OpenSSL has had, which is exactly why the TLS layer is off by default and labelled unaudited.

## Platform Support

| Platform | Threading | Async I/O | Status |
|----------|-----------|-----------|---------|
| **Linux** | pthread | epoll | ✅ Full support |
| **macOS** | pthread | kqueue | ✅ Full support |
| **FreeBSD** | pthread | kqueue | ✅ Full support |
| **Windows** | Win32 threads | IOCP (planned) | ⚠️ Basic support |

The optional TLS 1.3 layer is narrower than the table above: it is compiled on native targets only (never WASM or Cloudflare Workers) and runs in threaded mode only.

### Future Plans

- Improve Windows support by implementing a full IOCP backend and solidifying Win32 threading integration.

## Performance

Designed for high performance:
- **Async I/O**: Handles thousands of concurrent connections
- **Event Loop**: Platform-specific backends (epoll/kqueue)
- **Thread Pool**: Bounded worker management
- **Zero Copy**: Minimal memory allocations
- **LRU Cache**: Fast in-memory caching
- **Compression**: Optional gzip for bandwidth savings

Benchmark results are in [STRESS_TESTS.md](../STRESS_TESTS.md) — originally written against v0.9.0, with its figures re-verified against 2.0.0.

## Code Quality

- **6 ctest suites in a default build** — `WebLibTests` (166 unit tests), `KamranHeaderTests`, `AsyncWebSocketTests`, `StressTests`, `WorkerTests`, `WasmTests`. Building with `-DWEBLIB_ENABLE_TLS=ON -DWEBLIB_TLS_TEST_HOOKS=ON` adds 7 more covering the experimental TLS layer, for 13 in total. Run them all with `cd build && ctest --output-on-failure`.
- **Valgrind clean** — the default build runs the full suite under Valgrind in CI with zero leaks. The TLS build is covered by an ASan/UBSan run instead, not Valgrind.
- **Static analysis** — Clean builds on GCC/Clang with `-Wall -Wextra -pedantic`
- **CI/CD** — `primary-checks` (gcc build, full suite, Valgrind), `clang-check`, `tls-check` (TLS build plus an ASan/UBSan run of the TLS suites), `macos-check` (pull requests only), and `docker-image-check`
- **Code review** — All changes reviewed

## Examples

The `examples/` directory contains working examples:

- **simple_server.c** — Basic HTTP server (threaded mode)
- **async_server.c** — Async I/O server (event loop)
- **websocket_echo_server.c** — WebSocket echo server
- **async_websocket_echo_server.c** — Async WebSocket server
- **rest_api_server.c** — Full REST API with CRUD operations
- **worker_example.c** — Cloudflare Workers runtime with KV, R2, D1 and Queues bindings (runs natively against in-memory backends)
- **wasm_example.c** — The WASM-safe subset: JSON, router, templates and validation (builds native or via Emscripten)
- **tls_server.c** — HTTPS over the hand-written pure-C TLS 1.3 layer. Only built with `-DWEBLIB_ENABLE_TLS=ON`, and EXPERIMENTAL and UNAUDITED — do not put it in front of real users.

## Citation

If you use this library in your research or project, please cite the archived deposit. The record below is the v1.0.0 deposit — if a 2.0.0 record is later archived, cite that DOI and version instead:

```bibtex
@software{khan2026modern,
  author       = {Kamran Khan},
  title        = {Modern C Web Library: Production-Ready Pure C Web Framework},
  year         = {2026},
  publisher    = {Zenodo},
  version      = {1.0.0},
  doi          = {10.5281/zenodo.18793559},
  url          = {https://doi.org/10.5281/zenodo.18793559}
}
```

**DOI:** https://doi.org/10.5281/zenodo.18793559

## Contributing

Contributions are welcome! Please read [CONTRIBUTING.md](../CONTRIBUTING.md) for guidelines.

## License

MIT License. See [LICENSE](../LICENSE) for details.

## Support

- **GitHub Issues**: [Report bugs or request features](https://github.com/kamrankhan78694/modern-c-web-library/issues)
- **Discussions**: [Ask questions or discuss ideas](https://github.com/kamrankhan78694/modern-c-web-library/discussions)
- **Documentation**: This documentation is continuously updated

---

**Happy coding with Modern C Web Library!** 🚀
