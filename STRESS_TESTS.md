# Stress Test Report — Production Readiness Assessment

**Modern C Web Library**
*Originally written against v0.9.0 on 2026-02-20 · test counts and per-test descriptions below
re-checked against the v2.0.0 tree on 2026-07-27*

> **What changed since the original report.** The six issues this report first raised are all fixed
> (see [BUGS.md](BUGS.md), which now tracks ten issues, all closed). The suite itself has grown from
> 28 tests to 37 — nine of them HTTP security regressions added in PRs #78–#89. Counts, category
> tables and the readiness checklist below reflect the current suite, not the February snapshot.

---

## Executive Summary

A production-level stress test suite was developed and is run on every push against the Modern C Web
Library. The library demonstrates **stable behaviour and clean memory handling** across all tested
components, with zero memory leaks confirmed under Valgrind in CI. Six issues were identified by the
original run, documented in [BUGS.md](BUGS.md), ranging from Critical (SIGPIPE handling) to
Informational; all six were fixed before v1.0.0 shipped on 2026-02-22.

**Overall Assessment: suitable for production HTTP workloads on Linux** — the critical SIGPIPE bug
and the session thread-safety bug that gated the original assessment are both fixed. Note that this
report covers the plain-HTTP core only. It says nothing about the TLS layer added in v2.0.0, which
is **experimental, unaudited, off by default** (`WEBLIB_ENABLE_TLS=OFF`) and must not be treated as
production-ready — see [`src/tls/README.md`](src/tls/README.md).

---

## Test Environment

- **Platform:** Linux (Ubuntu), x86_64
- **Compiler:** GCC with `-Wall -Wextra -pedantic`
- **C Standard:** C11
- **Memory Checker:** Valgrind 3.x (leak-check=full)
- **Test Framework:** Custom (consistent with existing test_weblib.c)

## Running the Tests

```bash
# Build
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build --parallel

# Run stress tests
./build/tests/test_stress

# Run with Valgrind (Linux; the CI memcheck job does this inside Dockerfile.dev)
valgrind --leak-check=full ./build/tests/test_stress

# Run via CTest (subshell, so you stay in the repo root for the next line)
(cd build && ctest -R StressTests)

# Skip server integration tests (constrained environments)
SKIP_SERVER_TESTS=1 ./build/tests/test_stress
```

`StressTests` is one of the **6 ctest suites** in a default build (`WebLibTests`,
`KamranHeaderTests`, `AsyncWebSocketTests`, `StressTests`, `WorkerTests`, `WasmTests`). Building with
the experimental TLS layer adds seven more — `TlsTests`, `TlsCryptoTests`, `TlsParseTests`,
`TlsTransportTests`, `TlsFuzzTests`, `TlsHttpTests`, `TlsInteropOpenssl` — for **13 suites** total:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo \
      -DWEBLIB_ENABLE_TLS=ON -DWEBLIB_TLS_TEST_HOOKS=ON
cmake --build build --parallel
cd build && ctest --output-on-failure
```

Both flags matter. `-DWEBLIB_ENABLE_TLS=ON` alone gives you 12 suites: `TlsHttpTests` additionally
needs `-DWEBLIB_TLS_TEST_HOOKS=ON`, because it drives the server through a deterministic-RNG test
seam. That hooks flag must never be set in a production build. The CI `tls-check` job configures
with both and runs all 13. The TLS suites are outside the scope of this report.

---

## Test Results

### Summary

```
Total Tests:   37
Passed:        37
Failed:         0

Valgrind Results (CI, Ubuntu/gcc):
  Definite leaks:   0
  Indirect leaks:   0
  Errors:           0
