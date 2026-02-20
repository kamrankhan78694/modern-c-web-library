# Known Bugs & Issues

**Modern C Web Library — Production Stress Test Findings**

*Last Updated: 2026-02-20*
*Discovered during Phase 10 production-level stress testing*

---

## Bug Summary

| # | Severity | Component | Status | Description |
|---|----------|-----------|--------|-------------|
| 1 | **Critical** | HTTP Server | Open | No SIGPIPE handling — server process can crash on client disconnect |
| 2 | **High** | Session Store | Open | Session store is not thread-safe — data races in multi-threaded mode |
| 3 | **Medium** | Session / CSRF | Open | `rand()` fallback is not thread-safe and cryptographically weak |
| 4 | **Low** | Middleware | Open | All middleware types use global singletons — only one instance per type |
| 5 | **Low** | Event Loop | Open | Timer limit of 64 is hard-coded with no runtime feedback beyond -1 return |
| 6 | **Info** | HTTP Server | Open | No HTTP keep-alive connection limit — could exhaust file descriptors |

---

## Detailed Bug Reports

### BUG-1: No SIGPIPE Handling (Critical)

**Component:** `src/http_server.c`
**Severity:** Critical — can terminate the entire server process
**Discovered:** Stress test analysis of `send()` calls

**Description:**
The HTTP server calls `send()` with flags=0 on all socket writes. On POSIX systems, writing to a socket whose peer has disconnected generates a `SIGPIPE` signal. The default action for `SIGPIPE` is to terminate the process. This means a single misbehaving client disconnecting at the wrong time can crash the entire server.

**Affected Lines:**
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

### BUG-2: Session Store Not Thread-Safe (High)

**Component:** `src/session.c`
**Severity:** High — data races under concurrent access
**Discovered:** Code analysis during stress testing

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

### BUG-3: `rand()` Fallback Not Thread-Safe (Medium)

**Component:** `src/session.c`, `src/middleware_csrf.c`
**Severity:** Medium — affects security in edge cases
**Discovered:** Code analysis during stress testing

**Description:**
Both session ID generation and CSRF token generation use `/dev/urandom` as the primary randomness source, which is correct. However, the fallback path uses `srand()` and `rand()`, which:

1. Are not thread-safe — multiple threads calling `rand()` simultaneously can produce identical values
2. Are seeded with predictable values (`time(NULL) ^ clock()`) making tokens guessable
3. Could lead to session ID collisions in multi-threaded operation

**Affected Files:**
- `src/session.c:66-75` — `generate_session_id()` fallback
- `src/middleware_csrf.c:66-82` — `_csrf_random_bytes()` fallback

**Mitigation:**
On Linux, `/dev/urandom` is always available, so this fallback rarely triggers. The risk is primarily on embedded or unusual systems without `/dev/urandom`.

**Recommended Fix:**
Use `rand_r()` (thread-safe) or per-thread seed storage instead of global `rand()`.

---

### BUG-4: Middleware Singleton Pattern Limitation (Low)

**Component:** All middleware implementations
**Severity:** Low — design limitation, not a crash bug
**Discovered:** Architecture analysis during stress testing

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

### BUG-5: Hard-Coded Timer Limit (Low)

**Component:** `src/event_loop.c`
**Severity:** Low — fails silently beyond documentation
**Discovered:** Code analysis during stress testing

**Description:**
The event loop has a hard-coded `MAX_TIMERS=64` limit. When this limit is reached, `event_loop_add_timeout()` returns -1, but there's no way to query the current timer count or adjust the limit. Applications using many timers (e.g., per-connection timeouts) could silently fail to set timers.

---

### BUG-6: No Keep-Alive Connection Limit (Info)

**Component:** `src/http_server.c`
**Severity:** Info — potential resource exhaustion under sustained load
**Discovered:** Stress test analysis

**Description:**
The HTTP server supports HTTP/1.1 keep-alive connections, but there is no limit on the number of simultaneous keep-alive connections. A slow-loris style attack or simply many persistent connections could exhaust the server's file descriptor limit.

The `MAX_CONNECTIONS=128` backlog limit only applies to the `listen()` queue, not to the total number of active connections.

**Recommended Fix:**
Track active connection count and reject new connections when a configurable maximum is reached.

---

## Stress Test Results Summary

All 28 stress tests pass with zero failures and zero memory leaks (verified under Valgrind):

```
Tests run: 28
Tests passed: 28
Tests failed: 0

Valgrind: 0 errors, 0 leaks (393,359 allocs, 393,359 frees)
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
