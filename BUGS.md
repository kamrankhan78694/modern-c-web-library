# Known Bugs & Issues

**Modern C Web Library — Production Stress Test Findings**

*Last Updated: 2026-07-29*
*BUG-1..10 discovered during Phase 10 production-level stress testing and Phase 10.1 security code
review; BUG-11..14 during the demo-app / response-hook work (PR #137)*

**Scope:** this file tracks the ten issues found by the Phase 10 stress-testing and security-review
pass, plus the four found while building the dev server and end-to-end suite. Thirteen are fully
resolved; BUG-4 (middleware singleton state) is only partially fixed — see the summary table below.
Two things it deliberately does *not* cover:

- **Later security-review findings.** The post-v1.0.0 audit stream (roughly PRs #74–#95 — request
  smuggling, path aliasing, Host-header bypass, session use-after-free, the async idle reaper, and
  others) is fixed in code and recorded in the commit history and PR discussions, not re-litigated
  here. Read this file as a closed historical record, not as the complete list of everything ever
  found.
- **The experimental TLS layer** (`src/tls/`, off by default). Its known gaps live in
  [`src/tls/README.md`](src/tls/README.md) — that code has not had an external audit.

---

## Bug Summary

| # | Severity | Component | Status | Description |
|---|----------|-----------|--------|-------------|
| 1 | **Critical** | HTTP Server | **Fixed** | No SIGPIPE handling — server process can crash on client disconnect |
| 2 | **High** | Session Store | **Fixed** | Session store is not thread-safe — data races in multi-threaded mode |
| 3 | **Medium** | Session / CSRF | **Fixed** | `rand()` fallback is not thread-safe and cryptographically weak |
| 4 | **Low** | Middleware | **Partially fixed** | Global singletons — one instance per type. `user_data` plumbing landed; 4 of 9 modules read it (only 3 usable per-instance), 5 still ignore it |
| 5 | **Low** | Event Loop | **Fixed** | Timer limit of 64 is hard-coded with no runtime feedback beyond -1 return |
| 6 | **Info** | HTTP Server | **Fixed** | No HTTP keep-alive connection limit — could exhaust file descriptors |
| 7 | **Medium** | env_config / security_utils | **Fixed** | Duplicate `_secure_wipe()` — private copy of public `secure_zero()` |
| 8 | **Medium** | Security Headers Middleware | **Fixed** | Shallow `memcpy` of config struct containing string pointers (dangling pointer risk) |
| 9 | **Medium** | CSRF / Session / Auth | **Fixed** | Private functions duplicate public security APIs (`secure_random_bytes`, `secure_compare`) |
| 10 | **Low** | Auth / CSRF Middleware | **Fixed** | `memset()` used instead of `secure_zero()` to wipe sensitive data — compiler may optimize away |
| 11 | **Low** | HTTP Response | **Fixed** | `http_response_send_text()` *appended* `Content-Type` instead of replacing it — now replaces, and `http_response_send_html()` exists |
| 12 | **Low** | Router / Request | **Fixed** | Route parameters allocated by `router_route()` had no public free — `http_request_clear_params()` is now exported |
| 13 | **Low** | HTTP Server (shutdown) | **Fixed** | `http_server_stop()` closed `server->socket_fd` while the accept thread might still pass it to `accept()` — replaced by a wake pipe + join-before-close |
| 14 | **Low** | Test infrastructure | **Fixed** | `tests/test_stress.c` bound 15 hardcoded ports — now binds port 0 and reads back via `http_server_port()` |

---

## Detailed Bug Reports

### BUG-1: No SIGPIPE Handling (Critical) — ✅ FIXED

**Component:** `src/http_server.c`
**Severity:** Critical — can terminate the entire server process
**Discovered:** Stress test analysis of `send()` calls
**Fixed:** Added `signal(SIGPIPE, SIG_IGN)` in `http_server_create()` and `MSG_NOSIGNAL` flag on all `send()` calls via a portable `SEND_FLAGS` macro.

**Description:**
The HTTP server calls `send()` with flags=0 on all socket writes. On POSIX systems, writing to a socket whose peer has disconnected generates a `SIGPIPE` signal. The default action for `SIGPIPE` is to terminate the process. This means a single misbehaving client disconnecting at the wrong time can crash the entire server.

**Affected Lines (as of the original report — all three now pass `SEND_FLAGS`):**
- `src/http_server.c:895` — `send(fd, buf + sent_total, len - sent_total, 0)`
- `src/http_server.c:2017` — async mode header send
- `src/http_server.c:2045` — async mode body send

**Reproduction:**
1. Start the server with multiple connected clients
2. Have one client abruptly close its connection during a large response write
3. The server process may receive SIGPIPE and terminate

**Recommended Fix:**
Either set `MSG_NOSIGNAL` flag on all `send()` calls (Linux), or install a process-wide `signal(SIGPIPE, SIG_IGN)` handler at server startup. On macOS, `SO_NOSIGPIPE` socket option can also be used.

```c
// Option A: Per-send flag (Linux)
send(fd, buf, len, MSG_NOSIGNAL);

// Option B: Process-wide ignore (portable)
signal(SIGPIPE, SIG_IGN);
```

---

### BUG-2: Session Store Not Thread-Safe (High) — ✅ FIXED

**Component:** `src/session.c`
**Severity:** High — data races under concurrent access
**Discovered:** Code analysis during stress testing
**Fixed:** Added `pthread_mutex_t` to `session_store_t` with lock/unlock around all session store operations (`session_create`, `session_get`, `session_destroy`, `session_cleanup_expired`, `session_store_destroy`).

**Description:**
The session store (`session_store_t`) has no mutex or synchronization mechanism. In threaded HTTP server mode (the default), multiple worker threads can simultaneously call `session_create()`, `session_get()`, `session_set_data()`, `session_destroy()`, and `session_cleanup_expired()`. This can cause:

- Duplicate session ID assignment
- Use-after-free of session data
- Corrupted linked list of session data entries
- Race conditions on `session_count` updates

**Comparison with other components:**
- `cache.c` — uses `pthread_mutex_t` ✓
- `middleware_ratelimit.c` — uses `pthread_mutex_t` ✓
- `middleware_metrics.c` — uses `pthread_mutex_t` ✓
- `session.c` — **no synchronization** ✗

**Recommended Fix:**
Add a `pthread_mutex_t` to `session_store_t` and lock/unlock around all session operations, following the same pattern used in `cache.c`.

---

### BUG-3: `rand()` Fallback Not Thread-Safe (Medium) — ✅ FIXED

**Component:** `src/session.c`, `src/middleware_csrf.c`
**Severity:** Medium — affects security in edge cases
**Discovered:** Code analysis during stress testing
**Fixed:** Initially addressed in commit `9a89c30` by swapping `srand()`/`rand()` for `rand_r()` with per-function seed state. That intermediate fix has since been superseded by PR #76 (`6895aab`, "Harden CSPRNG and remove predictable-entropy fallbacks"): the predictable-PRNG fallback was removed outright. `generate_session_id()` (`src/session.c`) and the CSRF token generator `_generate_token()` (`src/middleware_csrf.c`) now call the public `secure_random_bytes()` and **fail closed** — `session_create()` refuses to issue a session and `_generate_token()` returns `NULL` if the OS CSPRNG is unavailable. No `rand()`/`rand_r()` path remains anywhere in `src/`.

**Description:**
Both session ID generation and CSRF token generation use `/dev/urandom` as the primary randomness source, which is correct. However, the fallback path uses `srand()` and `rand()`, which:

1. Are not thread-safe — multiple threads calling `rand()` simultaneously can produce identical values
2. Are seeded with predictable values (`time(NULL) ^ clock()`) making tokens guessable
3. Could lead to session ID collisions in multi-threaded operation

**Affected Files (as of the original report, pre-`6895aab`):**
- `src/session.c` — `generate_session_id()` fallback
- `src/middleware_csrf.c` — `_csrf_random_bytes()` fallback (function no longer exists)

**Mitigation (historical):**
On Linux, `/dev/urandom` is always available, so this fallback rarely triggered. The risk was primarily on embedded or unusual systems without `/dev/urandom`. It no longer applies: there is no fallback path to reach.

**Recommended Fix (superseded):**
The original recommendation was `rand_r()` or per-thread seed storage. The fix actually adopted was stronger — remove the weak fallback entirely and fail closed on CSPRNG failure, so a host without usable entropy gets no token rather than a guessable one.

---

### BUG-4: Middleware Singleton Pattern Limitation (Low) — ⚠️ PARTIALLY FIXED

**Component:** All middleware implementations
**Severity:** Low — design limitation, not a crash bug
**Discovered:** Architecture analysis during stress testing
**Partially fixed:** Added `void *user_data` parameter to `middleware_fn_t` signature and router infrastructure, plus a `router_use_middleware_with_data()` API for registering middleware with per-instance state. Four modules read it — CORS (`src/middleware_cors.c`), rate-limit (`src/middleware_ratelimit.c`), auth (`src/middleware_auth.c`) and logging (`src/middleware_log.c`) — using the `user_data ? user_data : g_global` pattern, so they keep backward compatibility when `user_data` is NULL. **Only three of those are usable per-instance**, however: CORS, auth and logging take a pointer to their own *public* config struct (`cors_options_t *`, `basic_auth_config_t *` / `apikey_auth_config_t *` / `jwt_auth_config_t *`, `log_config_t *`), which a caller can allocate. Rate limiting casts `user_data` to `ratelimiter_t *` — an internal *state* struct (mutex + hash table) defined only in `src/middleware_ratelimit.c` and absent from `include/kamran.k` — while its only public constructor `ratelimit_middleware_create()` returns a `middleware_fn_t` and keeps the limiter in the file-static `g_ratelimiter`. No public API yields a second one, so rate limiting is one instance per type in practice, and passing a `ratelimit_config_t *` there is type confusion that will corrupt memory, not configuration. **Five modules still discard it** (each begins `(void)user_data;` and reads only its module global): static files (`src/middleware_static.c`), CSRF (`src/middleware_csrf.c`), error handler (`src/middleware_error.c`), metrics (`src/middleware_metrics.c`) and security headers (`src/middleware_security_headers.c`). Those five remain strictly one instance per type — registering two silently gives both the last-configured global. See [docs/TECHNICAL_DEBT.md](docs/TECHNICAL_DEBT.md) §3 for the per-route workaround.

**Description:**
All middleware types (CORS, rate limiting, static files, auth, logging, error handler, CSRF, metrics) use global static variables to store their configuration. This means only one instance of each middleware type can exist at a time. Calling `*_middleware_create()` a second time overwrites the first configuration.

**Impact:**
- Cannot have different rate limits for different route groups
- Cannot configure multiple CORS policies for different API versions
- Cannot have both public and private auth configurations active simultaneously

**Current Pattern:**
```c
static cors_options_t *g_cors_config = NULL;    // middleware_cors.c
static ratelimiter_t *g_ratelimiter = NULL;      // middleware_ratelimit.c
```

**Root Cause:**
The `middleware_fn_t` function signature `bool (*)(http_request_t*, http_response_t*)` doesn't support a user_data parameter, making per-instance state impossible without global variables.

---

### BUG-5: Hard-Coded Timer Limit (Low) — ✅ FIXED

**Component:** `src/event_loop.c`
**Severity:** Low — fails silently beyond documentation
**Discovered:** Code analysis during stress testing
**Fixed:** Improved error message to include current/max timer count. Added `event_loop_get_timer_count()` and `event_loop_get_max_timers()` API functions for runtime introspection.

**Description:**
The event loop has a hard-coded `MAX_TIMERS=64` limit. When this limit is reached, `event_loop_add_timeout()` returns -1, but there's no way to query the current timer count or adjust the limit. Applications using many timers (e.g., per-connection timeouts) could silently fail to set timers.

---

### BUG-6: No Keep-Alive Connection Limit (Info) — ✅ FIXED

**Component:** `src/http_server.c`
**Severity:** Info — potential resource exhaustion under sustained load
**Discovered:** Stress test analysis
**Fixed:** Added active connection tracking with `pthread_mutex_t`-protected counter. New connections are rejected with 503 when the limit is reached. Added `http_server_set_max_connections()` and `http_server_get_active_connections()` API functions.

**Description:**
The HTTP server supports HTTP/1.1 keep-alive connections, but there is no limit on the number of simultaneous keep-alive connections. A slow-loris style attack or simply many persistent connections could exhaust the server's file descriptor limit.

The `MAX_CONNECTIONS=128` backlog limit only applies to the `listen()` queue, not to the total number of active connections.

**Recommended Fix:**
Track active connection count and reject new connections when a configurable maximum is reached.

---

### BUG-7: Duplicate `_secure_wipe()` in env_config.c (Medium) — ✅ FIXED

**Component:** `src/env_config.c`
**Severity:** Medium — code duplication of security-critical function
**Discovered:** Code review of PR #61
**Fixed:** Removed `_secure_wipe()` and replaced calls with public `secure_zero()`.

**Description:**
`env_config.c` contained a private `_secure_wipe()` function that was an exact reimplementation of the public `secure_zero()` from `security_utils.c`. The `env_secure_value_free()` function called this internal duplicate instead of the public API.

**Risk:**
If one implementation is updated (e.g., to use a platform-specific intrinsic) while the other is not, sensitive data may not be properly wiped in one code path.

**Root Cause:**
The secure value API was developed before or alongside `secure_zero()`, and the author did not refactor to reuse the public function.

---

### BUG-8: Shallow Config Copy in Security Headers Middleware (Medium) — ✅ FIXED

**Component:** `src/middleware_security_headers.c`
**Severity:** Medium — potential use-after-free / dangling pointer
**Discovered:** Code review of PR #61
**Fixed:** Deep-copy all string fields with `strdup()` in `security_headers_middleware_create()` and free them in `security_headers_middleware_destroy()`, with per-field allocation failure handling.

**Description:**
`security_headers_middleware_create()` used `memcpy()` to copy the entire `security_headers_config_t` struct, which only copies string pointers, not the string data. If the caller provides strings from local/dynamic storage that is later freed or goes out of scope, the middleware holds dangling pointers.

**Comparison with other middleware:**
- `middleware_cors.c` — deep-copies strings with `strdup()` ✓
- `middleware_auth.c` — deep-copies strings with `strdup()` ✓
- `middleware_static.c` — deep-copies strings with `strdup()` ✓
- `middleware_security_headers.c` — **shallow `memcpy`** ✗ (now fixed)

---

### BUG-9: Private Functions Duplicate Public Security APIs (Medium) — ✅ FIXED

**Component:** `src/middleware_csrf.c`, `src/session.c`, `src/middleware_auth.c`
**Severity:** Medium — code duplication of security-critical functions
**Discovered:** Codebase-wide audit following PR #61 bug fixes
**Fixed:** Removed all private reimplementations and replaced with calls to the public APIs: `_fill_random()` → `secure_random_bytes()`, `generate_session_id()` → `secure_random_bytes()`, `_ct_strcmp()` → `secure_compare()`, `_auth_secure_compare()` → `secure_compare()`.

**Description:**
Three security-critical operations have public APIs in `security_utils.c`, but multiple modules reimplemented them privately. The file:line references below are as of the original report; none of these private functions exists in `src/` today.

**1. Random byte generation (`secure_random_bytes` duplicated):**
- `src/middleware_csrf.c:53-84` — `_fill_random()` reimplements `/dev/urandom` reading + fallback
- `src/session.c:38-80` — `generate_session_id()` reimplements `/dev/urandom` reading + fallback

**2. Constant-time comparison (`secure_compare` duplicated):**
- `src/middleware_csrf.c:116-128` — `_ct_strcmp()` reimplements XOR-based comparison
- `src/middleware_auth.c:452-462` — `_auth_secure_compare()` reimplements XOR-based comparison

**Risk:**
- If a bug is found in one implementation, the duplicates may not be fixed
- Subtle differences between implementations (e.g., `_ct_strcmp` handles unequal-length strings differently than `secure_compare`) can introduce security inconsistencies
- Increases maintenance burden and code review surface

**Recommended Fix:**
Refactor all private implementations to call the public API:
- `_fill_random()` → call `secure_random_bytes()`
- `generate_session_id()` → call `secure_random_bytes()`
- `_ct_strcmp()` → call `secure_compare()`
- `_auth_secure_compare()` → call `secure_compare()`

---

### BUG-10: `memset()` Used to Wipe Sensitive Data (Low) — ✅ FIXED

**Component:** `src/middleware_auth.c`, `src/middleware_csrf.c`
**Severity:** Low — compiler may optimize away the wipe
**Discovered:** Codebase-wide audit following PR #61 bug fixes
**Fixed:** Replaced all `memset()` calls on sensitive data with `secure_zero()` which uses `volatile` pointers to prevent dead-store elimination.

**Description:**
Several locations use `memset(ptr, 0, len)` to wipe sensitive data before freeing. The C standard permits compilers to optimize away `memset()` calls on memory that is not subsequently read ("dead store elimination"). The public `secure_zero()` function exists specifically to prevent this optimization using `volatile` pointers.

**Affected Locations (as of the original report — all three now call `secure_zero()`):**
- `src/middleware_auth.c:527` — `memset(decoded, 0, sizeof(decoded))` — wipes Base64-decoded credentials
- `src/middleware_auth.c:935` — `memset((void *)g_jwt_auth_config->secret, 0, ...)` — wipes JWT secret before free
- `src/middleware_csrf.c:221` — `memset(&g_csrf_state, 0, sizeof(g_csrf_state))` — wipes CSRF state

**Recommended Fix:**
Replace `memset()` with `secure_zero()` for all security-sensitive wipe operations:
```c
// Before (may be optimized away):
memset(decoded, 0, sizeof(decoded));

// After (guaranteed wipe):
secure_zero(decoded, sizeof(decoded));
```

---

### BUG-11: `http_response_send_text()` appends `Content-Type` (Low) — ✅ FIXED

`http_response_send_text()` added its `Content-Type` with `replace_existing = false`, so it appended
rather than replaced. A handler that sent text and then set a different content type emitted **two**
`Content-Type` headers, which is invalid per RFC 9110 and which browsers resolve by rendering as
plain text.

**Fixed:** `send_text` now replaces, and `http_response_send_html()` exists so serving HTML no
longer means sending text and correcting the header afterwards — which was exactly the sequence that
triggered this. `http_response_send_template()` now sends `text/html` (rendered templates are HTML;
callers previously had to correct that header too). `examples/demo_app.c` dropped its
set-header-after-send workaround. Regression tests assert exactly one `Content-Type` and were
verified to fail against the appending behaviour; the end-to-end suite independently asserts one
`Content-Type` on `GET /`.

### BUG-12: no public way to free route parameters (Low) — ✅ FIXED

`router_route()` stores route parameters on the request via `http_request_set_param()`, which
allocates a node, a key and a value. They were released only by `param_list_free()` from
`http_request_destroy()` — and neither was exported. The HTTP server owns the request lifecycle, so
a normal server was unaffected; code driving the router directly (a Cloudflare Worker handler, an
embedder, a test) leaked three allocations per parameterised request.

**Fixed:** `http_request_clear_params()` is public. `tests/test_weblib.c` deleted its
`_test_free_param_list()` mirror of the internal node layout — the copy that would have rotted when
the struct changed — and calls the real API.

### BUG-13: `socket_fd` closed from another thread during shutdown (Low) — ✅ FIXED

In threaded mode `http_server_stop()` called `shutdown()`, `close()`, then set
`server->socket_fd = -1` from the caller's thread, while the accept thread might be inside — or
about to enter — `accept(server->socket_fd)`. Closing the descriptor was what unblocked `accept()`,
so the close was deliberate; the hazard was fd reuse: between `close()` and the next `accept()`,
another thread's `open()` could be handed the same descriptor number, and `accept()` would run on an
unrelated file.

**Fixed** with the self-pipe pattern already used by the keep-alive dispatcher: the accept thread
polls on the listen socket and a wake pipe, the stopper writes a byte and **joins before anything is
closed**, so no descriptor is ever invalidated while another thread could still use it. The listen
socket is non-blocking so a connection reset between `poll()` and `accept()` returns `EAGAIN`
instead of blocking where the pipe cannot reach (accepted sockets are restored to blocking —
BSD-family kernels inherit the flag, Linux does not). `http_server_shutdown()` had the same race
and got the same reordering.

### BUG-14: `test_stress.c` uses hardcoded ports with no bind retry (Low) — ✅ FIXED

`tests/test_stress.c` bound fifteen fixed ports (19000–19016), each with a bare
`ASSERT(http_server_listen(server, port) == 0)`. `SO_REUSEADDR` was set, but running the suite
repeatedly in quick succession left enough sockets in `TIME_WAIT` that a bind occasionally failed —
observed at roughly 1 run in 3 re-running `ctest` back to back — and the test then reported a
product failure for an environment condition.

**Fixed:** every site binds port 0 and reads the kernel-assigned port back through the new
`http_server_port()` accessor, removing the shared port namespace outright rather than making
collisions rarer. Verified with three consecutive full-suite runs. (The restart test re-reads the
port each cycle, since every re-listen gets a fresh ephemeral port.)

---

## Stress Test Results Summary

All 38 stress tests pass with zero failures and zero **definite or indirect** memory leaks. The leak
check runs on every push in the `Build, Test & Memcheck (Docker)` CI job, which executes every test
binary under Valgrind on Ubuntu with
`--errors-for-leak-kinds=definite,indirect` and fails the job if any of them reports one. *Possible*
and *still-reachable* blocks are displayed but are not gated:

```
Tests run: 37
Tests passed: 37
Tests failed: 0

Valgrind: 0 errors, 0 definite leaks, 0 indirect leaks
```


### What Passed Cleanly

| Area | Test | Result |
|------|------|--------|
| Router | 256 routes (MAX_ROUTES) | ✓ Handled correctly, 257th rejected |
| Router | 32 middlewares (MAX_MIDDLEWARES) | ✓ Handled correctly, 33rd rejected |
| Router | 4KB paths | ✓ No buffer overflow |
| Router | 20 path parameters | ✓ All matched correctly |
| JSON | 511-depth nesting | ✓ Parsed successfully |
| JSON | 513-depth nesting | ✓ Correctly rejected (NULL) |
| JSON | 1000-key objects | ✓ Stringify + re-parse correct |
| JSON | 10,000-element arrays | ✓ Stringify + re-parse correct |
| JSON | 100KB strings | ✓ No issues |
| JSON | 22 malformed inputs | ✓ All returned NULL, no crashes |
| JSON | 10,000 parse/free cycles | ✓ No memory leaks |
| Cache | 500 entries in 100-max cache | ✓ LRU eviction works correctly |
| Cache | 10,000 rapid operations | ✓ All consistent |
| Cache | TTL expiration | ✓ Accurate within 1 second |
| Sessions | 1024 mass creation | ✓ Limit enforced, extra returns NULL |
| Sessions | 100 key-value pairs per session | ✓ All retrievable |
| Sessions | Expiry cleanup | ✓ Expired sessions cleaned |
| HTTP | 100 rapid sequential requests | ✓ ≥85% success rate |
| HTTP | 20 concurrent requests (5×4) | ✓ ≥80% success rate |
| HTTP | 100KB request body | ✓ Processed correctly |
| HTTP | >1MB body rejection | ✓ Rejected with 413/400 |
| HTTP | 90 headers | ✓ All accepted |
| HTTP | Slow client (1s delay) | ✓ Completed within timeout |
| HTTP | Slow-loris drip that never completes a request | ✓ Cut off by the total request deadline, not just the per-`recv()` timeout |
| HTTP | Partial request then silence, per-`recv()` timeout disabled | ✓ Still bounded — the effective read timeout is capped at the deadline |
| HTTP | `listen()` failing after startup | ✓ Server state reset; `destroy()` stays safe (test skips if the failure can't be forced) |
| HTTP | Transfer-Encoding smuggling matrix | ✓ Token-parsed per RFC 7230 §3.3.1: body de-chunked, smuggling vectors → 400, unsupported coding → 501 |
| HTTP | Control bytes in the request-target (CR, LF, HTAB, C0, DEL) | ✓ Rejected with 400 — no CRLF injection into `req->path` |
| HTTP | Host header enforcement | ✓ Keyed on the HTTP version, not the `Connection` header |
| HTTP | Path canonicalization before routing | ✓ `//` collapsed, trailing `/` stripped; literal and `:param` routes agree |
| HTTP (async) | Idle / slow-loris connection on the event loop | ✓ Reaped once the request deadline passes; shutdown latency stays bounded |
| HTTP (async) | Re-listen after stop | ✓ Event loop torn down cleanly; the second `listen()` succeeds |
| Validation | 100KB string validation | ✓ Correct results |
| Validation | 1000 script tag sanitization | ✓ All escaped |
| Compression | 1MB CRC32 | ✓ No issues |
| Lifecycle | 100× server create/destroy | ✓ No leaks |
| Lifecycle | 100× router create/destroy | ✓ No leaks |
| Lifecycle | 100× event loop create/destroy | ✓ No leaks |

---

## Previously Fixed Issues

The following issues were found and fixed during earlier development phases:

- ✅ **Buffer overflows** — All `sprintf` replaced with `snprintf` (Phase 3)
- ✅ **JSON depth limit** — Added `JSON_MAX_DEPTH=512` to prevent stack overflow
- ✅ **HTTP parser hardening** — Duplicate Transfer-Encoding detection, Content-Length validation
- ✅ **Request size limits** — `MAX_BODY_BYTES=1MB`, `MAX_HEADER_BYTES=16KB`
- ✅ **Thread pool** — Replaced unbounded thread-per-connection model (Phase 7)
- ✅ **Graceful shutdown** — Server state machine with drain timeout (Phase 7)

---

**Maintainer:** [@kamrankhan78694](https://github.com/kamrankhan78694)