```

The original report quoted exact heap figures (393,359 allocs, all freed; 0 bytes in use at exit).
Those were measured against the 28-test suite and are no longer accurate for 37 tests, so they have
been dropped rather than guessed at. What CI actually gates on — and what is stated above — is what
`valgrind --leak-check=full --show-leak-kinds=definite,indirect --error-exitcode=1` enforces: zero
errors and zero definite or indirect leaks, on every `tests/test_*` binary, on every push.

### Detailed Results by Category

#### 1. Router Stress Tests (4/4 passed)

| Test | Description | Result |
|------|-------------|--------|
| `router_max_routes` | Fill 256 routes, verify 257th rejected | ✅ Pass |
| `router_max_middlewares` | Fill 32 middlewares, verify 33rd rejected | ✅ Pass |
| `router_long_paths` | Route with ~4KB path | ✅ Pass |
| `router_many_params` | Route with 20 `:param` segments | ✅ Pass |

**Findings:** Router handles all limits correctly with proper error codes. No buffer overflows even with near-maximum path lengths.

#### 2. JSON Parser Stress Tests (6/6 passed)

| Test | Description | Result |
|------|-------------|--------|
| `json_deep_nesting` | 511-depth succeeds, 513-depth rejected | ✅ Pass |
| `json_large_object` | 1,000-key object: create → stringify → re-parse | ✅ Pass |
| `json_large_array` | 10,000-element array: create → stringify → re-parse | ✅ Pass |
| `json_large_string` | 100KB string value | ✅ Pass |
| `json_malformed_fuzzing` | 22 malformed inputs (truncated, extra commas, unbalanced) | ✅ Pass |
| `json_repeated_parse_free` | 10,000 parse/free cycles | ✅ Pass |

**Findings:** The JSON parser is remarkably robust. All malformed inputs return NULL without crashes. The depth limit (512) prevents stack overflow attacks. The 10K parse/free cycle confirms no memory leaks in the parser.

#### 3. Cache Stress Tests (3/3 passed)

| Test | Description | Result |
|------|-------------|--------|
| `cache_fill_eviction` | 500 entries in 100-max cache | ✅ Pass |
| `cache_rapid_set_get` | 10,000 rapid set/get operations | ✅ Pass |
| `cache_ttl_accuracy` | 1-second TTL entries expire after 2s | ✅ Pass |

**Findings:** LRU eviction works correctly under load. Cache maintains count invariant (never exceeds max_entries). TTL expiration is accurate within 1 second.

#### 4. Session Stress Tests (3/3 passed)

| Test | Description | Result |
|------|-------------|--------|
| `session_mass_create` | Create 1,024 sessions (MAX_SESSIONS) | ✅ Pass |
| `session_data_operations` | 100 key-value pairs on single session | ✅ Pass |
| `session_cleanup` | Expired session cleanup | ✅ Pass |

**Findings:** Session store correctly enforces the 1,024 limit. The thread-safety gap the original report noted (BUG-2) is fixed — `session_store_t` now carries a `pthread_mutex_t`.

#### 5. HTTP Server Integration Tests (15/15 passed)

| Test | Description | Result |
|------|-------------|--------|
| `rapid_connections` | 100 sequential HTTP requests | ✅ Pass (≥85% success) |
| `concurrent_connections` | 5 threads × 4 requests | ✅ Pass (≥80% success) |
| `large_body` | 100KB POST body | ✅ Pass |
| `oversized_request` | >1MB body → rejection | ✅ Pass (413/400) |
| `many_headers` | 90 headers (near MAX_HEADER_COUNT=100) | ✅ Pass |
| `slow_client` | Partial request with 1s delay | ✅ Pass |
| `slowloris_deadline` | Drip-fed request that never completes; each `recv()` stays under `SO_RCVTIMEO` | ✅ Pass (cut off by the total request deadline) |
| `request_deadline_silent` | Partial request then silence, with the per-`recv()` read timeout disabled | ✅ Pass (effective read timeout capped at the deadline) |
| `listen_failure_cleanup` | `listen()` forced to fail after startup, then `destroy()` | ✅ Pass (state reset, teardown clean; skips if the failure can't be forced) |
| `transfer_encoding_smuggling` | Transfer-Encoding token parsing per RFC 7230 §3.3.1 | ✅ Pass (body de-chunked; smuggling vectors → 400; unsupported coding → 501) |
| `request_target_control_bytes` | Bare CR, LF, HTAB, C0 and DEL in the request-target | ✅ Pass (all → 400) |
| `host_header_enforcement` | Host requirement keyed on HTTP version, not the `Connection` header | ✅ Pass |
| `path_normalization` | `//` collapsed and trailing `/` stripped before routing | ✅ Pass (literal and `:param` routes agree) |
| `async_idle_reaper` | Idle / slow-loris connection in async mode | ✅ Pass (reaped at the deadline; shutdown latency bounded) |
| `async_server_restart` | Stop an async server, then listen again on the same port | ✅ Pass (event loop torn down cleanly) |

