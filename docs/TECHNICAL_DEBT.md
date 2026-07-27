# Known Trade-offs and Technical Debt

This document explicitly records architectural decisions, trade-offs, and known technical debt in the Modern C Web Library.  Items are categorised by severity and area.

## Architecture Trade-offs

### 1. Single-Process, Thread-Pool Model

**Decision**: The threaded mode uses a bounded thread pool (default 16 workers) rather than a multi-process model.

**Trade-off**: A single-process crash takes down all connections.  A multi-process model (like nginx) provides better fault isolation but is significantly more complex to implement in pure C without external process supervisors.

**Mitigation**: Run behind a process supervisor (systemd `Restart=on-failure`) or container orchestrator (Kubernetes) that restarts the process on failure.

### 2. Built-in TLS is Experimental and Off by Default

**Decision**: The default build implements no TLS; HTTPS is achieved by running behind a reverse proxy (nginx, Caddy, HAProxy).  A hand-written, zero-dependency pure-C TLS 1.3 *server* now exists behind the CMake option `WEBLIB_ENABLE_TLS` (default **OFF**, `CMakeLists.txt:80`, native targets only).  With the option off, no `src/tls` code is compiled and the build is byte-identical to a no-TLS build.  Enable it at runtime with `http_server_enable_tls(server, cert_pem, cert_len, key_pem, key_len)` before `http_server_listen()`; see `examples/tls_server.c`.

**Trade-off**: In the default configuration this means plaintext HTTP on the loopback interface between the proxy and the application.  Turning the option on trades that for hand-rolled crypto, which carries risk a mature proxy does not.  The layer is **EXPERIMENTAL and UNAUDITED** — it completes a real `openssl s_client` TLS 1.3 handshake and HTTPS round-trip in CI, but it has had no external cryptographic audit and must not be relied on for any security property in production.  It is also deliberately narrow: `TLS_CHACHA20_POLY1305_SHA256`, X25519, Ed25519, server-side, 1-RTT, with no cipher/curve/signature agility.  There is no AES-GCM, RSA, ECDSA, or bignum code by design — constant-time-by-construction primitives are the only responsible choice for crypto written from scratch.  Its full set of limits is recorded under "TLS layer (experimental)" below, and its security scope in `src/tls/README.md`.

**Mitigation**: For production, keep `WEBLIB_ENABLE_TLS=OFF` and deploy behind a TLS-terminating reverse proxy.  Bind the application to `127.0.0.1` only when a proxy is present.

### 3. Global / Static Middleware State

**Decision**: `middleware_fn_t` carries a `void *user_data` argument and `router_use_middleware_with_data()` registers middleware with per-instance state.  Four modules honour it — CORS, rate-limit, auth and logging — falling back to their module global when `user_data` is NULL.  **Five do not**: static files, CSRF, error handler, metrics and security headers each discard the argument and read only a module-level static, so only one instance of those types can exist at a time.

**Trade-off**: This simplifies the API (`cors_middleware_create` returns a function pointer) but, for the five unconverted modules, prevents multiple independent middleware instances (e.g. different static-file roots for different route groups).  Registering a second one silently reconfigures the first.

**Mitigation**: Converting the remaining five to the `user_data ? user_data : g_global` pattern the other four already use is planned.  For now, use route-specific middleware by checking the path inside the middleware callback.

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

1. **Integration test coverage** — Phase 7.5 added basic networking tests (GET, POST, 404, malformed, sequential connections).  Missing: keep-alive request *pipelining* (multiple requests written before the first response is read).  Chunked transfer encoding over real sockets is now covered by `test_stress_transfer_encoding_smuggling` (`tests/test_stress.c`), and timeout / slow-loris behaviour by `test_stress_async_idle_reaper`, `test_stress_slow_client`, `test_stress_slowloris_deadline` and `test_stress_request_deadline_silent`.  Concurrent parallel connections stress test was added in Phase 10.

2. **Memory leak on error paths** — Some HTTP parsing error paths may leak partial header allocations.  Valgrind CI gate catches definite/indirect leaks, but conditional leaks under extreme error conditions need additional audit.

### Medium Priority

3. **No async-mode test for the request body limit** — Async mode shares the threaded parser (`async_read_handler` → `http_parser_execute`, `src/http_server.c`), so `MAX_BODY_BYTES` *is* enforced in async mode, at all three of its guards: the `Content-Length` pre-check, the streaming accumulation check in `append_body_data`, and the chunked `chunk_size` check.  `async_on_parser_result` returns the resulting 413.  The gap is test coverage only: the oversized-body regression test (`test_stress_oversized_request`) runs in threaded mode, and the two async integration tests (`test_stress_async_idle_reaper`, `test_stress_async_server_restart`) do not exercise body size.  Add an async-mode equivalent.

