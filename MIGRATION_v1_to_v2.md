# Migration Guide: v1.x → v2.0

> **Status**: This guide will be updated as each phase is implemented. Currently, all items are planned changes.

---

## Overview

The v2.0 release introduces significant performance and feature improvements while maintaining backward compatibility with v1.0 APIs wherever possible. This guide documents all breaking changes, new APIs, and recommended migration steps.

---

## Migration Summary

| Category | Impact | Details |
|----------|--------|---------|
| Core HTTP API | **No breaking changes** | `http_server_create()`, `router_add_route()`, etc. unchanged |
| Event loop | **No breaking changes** | `event_loop_create()` auto-selects io_uring when available |
| Memory allocation | **New API (opt-in)** | Arena allocator available; existing malloc path still works |
| TLS | **New API** | `http_server_enable_tls()` — additive, not breaking |
| HTTP/2 | **New API** | `http_server_enable_http2()` — additive, not breaking |
| SSE streaming | **New API** | `sse_stream_create()` — new capability |
| Agent protocol | **New API** | `agent_registry_create()` — new capability |
| Thread pool | **Behavioral change** | Work-stealing replaces mutex-based queue (Phase 17) |
| Metrics endpoint | **Format change** | `/metrics` switches to Prometheus exposition format (Phase 19) |

---

## Phase-by-Phase Changes

### Phase 11 (v1.1.0) — Memory Architecture

**New APIs:**
```c
// Arena allocator (optional — existing malloc path still works)
arena_t *arena_create(size_t block_size);
void *arena_alloc(arena_t *arena, size_t size);
void arena_reset(arena_t *arena);
void arena_destroy(arena_t *arena);

// Object pool
pool_t *pool_create(size_t object_size, size_t count);
void *pool_acquire(pool_t *pool);
void pool_release(pool_t *pool, void *object);
void pool_destroy(pool_t *pool);
```

**New endpoint:**
- `GET /debug/memory` — Memory usage dashboard (JSON)

**Migration**: No action required. Arena allocator is opt-in. Existing code continues to work unchanged.

### Phase 12 (v1.2.0) — io_uring

**No API changes.** The event loop automatically selects io_uring on Linux 5.1+ kernels. Fallback to epoll/kqueue/poll is automatic.

**Migration**: No action required. Performance improvement is automatic.

### Phase 13 (v1.3.0) — SIMD Parser

**No API changes.** The HTTP parser uses SIMD instructions when available. Falls back to scalar parsing on unsupported hardware.

**Migration**: No action required. Performance improvement is automatic.

### Phase 14 (v1.4.0) — TLS 1.3

**New APIs:**
```c
int http_server_enable_tls(http_server_t *server, const char *cert_path, const char *key_path);
int http_server_set_tls_cipher(http_server_t *server, const char *cipher_list);
```

**Migration**: No action required for HTTP-only deployments. To enable HTTPS, call `http_server_enable_tls()` before `http_server_listen()`.

### Phase 15 (v1.5.0) — HTTP/2

**New API:**
```c
int http_server_enable_http2(http_server_t *server);
```

**Migration**: No action required. HTTP/2 is opt-in via `http_server_enable_http2()`. HTTP/1.1 remains the default.

### Phase 16 (v1.6.0) — AI Inference Serving

**New APIs:**
```c
// Server-Sent Events
sse_stream_t *sse_stream_create(http_response_t *res);
int sse_stream_send(sse_stream_t *stream, const char *event, const char *data);
int sse_stream_send_id(sse_stream_t *stream, const char *event, const char *data, const char *id);
void sse_stream_close(sse_stream_t *stream);

// Batch coalescing middleware
batch_config_t batch_cfg = { .window_ms = 50, .max_batch_size = 32 };
middleware_fn_t batch_mw = batch_middleware_create(&batch_cfg);
```

**Migration**: No action required. New capabilities are additive.

### Phase 17 (v1.7.0) — Lock-Free Concurrency

**Behavioral change**: The thread pool's internal scheduling switches from mutex-based to work-stealing. The public API (`http_server_set_thread_count()`) is unchanged.

**Migration**: No action required. The behavior change is internal and transparent.

### Phase 18 (v1.8.0) — Agent Orchestration

**New APIs:**
```c
agent_registry_t *agent_registry_create(void);
int agent_register(agent_registry_t *reg, const char *agent_id, agent_handler_t handler);
int agent_send_message(agent_registry_t *reg, const char *from, const char *to, json_value_t *msg);
void agent_registry_destroy(agent_registry_t *reg);
```

**Migration**: No action required. New capabilities are additive.

### Phase 19 (v1.9.0) — Observability

**⚠️ Potential breaking change**: The `/metrics` endpoint format changes from custom JSON to Prometheus exposition format.

**Before (v1.0):**
```json
{"total_requests": 1234, "methods": {"GET": 1000, "POST": 234}, ...}
```

**After (v1.9.0):**
```
# HELP http_requests_total Total HTTP requests
# TYPE http_requests_total counter
http_requests_total{method="GET"} 1000
http_requests_total{method="POST"} 234
```

**Migration**: If you parse the `/metrics` JSON endpoint, update your parser to handle Prometheus exposition format. The original JSON metrics will remain available via `metrics_middleware_create()` internal API.

**New APIs:**
```c
void prometheus_register(router_t *router);  // GET /metrics (Prometheus format)
```

### Phase 20 (v2.0.0) — Release

No additional API changes. Phase 20 focuses on benchmarks, examples, and documentation.

---

## Compile-Time Changes

| Flag | v1.0 | v2.0 | Notes |
|------|------|------|-------|
| `-DENABLE_IOURING=ON` | N/A | Optional | Force io_uring (Phase 12) |
| `-DENABLE_SIMD=ON` | N/A | Default ON | SIMD parser (Phase 13) |
| `-DENABLE_TLS=ON` | N/A | Optional | TLS 1.3 support (Phase 14) |
| `-DENABLE_HTTP2=ON` | N/A | Optional | HTTP/2 support (Phase 15) |
| `-DENABLE_PROFILER=ON` | N/A | Optional | Built-in profiler (Phase 19) |

---

## Deprecated APIs

No v1.0 APIs are deprecated in v2.0. All existing APIs continue to function as documented.

---

## Recommended Upgrade Path

1. **Update to v1.1.0** first — verify existing tests pass with arena allocator disabled
2. **Enable io_uring** (v1.2.0) — automatic on Linux 5.1+, verify no regressions
3. **Enable TLS** (v1.4.0) — if needed, add `http_server_enable_tls()` call
4. **Enable HTTP/2** (v1.5.0) — if needed, add `http_server_enable_http2()` call
5. **Adopt SSE streaming** (v1.6.0) — for AI inference endpoints
6. **Update metrics parsing** (v1.9.0) — if consuming `/metrics` endpoint
7. **Upgrade to v2.0.0** — full release with all features

---

## Getting Help

- **Issues**: [GitHub Issues](https://github.com/kamrankhan78694/modern-c-web-library/issues)
- **Documentation**: [NEXT_PHASE.md](NEXT_PHASE.md) for detailed implementation plans
- **API Reference**: [docs/api/README.md](docs/api/README.md) for complete API documentation

---

**Last Updated**: February 2026
**Status**: Planned — will be updated as each phase is implemented
