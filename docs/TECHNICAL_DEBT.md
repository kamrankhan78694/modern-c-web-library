# Known Trade-offs and Technical Debt

This document explicitly records architectural decisions, trade-offs, and known technical debt in the Modern C Web Library.  Items are categorised by severity and area.

## Architecture Trade-offs

### 1. Single-Process, Thread-Pool Model

**Decision**: The threaded mode uses a bounded thread pool (default 16 workers) rather than a multi-process model.

**Trade-off**: A single-process crash takes down all connections.  A multi-process model (like nginx) provides better fault isolation but is significantly more complex to implement in pure C without external process supervisors.

**Mitigation**: Run behind a process supervisor (systemd `Restart=on-failure`) or container orchestrator (Kubernetes) that restarts the process on failure.

### 2. No Built-in TLS

**Decision**: The library does not implement TLS.  HTTPS is achieved by running behind a reverse proxy (nginx, Caddy, HAProxy).

**Trade-off**: This means plaintext HTTP on the loopback interface between the proxy and the application.  A pure-C TLS implementation is planned (Phase 8) but is a large undertaking covering SHA-256, AES-GCM, RSA/ECDSA, PEM parsing, and certificate validation.

**Mitigation**: Deploy behind a TLS-terminating reverse proxy.  Bind the application to `127.0.0.1` only when a proxy is present.

### 3. Global / Static Middleware State

**Decision**: Several middleware modules (CORS, rate-limit, static files, auth, CSRF, logging, error handler) use module-level static variables for their configuration.  Only one instance of each middleware type can exist at a time.

**Trade-off**: This simplifies the API (`cors_middleware_create` returns a function pointer) but prevents multiple independent middleware instances (e.g. different rate limits for different route groups).

**Mitigation**: Refactoring to per-instance state carried via `void *user_data` closures is planned.  For now, use route-specific middleware by checking the path inside the middleware callback.

### 4. Compile-Time Static Limits

**Decision**: Key capacity limits are `#define` constants rather than runtime-configurable:

| Constant | Value | File |
|----------|-------|------|
| `MAX_CONNECTIONS` | 128 | http_server.c |
| `MAX_ROUTES` | 256 | router.c |
| `MAX_MIDDLEWARES` | 32 | router.c |
| `MAX_HEADER_COUNT` | 100 | http_server.c |
| `MAX_BODY_BYTES` | 1 MiB | http_server.c |
| `MAX_EVENTS` | 1024 | event_loop.c |
| `MAX_TIMERS` | 64 | event_loop.c |

**Trade-off**: Simplifies memory management (no dynamic resizing) but requires recompilation to change limits.

**Mitigation**: Make these configurable via `http_server_set_*()` APIs or CMake options in a future release.

## Known Technical Debt

### High Priority

1. **Integration test coverage** — Phase 7.5 added basic networking tests (GET, POST, 404, malformed, sequential connections).  Missing: keep-alive pipelining, chunked transfer encoding over sockets, concurrent parallel connections stress test, and timeout behavior tests.

2. **Memory leak on error paths** — Some HTTP parsing error paths may leak partial header allocations.  Valgrind CI gate catches definite/indirect leaks, but conditional leaks under extreme error conditions need additional audit.

3. **No request body limit enforcement in async mode** — The `MAX_BODY_BYTES` limit is enforced in threaded mode but async mode body accumulation is not yet implemented.

### Medium Priority

4. **Session store is in-memory only** — Sessions are lost on server restart.  A file-backed or pluggable session store would improve production readiness.

5. **Rate limiter uses linear scan** — IP tracking in the rate limiter uses a fixed-size array with linear search.  This is O(n) per request where n is the number of tracked IPs.  A hash table would improve performance under high cardinality.

6. **No HTTP/1.1 keep-alive support** — Each request opens a new TCP connection.  The server does not reuse connections for multiple requests, which increases latency and resource usage.

7. **Template engine is basic** — Only supports `{{ variable }}` substitution.  No conditionals, loops, or includes.  Adequate for simple pages but not for complex rendering.

### Low Priority

8. **No Windows async I/O (IOCP)** — Async mode uses `epoll` (Linux), `kqueue` (macOS/BSD), or `poll` (fallback).  Windows IOCP is not implemented.

9. **JSON parser does not validate UTF-8** — The JSON parser accepts and produces strings but does not validate that they contain valid UTF-8 sequences.  Invalid UTF-8 passes through unmodified.

10. **No response compression** — Responses are sent uncompressed.  gzip/deflate support is planned (Phase 9).

11. **Static file serving has no byte-range resume** — Range request headers are not honoured for partial content delivery.

## Deliberate Non-Goals

These items are intentionally not implemented and are not considered debt:

- **External dependency management** — The project philosophy is zero external dependencies.  Features that would normally use OpenSSL, libcurl, or similar libraries are implemented from scratch in C.
- **Multi-language support** — No Python, JavaScript, or other language bindings.  The library is pure C.
- **ORM / query builder** — Database interaction is limited to connection pooling.  Query building is the application's responsibility.

## Addressing Technical Debt

When working on technical debt:

1. Check `TODO.md` for the planned phase where the item is scheduled
2. Write tests *before* the fix (TDD approach)
3. Run Valgrind to verify no new memory issues
4. Update this document to mark items as resolved

---

**Last Updated**: 2026-02-20
