# Architecture — Modern C Web Library

> **Version**: v1.0.0 (current) with v2.0 planned architecture
> **Last Updated**: February 2026

---

## Design Philosophy

The Modern C Web Library is built on a single uncompromising principle: **zero external dependencies**. Every component — from the HTTP parser to the JSON serializer to the event loop — is implemented in pure ISO C (C99/C11) using only standard library functions and platform APIs.

This architecture document describes the current v1.0.0 system and the planned v2.0 evolution.

---

## Current Architecture (v1.0.0)

### High-Level Component Diagram

```
┌─────────────────────────────────────────────────────────────────┐
│                        Application Layer                         │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────────────┐│
│  │  Router   │  │Middleware│  │ Handlers │  │  Examples/Apps   ││
│  │ (256 max) │  │ (32 max) │  │ (user)   │  │ (simple, async)  ││
│  └──────────┘  └──────────┘  └──────────┘  └──────────────────┘│
├─────────────────────────────────────────────────────────────────┤
│                        HTTP Layer                                │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────────────┐│
│  │HTTP Server│  │  Parser  │  │ Request/ │  │  WebSocket       ││
│  │ (sync/   │  │ (headers,│  │ Response │  │  (RFC 6455)      ││
│  │  async)  │  │  body)   │  │ objects  │  │                  ││
│  └──────────┘  └──────────┘  └──────────┘  └──────────────────┘│
├─────────────────────────────────────────────────────────────────┤
│                        Middleware Layer                           │
│  ┌────────┐ ┌──────┐ ┌──────┐ ┌─────┐ ┌────────┐ ┌───────────┐│
│  │  CORS  │ │ Rate │ │ CSRF │ │Auth │ │Logging │ │  Metrics  ││
│  │        │ │Limit │ │      │ │     │ │        │ │           ││
│  └────────┘ └──────┘ └──────┘ └─────┘ └────────┘ └───────────┘│
│  ┌────────┐ ┌──────┐ ┌──────┐ ┌─────┐ ┌────────┐             │
│  │ Error  │ │Static│ │Body  │ │Cache│ │Compress│             │
│  │Handler │ │Files │ │Parser│ │     │ │(gzip)  │             │
│  └────────┘ └──────┘ └──────┘ └─────┘ └────────┘             │
├─────────────────────────────────────────────────────────────────┤
│                        Data Layer                                │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────────────┐│
│  │   JSON   │  │ Sessions │  │ Template │  │   DB Pool        ││
│  │ (parse/  │  │ (cookie) │  │ Engine   │  │ (thread-safe)    ││
│  │stringify)│  │          │  │ ({{ }})  │  │                  ││
│  └──────────┘  └──────────┘  └──────────┘  └──────────────────┘│
├─────────────────────────────────────────────────────────────────┤
│                        I/O Layer                                 │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────────────┐│
│  │Event Loop│  │  Thread  │  │  Socket  │  │  Benchmark       ││
│  │(epoll/   │  │  Pool    │  │ Timeouts │  │  Suite           ││
│  │kqueue/   │  │ (bounded)│  │(SO_RCVTIMEO│ │                  ││
│  │poll)     │  │          │  │SO_SNDTIMEO)│ │                  ││
│  └──────────┘  └──────────┘  └──────────┘  └──────────────────┘│
├─────────────────────────────────────────────────────────────────┤
│                        Platform Layer                            │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────────────┐  │
│  │ Linux (epoll,│  │ macOS/BSD    │  │ Windows (WinSock2,   │  │
│  │  pthreads)   │  │ (kqueue,     │  │  CreateThread)       │  │
│  │              │  │  pthreads)   │  │                      │  │
│  └──────────────┘  └──────────────┘  └──────────────────────┘  │
└─────────────────────────────────────────────────────────────────┘
```

### Request Processing Pipeline

```
Client Request
    │
    ▼
┌─────────────────┐
│  Socket Accept   │  (thread pool or event loop)
└────────┬────────┘
         │
         ▼
┌─────────────────┐
│  HTTP Parser     │  Parse method, path, headers, body
└────────┬────────┘
         │
         ▼
┌─────────────────┐
│  Route Matching  │  Match path pattern + method
└────────┬────────┘
         │
         ▼
┌─────────────────┐
│  Middleware[0]   │──▶ false? → Stop (e.g., auth failure)
│  Middleware[1]   │──▶ false? → Stop
│  Middleware[N]   │──▶ true  → Continue
└────────┬────────┘
         │
         ▼
┌─────────────────┐
│  Route Handler   │  User-defined handler function
└────────┬────────┘
         │
         ▼
┌─────────────────┐
│  Response Send   │  Serialize headers + body → socket
└─────────────────┘
```