**Findings:** Server handles high-throughput scenarios well. The thread pool (16 workers) efficiently processes concurrent requests. Oversized requests are properly rejected with appropriate HTTP error codes. The nine tests below `slow_client` were added after the original report (PRs #78–#89) and are regression guards for specific security findings: request-smuggling via Transfer-Encoding, CRLF injection through the request-target, route aliasing via un-normalized paths, Host-header bypass, and connection exhaustion from clients that never finish a request.

#### 6. Input Validation Stress Tests (2/2 passed)

| Test | Description | Result |
|------|-------------|--------|
| `input_validation_long_strings` | 100KB string validation | ✅ Pass |
| `html_sanitize_large` | 1,000 `<script>` tags sanitized | ✅ Pass |

**Findings:** Input validation handles large inputs without crashes. HTML sanitization correctly escapes all script tags.

#### 7. Compression Stress Tests (1/1 passed)

| Test | Description | Result |
|------|-------------|--------|
| `compression_large_payload` | CRC32 of 1MB data | ✅ Pass |

#### 8. Memory Lifecycle Tests (3/3 passed)

| Test | Description | Result |
|------|-------------|--------|
| `server_create_destroy_cycle` | 100× create/destroy HTTP server | ✅ Pass |
| `router_create_destroy_cycle` | 100× create/destroy router with routes | ✅ Pass |
| `event_loop_create_destroy_cycle` | 100× create/destroy event loop | ✅ Pass |

**Findings:** No memory leaks across 300 object lifecycle iterations. All `create()`/`destroy()` pairs are properly balanced.

---

## Known Limits Verified

| Component | Limit | Value | Behavior When Exceeded |
|-----------|-------|-------|----------------------|
| Router | MAX_ROUTES | 256 | Returns -1 |
| Router | MAX_MIDDLEWARES | 32 | Returns -1 |
| Sessions | MAX_SESSIONS | 1,024 | Returns NULL |
| JSON Parser | JSON_MAX_DEPTH | 512 | Returns NULL |
| HTTP Parser | MAX_HEADER_COUNT | 100 | Returns 400 |
| HTTP Parser | MAX_BODY_BYTES | 1 MB | Returns 413 |
| HTTP Parser | MAX_HEADER_BYTES | 16 KB | Returns 431 |
| Event Loop | MAX_TIMERS | 64 | Returns -1 |
| Event Loop | MAX_EVENTS | 1,024 | — |
| Connections | Listen Backlog | 128 | Kernel queues |

---

## Bugs Discovered

Six issues were identified during the original stress testing run. **All six are fixed.** Full
details, plus four more found in later security review, are in [BUGS.md](BUGS.md) — ten issues, all
closed.

| # | Severity | Issue | Status |
|---|----------|-------|--------|
| 1 | **Critical** | No SIGPIPE handling — `send()` without `MSG_NOSIGNAL` can crash process | ✅ Fixed (`signal(SIGPIPE, SIG_IGN)` + `MSG_NOSIGNAL`) |
| 2 | **High** | Session store lacks thread safety — no mutex in `session.c` | ✅ Fixed (`pthread_mutex_t` in `session_store_t`) |
| 3 | **Medium** | `rand()` fallback in session/CSRF not thread-safe | ✅ Fixed — the fallback was removed entirely; both paths now use `secure_random_bytes()` and fail closed |
| 4 | **Low** | Middleware singleton pattern limits to one instance per type | ✅ Fixed (`user_data` on `middleware_fn_t`) |
| 5 | **Low** | Hard-coded MAX_TIMERS=64 with no query API | ✅ Fixed (`event_loop_get_timer_count()` / `_get_max_timers()`) |
| 6 | **Info** | No keep-alive connection count limit | ✅ Fixed (`http_server_set_max_connections()`) |

---

## Production Readiness Checklist

| Criterion | Status | Notes |
|-----------|--------|-------|
| Compiles without warnings | ✅ | `-Wall -Wextra -pedantic` clean |
| All unit tests pass | ✅ | 166/166 (test_weblib) |
| All stress tests pass | ✅ | 37/37 (test_stress) |
| Full ctest run passes | ✅ | 6/6 suites by default; 13/13 with `-DWEBLIB_ENABLE_TLS=ON -DWEBLIB_TLS_TEST_HOOKS=ON` |
| Memory leak free | ✅ | Valgrind: 0 leaks, 0 errors |
| Buffer overflow safe | ✅ | All `sprintf` → `snprintf` |
| JSON depth limit | ✅ | MAX_DEPTH=512 prevents stack overflow |
| Request size limits | ✅ | Body 1MB, headers 16KB |
| Thread pool bounded | ✅ | Default 16, configurable [1, 256] |
| Graceful shutdown | ✅ | State machine with drain timeout |
| SIGPIPE protection | ✅ | BUG-1 fixed — `signal(SIGPIPE, SIG_IGN)` + `MSG_NOSIGNAL` on every `send()` |
| Thread-safe sessions | ✅ | BUG-2 fixed — `pthread_mutex_t` in `session_store_t` |
| Secure random | ✅ | BUG-3 fixed — `secure_random_bytes()` everywhere, fails closed; no `rand()`/`rand_r()` fallback remains |
| HTTPS / TLS | ⚠️ | Available in v2.0.0 but **experimental and unaudited**, off by default, and outside the scope of this report — see [`src/tls/README.md`](src/tls/README.md) |

---

**Status:** BUG-1 (SIGPIPE) and BUG-2 (session thread safety), the two blockers this report raised
for the v1.0.0 release, were both fixed in commit `9a89c30` on 2026-02-20 — ahead of v1.0.0 shipping
on 2026-02-22. All ten issues tracked in [BUGS.md](BUGS.md) are now resolved. For plain-HTTP
workloads on Linux the library is in good shape; if you enable TLS you are running experimental,
unaudited cryptographic code and should not deploy it without an external audit.

---

**Report Author:** AI Production Manager (Copilot Coding Agent)
**Maintainer:** [@kamrankhan78694](https://github.com/kamrankhan78694)