4. **Session store is in-memory only** — Sessions are lost on server restart.  A file-backed or pluggable session store would improve production readiness.

5. ~~**Rate limiter uses linear scan**~~ — **NOT A DEFECT** (documentation error; this item was never accurate).  IP tracking has always used a 1024-bucket hash table with separate chaining, not a flat array: `hash_bucket_t table[HASH_TABLE_SIZE]`, a djb2 `_hash_ip()`, and `_find_or_create_entry()`, which hashes first and then walks only the matching bucket's chain (`src/middleware_ratelimit.c`).  Lookup is average-case O(1) under uniform hashing.  Residual caveat: the bucket count is fixed at 1024 with no resize path and `_hash_ip()` is unkeyed, so chains do still grow linearly at very high IP cardinality.

6. ~~**No HTTP/1.1 keep-alive support**~~ — **RESOLVED** (Phase 7).  The parser detects HTTP/1.1 `Connection: keep-alive` and loops within `handle_connection()` to serve multiple requests per TCP connection.

7. **Template engine is basic** — Only supports `{{ variable }}` substitution.  No conditionals, loops, or includes.  Adequate for simple pages but not for complex rendering.

### Low Priority

8. **No Windows async I/O (IOCP)** — Async mode uses `epoll` (Linux), `kqueue` (macOS/BSD), or `poll` (fallback).  Windows IOCP is not implemented.

9. **JSON parser does not validate UTF-8** — The JSON parser accepts and produces strings but does not validate that they contain valid UTF-8 sequences.  Invalid UTF-8 passes through unmodified.

10. ~~**No response compression**~~ — **RESOLVED** (Phase 9).  Pure C gzip compression (RFC 1951/1952) is implemented with `Accept-Encoding` content negotiation.

11. **Static file serving has no byte-range resume** — Range request headers are not honoured for partial content delivery.

### TLS layer (experimental)

The pure-C TLS 1.3 server in `src/tls/` (~5,500 lines) is off by default (`WEBLIB_ENABLE_TLS=OFF`).  These are its known limits.  None of them apply to a default build, in which no `src/tls` code is compiled at all.

12. **UNAUDITED — no external cryptographic review.** Both the primitives and the handshake state machine are hand-written and have not been independently reviewed.  Do not rely on this for any security property in production (`src/tls/README.md`).  Clearing this item means a real audit by an outside party, not more self-testing.

13. **Threaded mode only** — TLS termination is wired only into the threaded server path.  The two modes are mutually exclusive and refuse each other rather than silently misbehaving: `http_server_enable_tls()` returns -1 when async mode is set, and `http_server_set_async(true)` returns -1 when TLS is enabled (`src/http_server.c`).  An HTTPS server therefore gives up the async event loop and its connection scaling.

14. **Native builds only** — the Emscripten/WASM and Cloudflare Workers targets never compile `src/tls`; those platforms terminate TLS in the host runtime.

15. **No WebSocket over TLS (`wss://`)** — a WebSocket upgrade arriving on a TLS connection is refused with `503 Service Unavailable` rather than served, so it fails loudly instead of speaking plaintext frames inside the TLS session (`src/http_server.c`).  Wiring `wss://` is deferred to the interoperability milestone.

16. **Ed25519-only certificates; no browser support** — the profile signs with Ed25519 and nothing else, and Ed25519 server certificates have limited and inconsistent browser support.  `openssl s_client` interoperates end-to-end (`tests/interop_openssl.sh`), but a browser page-load has not been achieved.  Supporting browsers means adding ECDSA P-256 or RSA, which means adding the bignum code the design deliberately avoids.

17. **No session resumption, tickets, PSK, 0-RTT, or KeyUpdate** — every connection pays a full 1-RTT handshake, and a long-lived connection cannot rekey.  Post-handshake handshake messages are rejected rather than processed.

18. **No client-certificate verification** — mutual TLS is not implemented; the server never requests or validates a client certificate.

19. **No certificate chain or validity checking of the server's own certificate** — the configured PEM is decoded and sent as-is.  Serving an expired or malformed certificate fails at the client, not at startup.

20. **`WEBLIB_TLS_TEST_HOOKS` must never ship** — this option (default OFF) compiles a deterministic-RNG seam that replaces the system CSPRNG, so that `TlsHttpTests` can reproduce a handshake byte for byte.  A build with it enabled has no handshake secrecy whatsoever.  It is enabled only in CI's `tls-check` job.

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

**Last Updated**: 2026-07-27 (v2.0.0)

This revision re-verified against the source: Architecture Trade-off 2 (TLS), and debt items 1, 3 and 5, and added items 12–20 for the TLS layer.  The remaining items were carried forward from the 2026-02-22 revision without re-verification — treat their details as unconfirmed until someone checks them against the current code.