### Operating Modes

**Threaded Mode** (default):
- Thread pool with bounded queue (default 16 workers)
- Each connection handled by one thread
- Blocking I/O with socket timeouts
- Best for: CPU-intensive workloads

**Async Mode** (event-driven):
- Single-threaded event loop
- Non-blocking I/O via epoll/kqueue/poll
- Handles thousands of concurrent connections
- Best for: I/O-bound workloads, WebSocket

### Source Module Map

| Module | File | Responsibility |
|--------|------|---------------|
| HTTP Server | `src/http_server.c` | Accept, parse, dispatch, respond |
| Router | `src/router.c` | Route matching, parameter extraction |
| JSON | `src/json.c` | Parse, create, serialize JSON |
| Event Loop | `src/event_loop.c` | epoll/kqueue/poll abstraction |
| WebSocket | `src/websocket.c` | RFC 6455 protocol handling |
| Async WebSocket | `src/async_websocket.c` | Event loop WebSocket integration |
| Body Parser | `src/body_parser.c` | URL-encoded, multipart parsing |
| Cookie | `src/cookie.c` | RFC 6265 cookie handling |
| Session | `src/session.c` | Server-side session management |
| Template | `src/template.c` | `{{ variable }}` template engine |
| Cache | `src/cache.c` | LRU in-memory cache |
| Compression | `src/compression.c` | Pure C gzip/DEFLATE |
| Benchmark | `src/benchmark.c` | Performance measurement |
| Thread Pool | `src/thread_pool.c` | Bounded worker thread pool |
| DB Pool | `src/db_pool.c` | Database connection pooling |
| Health Check | `src/health_check.c` | `/healthz` endpoint |
| Input Validation | `src/input_validation.c` | String/integer validation |
| Auth Middleware | `src/middleware_auth.c` | Basic/JWT/API-Key auth |
| CORS Middleware | `src/middleware_cors.c` | Cross-origin resource sharing |
| CSRF Middleware | `src/middleware_csrf.c` | CSRF token validation |
| Error Middleware | `src/middleware_error.c` | Centralized error responses |
| Log Middleware | `src/middleware_log.c` | Request logging |
| Metrics Middleware | `src/middleware_metrics.c` | Request metrics collection |
| Rate Limit | `src/middleware_ratelimit.c` | Token bucket rate limiting |
| Static Files | `src/middleware_static.c` | Static file serving |

---

## Planned Architecture (v2.0)

### v2.0 High-Level Evolution

```
v1.0.0 (current)                    v2.0.0 (planned)
─────────────────                   ─────────────────
malloc/free per req     ──────▶     Arena allocator (0 mallocs)
epoll/kqueue/poll       ──────▶     io_uring → epoll → kqueue → poll
Scalar HTTP parser      ──────▶     SIMD parser (SSE4.2/AVX2/NEON)
No TLS                  ──────▶     Pure C TLS 1.3 (AES-NI/ARM-CE)
HTTP/1.1 only           ──────▶     HTTP/2 + HTTP/1.1
No streaming            ──────▶     SSE + streaming JSON
pthread_mutex           ──────▶     Lock-free queues + RCU
Basic metrics           ──────▶     Prometheus + OpenTelemetry
No AI primitives        ──────▶     Inference serving + agent protocol
```

### v2.0 Component Stack

