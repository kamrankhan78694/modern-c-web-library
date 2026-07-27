# Modern C Web Library - AI Agent Instructions

## Project Philosophy: Pure C Implementation

This is a **strict pure C project** with zero external dependencies. All contributions must align with this core principle:

- **Language**: ISO C (C99/C11) only - no Python, JavaScript, or other languages
- **Dependencies**: None - implement everything from scratch in C
- **Libraries**: Only standard C library + platform APIs (POSIX, Windows API)
- **Goal**: Demonstrate that modern web backends can be built entirely in self-sufficient C

**Critical**: Never suggest external libraries (OpenSSL, libcurl, cJSON, etc.). If a feature needs implementation, write it in pure C. This is why the repo contains a hand-written TLS 1.3 implementation (`src/tls/`) rather than a link against OpenSSL — see the TLS section below, because reviewing hand-written cryptography needs extra care.

### Build Targets

The same sources build for three targets, so a change has to be considered against all three:

- **Native** (Linux/macOS/Windows) — the full library, including sockets, threads, WebSocket and the opt-in TLS layer.
- **WebAssembly** (Emscripten) — a pure-logic subset. `CMakeLists.txt` splits sources into `WEBLIB_SOURCES_WASM_SAFE` and `WEBLIB_SOURCES_NATIVE_ONLY`; anything doing OS I/O belongs in the latter and is excluded from WASM builds.
- **Cloudflare Workers** — the `src/worker_*.c` runtime (fetch handler, KV, R2, D1, Queues), compiled to WASM. See `docs/WORKER_API.md`.

### Warning Policy

`CMakeLists.txt` applies `-Wall -Wextra -pedantic` (`/W4` on MSVC) to every target, and CI builds with both GCC and Clang. **A change must not introduce a new warning on any target.** Fix the cause; don't silence with a pragma.

### The Umbrella Header

`include/kamran.k` is the single public header — every public type, macro and function declaration lives there, and user code includes only `#include "kamran.k"`. (`include/db_pool.h` is the one extra installed header.) Anything not declared in `kamran.k` is internal; keep it in `src/`.

## Architecture Overview

### Component Structure

Four components carry most of the request path. Around them sits a wider module set — middleware (CORS, auth, CSRF, rate limiting, logging, metrics, security headers, static files), sessions, cookies, body parsing, templates, compression, caching, health checks, a thread pool, a DB pool, WebSocket, the WASM and Cloudflare Worker runtimes, and the opt-in TLS layer described further below.

1. **HTTP Server** (`src/http_server.c`): Handles both threaded and async I/O modes
   - Threaded mode: `pthread` for concurrent connections
   - Async mode: Event loop for non-blocking I/O (see below)
   - Connection handling: `MAX_CONNECTIONS=128`, `READ_BUFFER_SIZE=8192`
   - Also the place where TLS termination is wired in, under `#ifdef WEBLIB_TLS`

2. **Event Loop** (`src/event_loop.c`): Platform-specific async I/O backends
   - Linux: `epoll` (high performance)
   - macOS/BSD: `kqueue` (high performance) 
   - Fallback: `poll` (portable)
   - Compile-time selection via preprocessor directives
   - Max events: 1024, Max timers: 64

3. **Router** (`src/router.c`): Request routing with middleware chain
   - Static limits: `MAX_ROUTES=256`, `MAX_MIDDLEWARES=32`
   - Route parameters: `/users/:id` pattern matching
   - Middleware: Boolean return (true=continue, false=stop)

4. **JSON Parser** (`src/json.c`): Hand-written JSON parser/serializer
   - No external JSON library - custom implementation
   - Linked list structures for objects/arrays
   - Memory management: All JSON must be freed with `json_value_free()`

### Operating Modes

The server supports two modes that affect I/O handling:

**Threaded Mode** (default):
- `pthread` creates thread per connection
- Blocking I/O operations
- Simpler programming model
- Example: `examples/simple_server.c`

**Async Mode** (via `http_server_set_async(server, true)`):
- Event loop with non-blocking I/O
- Single-threaded, handles thousands of connections
- Requires event-driven programming
- Example: `examples/async_server.c`

## The TLS Layer (`src/tls/`) — hand-written and unaudited

`src/tls/` holds a **hand-written, zero-dependency TLS 1.3 server**: 5,481 lines of pure C, crypto primitives included. It is the most dangerous code in the repository and should be reviewed to a stricter standard than anything else here.