```
┌─────────────────────────────────────────────────────────────────┐
│                     AI Application Layer                         │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────────────┐  │
│  │ Model Routing │  │ Agent        │  │ Tool Calling         │  │
│  │ /v1/models/  │  │ Orchestration│  │ Protocol             │  │
│  │ :name/predict│  │ JSON-RPC 2.0 │  │                      │  │
│  └──────────────┘  └──────────────┘  └──────────────────────┘  │
├─────────────────────────────────────────────────────────────────┤
│                     Streaming Layer                               │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────────────┐  │
│  │ SSE (Server  │  │ Streaming    │  │ Batch Coalescing     │  │
│  │ Sent Events) │  │ JSON Parser  │  │ Middleware           │  │
│  └──────────────┘  └──────────────┘  └──────────────────────┘  │
├─────────────────────────────────────────────────────────────────┤
│                     Protocol Layer                                │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────────────┐  │
│  │ HTTP/2       │  │ TLS 1.3      │  │ HPACK Compression    │  │
│  │ (binary      │  │ (AES-NI,     │  │ (static + dynamic    │  │
│  │  framing)    │  │  ARM-CE)     │  │  table)              │  │
│  └──────────────┘  └──────────────┘  └──────────────────────┘  │
├─────────────────────────────────────────────────────────────────┤
│                     Parsing Layer                                 │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────────────┐  │
│  │ SIMD HTTP    │  │ URI Parser   │  │ Tensor Binary        │  │
│  │ Parser       │  │ (vectorized) │  │ Protocol             │  │
│  │ (SSE4.2/AVX2/│  │              │  │                      │  │
│  │  NEON)       │  │              │  │                      │  │
│  └──────────────┘  └──────────────┘  └──────────────────────┘  │
├─────────────────────────────────────────────────────────────────┤
│                     Concurrency Layer                             │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────────────┐  │
│  │ Lock-Free    │  │ Work-Stealing│  │ RCU Routing Table    │  │
│  │ MPMC Queue   │  │ Scheduler    │  │ (zero-lock reads)    │  │
│  └──────────────┘  └──────────────┘  └──────────────────────┘  │
├─────────────────────────────────────────────────────────────────┤
│                     Memory Layer                                  │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────────────┐  │
│  │ Arena        │  │ Object Pool  │  │ Cache-Line Aligned   │  │
│  │ Allocator    │  │ (lock-free   │  │ Structures           │  │
│  │ (per-conn)   │  │  freelist)   │  │ (64-byte boundary)   │  │
│  └──────────────┘  └──────────────┘  └──────────────────────┘  │
├─────────────────────────────────────────────────────────────────┤
│                     I/O Layer                                     │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────────────┐  │
│  │ io_uring     │  │ epoll/kqueue │  │ Zero-Copy I/O        │  │
│  │ (Linux 5.1+) │  │ (fallback)   │  │ (sendfile, splice,   │  │
│  │              │  │              │  │  registered buffers)  │  │
│  └──────────────┘  └──────────────┘  └──────────────────────┘  │
├─────────────────────────────────────────────────────────────────┤
│                     Observability Layer                           │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────────────┐  │
│  │ Prometheus   │  │ OpenTelemetry│  │ Built-in Profiler    │  │
│  │ /metrics     │  │ Trace Context│  │ (rdtsc, flame graph) │  │
│  └──────────────┘  └──────────────┘  └──────────────────────┘  │
└─────────────────────────────────────────────────────────────────┘
```

### Architecture Decision Records (v2.0)

| Decision | Rationale |
|----------|-----------|
| io_uring with epoll/kqueue fallback | 3–10x throughput vs epoll alone; graceful degradation on older kernels |
| Custom TLS 1.3 (no OpenSSL) | Zero dependencies; AES-NI hardware acceleration; minimal handshake RTT |
| Arena allocator per connection | O(1) alloc/free; eliminates fragmentation; cache-line aligned |
| SIMD HTTP parser | 16–64x faster header scanning; falls back to scalar on unsupported hardware |
| Lock-free work-stealing thread pool | Eliminates mutex contention; scales linearly to core count |
| SSE + chunked streaming | Native LLM token streaming without WebSocket complexity |
| JSON-RPC over WebSocket | Standard agent-to-agent protocol; tool calling; structured responses |

### I/O Backend Selection (v2.0)

```
Runtime detection order:
1. io_uring  → probe __NR_io_uring_setup syscall (Linux 5.1+)
2. epoll     → Linux fallback
3. kqueue    → macOS/BSD
4. poll      → Universal fallback

Selection is automatic at event_loop_create() time.
No API changes required from v1.0 to v2.0.
```

---

## Static Limits

| Limit | Value | Location |
|-------|-------|----------|
| MAX_CONNECTIONS | 128 | `src/http_server.c` |
| BUFFER_SIZE | 8192 | `src/http_server.c` |
| MAX_ROUTES | 256 | `src/router.c` |
| MAX_MIDDLEWARES | 32 | `src/router.c` |
| MAX_EVENTS | 1024 | `src/event_loop.c` |
| MAX_TIMERS | 64 | `src/event_loop.c` |

---

## Build System

```
CMakeLists.txt
    ├── Platform detection (Linux/macOS/Windows/BSD)
    ├── Compiler flags (-Wall -Wextra -pedantic)
    ├── Library targets (static + shared)
    ├── Example targets (simple_server, async_server, rest_api_server, ...)
    └── Test targets (test_weblib)
```

**Platform libraries**: Linux/macOS → `pthread` · Windows → `ws2_32`

---

**Maintained by**: MCWL Core Team
**License**: MIT