**What it is.** TLS 1.3 (RFC 8446), server-side, with a single profile and no agility: `TLS_CHACHA20_POLY1305_SHA256` + X25519 + Ed25519. No AES-GCM, no RSA, no ECDSA, no TLS 1.2. A client that cannot offer all three is refused with `handshake_failure`. It includes primitives that each carry RFC known-answer tests (SHA-256, SHA-512, HMAC, HKDF, ChaCha20, Poly1305, ChaCha20-Poly1305 AEAD, X25519, Ed25519), DER/ASN.1 and PEM parsing plus Ed25519 key (PKCS#8/SPKI) parsing — the server certificate is PEM-decoded to DER and sent opaquely, never parsed as X.509 — a record layer honouring the 2^14 plaintext limit with fragmentation (a response over 16 KiB spans several records), and a full server handshake state machine with HelloRetryRequest and ALPN negotiation of **`http/1.1`** (not h2).

**EXPERIMENTAL and UNAUDITED.** It has had no external cryptographic audit and is not for production use. Never drop or soften that caveat — in code comments, docs, or PR text. If a change removes it, ask for it back.

**The handshake state machine is the highest-risk code in the repo.** TLS vulnerabilities have historically clustered in state handling — messages accepted out of order or skipped, transcript confusion, error paths that leave a connection usable — far more than in the primitives. When reviewing `src/tls/server_handshake.c`, `handshake.c`, `handshake_auth.c` or `tls_khannection.c`, check that every error latches a terminal state and wipes secrets, and that no message can be accepted out of sequence.

**Real constraints of the shipped code** (state these accurately; never more strongly):

- Server-side only — there is no TLS client.
- Threaded mode only — `http_server_enable_tls()` returns -1 when async mode is on.
- Native only — not compiled into WASM or Cloudflare Workers builds.
- A WebSocket upgrade over a TLS connection is **refused**: the server replies HTTP 503 `WebSocket over TLS not supported` (`src/http_server.c`).
- Interop with a real `openssl s_client` TLS 1.3 handshake and HTTPS round-trip (including a >16 KiB response and keep-alive) is verified in CI. **Browser page-load is not achieved** — Ed25519-only certificates have limited and inconsistent browser support. Never claim browser compatibility.
- The handshake is bounded by the total-request deadline, set with `http_server_set_request_timeout()` (default 60s) — not by `http_server_set_timeout()`, which sets the per-recv/send socket timeouts.

**Deliberately absent** — not bugs, but don't hide them either: no ChangeCipherSpec emission, no downgrade sentinel, no session resumption / 0-RTT / KeyUpdate, no client-certificate authentication, no SNI-based certificate selection.

**Build flags:**

- `WEBLIB_ENABLE_TLS` — default **OFF**. With it off, nothing under `src/tls/` is compiled and the build is byte-identical to a build without the TLS layer.
- `WEBLIB_TLS_TEST_HOOKS` — default **OFF**. Gates a deterministic-RNG seam used only by `TlsHttpTests`. It must never be on in a production build; a deterministic RNG would destroy TLS security.

**Public API.** Declared in `include/kamran.k`. It takes PEM **buffers with explicit lengths**, *not* file paths — a lot of older prose gets this wrong:

```c
int http_server_enable_tls(http_server_t *server,
                           const char *cert_pem, size_t cert_len,
                           const char *key_pem,  size_t key_len);
```

Returns 0 on success, -1 on: NULL/empty arguments, malformed PEM, async mode enabled, TLS already enabled, or the server already listening — it must be called before `http_server_listen()`. In a build without `WEBLIB_ENABLE_TLS` the function still exists and always returns -1.

**Every change under `src/tls/` must pass both configurations of the CI `tls-check` job** — the plain TLS build running the full suite, and the ASan/UBSan build running the TLS suites. The sanitizer build is not optional: it is the only thing that catches memory errors and undefined behaviour in hand-written crypto.

`src/tls/README.md` is the authoritative design and threat-model document. Link to it rather than restating it.

## Development Workflows

### Building and Testing

**Using Docker (Recommended for Contributors):**
```bash
# Run all tests in consistent environment
./docker-run.sh test

# Start development container with volume mount
./docker-run.sh dev

# Inside container: build, test, debug. The bind mount puts your working tree over
# /workspace, so the image's own build/ is hidden — configure a fresh one.
mkdir -p build && cd build
cmake .. && make
ctest --output-on-failure
./tests/test_weblib
valgrind --leak-check=full ./tests/test_weblib
```

**Native Build:**
```bash
# Initial build
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build --parallel

# Run tests (custom test framework, no external test libs)
cd build && ctest --output-on-failure

# Run examples
./examples/simple_server [port]    # Default: 8080
./examples/async_server [port]
```

**Build with the experimental TLS layer** (OFF by default, so the commands above compile none of `src/tls/`):
```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo -DWEBLIB_ENABLE_TLS=ON
cmake --build build --parallel
cd build && ctest --output-on-failure
```

Add `-DWEBLIB_TLS_TEST_HOOKS=ON` as well to get `TlsHttpTests` — without it that suite is not built at all, and you see 12 suites rather than 13.

To reproduce the CI sanitizer leg, use a **second** build directory (the sanitizer flags must not leak into your normal build) and repeat both TLS options there:

```bash
cmake -S . -B build-tls-san -DCMAKE_BUILD_TYPE=Debug \
  -DWEBLIB_ENABLE_TLS=ON -DWEBLIB_TLS_TEST_HOOKS=ON \
  -DCMAKE_C_FLAGS="-Wall -Wextra -pedantic -fsanitize=address,undefined -fno-omit-frame-pointer -g"
cmake --build build-tls-san --parallel
cd build-tls-san && ASAN_OPTIONS=detect_leaks=0 \
  UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
  ctest --output-on-failure --no-tests=error -R '^Tls'
```

`--no-tests=error` is not optional with `-R`: `ctest` exits 0 when a filter matches nothing, so a renamed suite or a dropped `add_test()` would look green while running no crypto tests at all.

The TLS example needs an **Ed25519** certificate — the only key type this server accepts:
```bash
openssl genpkey -algorithm ed25519 -out key.pem
openssl req -x509 -new -key key.pem -out cert.pem -days 365 -subj "/CN=localhost"

./build/examples/tls_server cert.pem key.pem 8443      # routes: / /hello /big
printf 'GET / HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n' \
  | openssl s_client -quiet -connect 127.0.0.1:8443 -tls1_3
```

**Docker Quick Reference:**
- `./docker-run.sh test` - Build and run tests in container
- `./docker-run.sh dev` - Development shell
- `./docker-run.sh async` - Run async server (`threaded` for the threaded one)
- See `DOCKER.md` for complete guide

### CI Gates

`.github/workflows/ci.yml` defines five jobs. Every one of them must be green before a PR merges:

| Job | What it checks |
| --- | --- |
| `primary-checks` | GCC build in `Dockerfile.dev`, full `ctest` run, then Valgrind `--leak-check=full` over every test binary. All other jobs depend on this one. |
| `clang-check` | Clang build with `-Wall -Wextra -pedantic` plus `ctest` — catches what GCC misses. |
| `tls-check` | `-DWEBLIB_ENABLE_TLS=ON -DWEBLIB_TLS_TEST_HOOKS=ON` twice: a RelWithDebInfo build running the full suite (including the real `openssl s_client` interop test), and an ASan/UBSan build running the TLS suites. **Required for any change under `src/tls/`.** |
| `macos-check` | Clang build + tests on macOS. Pull requests only. |
| `docker-image-check` | Builds the production `Dockerfile` and verifies the example binaries are present and executable. |

### Platform-Specific Compilation

The project uses preprocessor directives for platform detection:

```c
#ifdef __linux__
    // Linux-specific (epoll)
#elif defined(__APPLE__) || defined(__FreeBSD__)
    // macOS/BSD-specific (kqueue)
#elif defined(_WIN32)
    // Windows-specific
#else
    // Portable fallback (poll)
#endif
```

CMake sets `PLATFORM_LIBS` automatically:
- Linux/macOS: `pthread`
- Windows: `ws2_32 bcrypt` (`bcrypt` provides `BCryptGenRandom()` for `secure_random_bytes()`)
- Emscripten/WASM: empty — no platform libraries are linked

## Coding Conventions

### Naming and Style

- **Functions**: `snake_case` → `http_server_create()`, `event_loop_run()`
- **Types**: `snake_case_t` suffix → `http_server_t`, `json_value_t`
- **Enums**: `UPPER_SNAKE_CASE` → `HTTP_GET`, `HTTP_OK`, `EVENT_READ`
- **Internal/Static**: Prefix `_` → `_internal_helper()`
- **Indentation**: 4 spaces (no tabs)
- **Braces**: K&R style (opening brace on same line)

### Memory Management Patterns

**Critical**: All allocations must have corresponding frees. Common patterns:

```c
// Server lifecycle
http_server_t *server = http_server_create();  // malloc
// ... use server ...
http_server_destroy(server);                   // free

// JSON lifecycle
json_value_t *json = json_object_create();
json_object_set(json, "key", json_string_create("value"));
http_response_send_json(res, HTTP_OK, json);
json_value_free(json);  // MUST free after use

// Router lifecycle
router_t *router = router_create();
router_add_route(router, HTTP_GET, "/", handler);
http_server_set_router(server, router);
// ... 
router_destroy(router);  // Separate from server
```

### Error Handling Pattern

Return codes follow POSIX convention:
- Success: `0`
- Failure: `-1` (check `errno` for system errors)
- Pointers: `NULL` on failure

```c
int result = http_server_listen(server, port);
if (result < 0) {
    perror("Failed to start server");
    return 1;
}
```

### Route Handler Signature

All route handlers must match this signature:

```c
void handler_name(http_request_t *req, http_response_t *res) {
    // Extract data
    const char *param = http_request_get_param(req, "id");
    const char *header = http_request_get_header(req, "Content-Type");
    
    // Send response (marks res->sent = true internally)
    http_response_send_text(res, HTTP_OK, "Response body");
    // OR
    json_value_t *json = json_object_create();
    http_response_send_json(res, HTTP_OK, json);
    json_value_free(json);
}
```

### Middleware Pattern

Middleware chains execute before route handlers. The typedef is
`middleware_fn_t` and it takes **three** parameters — the third is the `void *user_data`
registered alongside the function, and it is `NULL` when you register with plain
`router_use_middleware()`:

```c
bool middleware_name(http_request_t *req, http_response_t *res, void *user_data) {
    (void)user_data;  // NULL unless registered via router_use_middleware_with_data()

    // Modify request/response
    http_response_set_header(res, "X-Custom", "value");

    // Control flow
    return true;   // Continue to next middleware/handler
    return false;  // Stop processing (e.g., auth failure)
}

// Registration (executes in order added)
router_use_middleware(router, logging_middleware);          // user_data == NULL
router_use_middleware(router, cors_middleware);
router_use_middleware_with_data(router, auth_middleware, &config);  // user_data == &config
```

## Common Implementation Patterns

### Adding New Routes

When adding endpoints, follow the example pattern:

1. Define handler function
2. Register in `main()` with `router_add_route()`
3. Use `http_request_get_param()` for URL parameters
4. Always send response via `http_response_send_text()` or `http_response_send_json()`

### Async I/O Implementation

When working with async mode:

1. Call `http_server_set_async(server, true)` before `http_server_listen()`
2. Get event loop: `event_loop_t *loop = http_server_get_event_loop(server)`
3. The server internally uses event callbacks for read/write operations
4. Signal handlers should call `event_loop_stop(loop)` for graceful shutdown

### Platform-Specific Features

When adding platform-specific code:

1. Use `#ifdef` guards matching existing patterns
2. Provide fallback implementation for unsupported platforms
3. Test on Linux, macOS, and Windows (if possible)
4. Document platform limitations in code comments

## Testing Strategy

Custom test framework (no external libraries like CUnit):

```c
// Test structure
void test_feature_name(void) {
    TEST("feature_name");
    
    // Setup
    feature_t *f = feature_create();
    ASSERT(f != NULL);
    
    // Test logic
    ASSERT(some_condition);
    
    // Cleanup
    feature_destroy(f);
    
    PASS();
}
```

Adding a case to an existing test file means writing the function and calling it from that file's `main()` — no CTest change needed. Adding a **new test file** does need a `add_executable()` + `add_test()` pair in `tests/CMakeLists.txt`, otherwise it never runs.

Registered ctest suites:

- **Default build (TLS off) — 6:** `WebLibTests`, `KamranHeaderTests`, `AsyncWebSocketTests`, `StressTests`, `WorkerTests`, `WasmTests`.
- **With `-DWEBLIB_ENABLE_TLS=ON -DWEBLIB_TLS_TEST_HOOKS=ON` — 13:** those six plus `TlsTests`, `TlsCryptoTests`, `TlsParseTests`, `TlsTransportTests`, `TlsFuzzTests`, `TlsHttpTests`, `TlsInteropOpenssl`.

Both configurations pass 100%. `test_weblib` self-reports `Tests run: 166`.

Two suites need their prerequisites understood before you trust a green run:

- `TlsHttpTests` is built only when `WEBLIB_TLS_TEST_HOOKS` is on. Otherwise it does not exist, and nothing tells you it is missing.
- `TlsInteropOpenssl` runs the `tls_server` example against a real `openssl s_client`, so it is registered only when that example is built (`BUILD_EXAMPLES=ON`) and a `bash` is found. Even then it exits 0 with `SKIP:` when `openssl` is absent, is LibreSSL rather than OpenSSL, has no `-tls1_3`, or lacks Ed25519.

A skip is not a pass, and a suite that was never registered is not a pass either. Read the ctest summary line, not just the exit code.

## Integration Points

### Server-Router Interaction

The server holds a pointer to the router but doesn't own it:

```c
http_server_t *server = http_server_create();
router_t *router = router_create();
http_server_set_router(server, router);  // Server stores reference only
// Must destroy both separately
router_destroy(router);
http_server_destroy(server);
```

### Event Loop Integration

In async mode, the server creates and manages the event loop internally:
- `http_server_set_async()` → creates `event_loop_t`
- `http_server_listen()` → runs event loop (blocking)
- `event_loop_stop()` → breaks from loop
- `http_server_destroy()` → destroys event loop

Don't create separate event loops unless implementing custom async patterns.

## Key Files Reference

- **API Surface**: `include/kamran.k` - all public declarations
- **Examples**: `examples/simple_server.c` (threaded), `examples/async_server.c` (event loop), `examples/tls_server.c` (TLS termination), `examples/worker_example.c` (Cloudflare Workers)
- **Build Config**: `CMakeLists.txt` - platform detection, compiler flags, the `WEBLIB_ENABLE_TLS` / `WEBLIB_TLS_TEST_HOOKS` options
- **CI**: `.github/workflows/ci.yml` - the five gates a PR must clear
- **TLS design & threat model**: `src/tls/README.md` - the authoritative document for anything cryptographic
- **Worker API**: `docs/WORKER_API.md` - the Cloudflare Workers surface
- **Roadmap**: `TODO.md` - planned features prioritized by 🎯/🔧/💡
- **Contributing**: `CONTRIBUTING.md` - detailed style guide and philosophy

## Common Pitfalls to Avoid

1. **Never suggest external dependencies** - implement in pure C
2. **Don't mix async/threaded patterns** - server uses one mode at a time
3. **Memory leaks**: Always pair create/destroy, especially JSON objects
4. **Platform assumptions**: Test `#ifdef` guards on multiple platforms
5. **Buffer sizes**: Respect `READ_BUFFER_SIZE=8192` and static array limits
6. **Response handling**: Don't send response multiple times (check `res->sent`)
7. **Threading**: Only threaded mode uses `pthread`, async mode is single-threaded
8. **WASM-safety**: New source files doing OS I/O go in `WEBLIB_SOURCES_NATIVE_ONLY`, not the WASM-safe list
9. **TLS maturity**: Never describe the TLS layer as production-ready, audited, or browser-compatible — it is none of those. And never present `http_server_enable_tls()` as taking file paths; it takes PEM buffers plus lengths
10. **TLS test hooks**: Never suggest enabling `WEBLIB_TLS_TEST_HOOKS` outside a test build

## When Adding New Features

Before implementing new functionality:

1. Check `TODO.md` for planned approach and priority
2. Verify it can be implemented in pure C without dependencies
3. Consider platform portability (Linux/macOS/Windows)
4. Write tests following existing test patterns
5. Add example usage to `examples/` if user-facing
6. Update `README.md` API reference section
7. Follow memory management patterns (create/destroy pairing)
8. Check it still builds for WASM if the code is placed in the WASM-safe source list
9. If it touches `src/tls/`, run both the TLS and the ASan/UBSan configurations before proposing it

## Quick Reference

**Start server**: `http_server_create()` → `http_server_set_router()` → `http_server_listen()` → cleanup  
**Add route**: `router_add_route(router, method, path, handler)`  
**JSON creation**: `json_object_create()` → `json_object_set()` → `json_value_free()`  
**Async mode**: `http_server_set_async(server, true)` before `http_server_listen()`  
**TLS (opt-in, experimental)**: `http_server_enable_tls(server, cert_pem, cert_len, key_pem, key_len)` before `http_server_listen()`; threaded mode only, native only, unaudited  
**Event loop backends**: epoll (Linux) > kqueue (BSD/macOS) > poll (fallback)  
**Static limits**: 128 connections, 256 routes, 32 middlewares, 1024 events, 64 timers  
**Build flags**: `WEBLIB_ENABLE_TLS` (OFF), `WEBLIB_TLS_TEST_HOOKS` (OFF, tests only), `BUILD_EXAMPLES` (ON), `BUILD_TESTS` (ON)
