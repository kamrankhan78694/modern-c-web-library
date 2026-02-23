# Next Phase Roadmap — Modern C Web Library v2.0.0

> **Methodology**: First-principles engineering workflow.
> Every decision below traces back to a verifiable technical constraint, not convention.
> **Scope**: v2.0 roadmap (Phases 11–20) — building on the completed v1.0 foundation.

---

## 1. Idea Intake

**Core problem in one sentence**: The library has shipped a feature-rich v0.6.0 but lacks the connection-level hardening, transport security, CI automation, and observability required for unsupervised production deployment.

---

## 2. Crystallized Brief

| Dimension | Detail |
|-----------|--------|
| **Target users** | C developers building HTTP/WebSocket backends who refuse external dependencies |
| **Desired outcomes** | A server that survives hostile network conditions, encrypts traffic, shuts down without data loss, and proves correctness through automated CI on every commit |
| **Non-goals** | HTTP/3 / QUIC (premature without TLS); framework-level ORM; language bindings; GUI tooling |

---

## 3. Completed Phases (Reference)

| Phase | Version | Status | Highlights |
|-------|---------|--------|------------|
| Phase 4 | v0.4.0 | ✅ Complete | HTTP parser hardening, header storage, JSON arrays, connection handling |
| Phase 5 | v0.5.0 | ✅ Complete | Body parsing, cookies, CORS, rate limiting, static file serving |
| Phase 6 | v0.6.0 | ✅ Complete | Sessions, template engine, auth middleware (Basic/JWT/API-Key), DB pooling, API docs |
| Phase 7 | v0.7.0 | ✅ Complete | Socket timeouts, thread pool, graceful shutdown, GitHub Actions CI, integration tests |
| Phase 8 | v0.8.0 | ✅ Complete | CSRF middleware, logging, error handler, input validation, health check |
| Phase 9 | v0.9.0 | ✅ Complete | Response compression, caching layer, metrics middleware, async WebSocket, benchmarking suite |
| Phase 10 | v1.0.0 | ✅ Complete | REST API example, tutorials, release engineering |

**Current state**: 129/129 unit tests passing · zero compiler warnings · 25 source modules · 5 example servers

---

## 4. Grounded First-Principles Design (v2.0)

### What makes a C web library disruptively fast?

Working from the hardware up, not from features down:

1. **Zero-allocation request path** — Every `malloc()` per request is a cache miss and a scalability cliff. Arena allocators with pointer-bump allocation eliminate heap contention entirely.
2. **Kernel bypass I/O** — `read()`/`write()` syscalls dominate latency at scale. `io_uring` with registered buffers and multishot accept removes the kernel-user boundary from the hot path.
3. **SIMD-accelerated parsing** — Byte-at-a-time HTTP parsing leaves 90%+ of CPU width unused. SSE4.2/AVX2/NEON can scan 16–32 bytes per cycle for delimiters and validate characters in parallel.
4. **Pure C TLS 1.3** — External TLS libraries (OpenSSL, BoringSSL) add 1M+ LOC of dependency. A focused TLS 1.3 implementation with AES-NI/ARM-CE hardware acceleration achieves security without bloat.
5. **Lock-free concurrency** — Mutex contention destroys throughput beyond 16 cores. CAS-based data structures, work-stealing schedulers, and per-CPU affinity enable linear scaling.
6. **AI-native serving primitives** — LLM inference has unique I/O patterns: streaming tokens via SSE, batch request coalescing, backpressure-aware queuing. These require first-class protocol support, not bolted-on middleware.
7. **Production observability** — A server without metrics, traces, and structured logging cannot be operated at scale. Built-in Prometheus/OpenTelemetry support is non-negotiable.

### Architecture Decision Records (v2.0)

| Decision | Rationale |
|----------|-----------|
| Arena + slab allocator per connection | Zero heap allocations in steady state; deterministic memory usage; no fragmentation |
| `io_uring` with epoll/kqueue/poll fallback chain | Maximum throughput on Linux 5.6+; graceful degradation on older kernels and other platforms |
| SIMD HTTP parser with scalar fallback | 10-20x speedup on modern CPUs; compile-time feature detection; no runtime dependency |
| Pure C TLS 1.3 (RFC 8446) | Zero external dependencies; AES-NI/ARM-CE acceleration; minimal attack surface |
| Lock-free MPMC queues + work-stealing | Linear scaling to 128+ cores; no mutex contention on hot paths |
| Built-in SSE/streaming JSON for AI serving | Purpose-built for LLM inference patterns; lower latency than generic HTTP middleware |
| Prometheus metrics + OpenTelemetry traces | Production-grade observability without external agents; matches Go/Rust ecosystem maturity |

---

## 5. Adversarial Review

| Attack Vector / Failure Mode | Current Exposure | Mitigation (Phase) |
|------------------------------|-----------------|---------------------|
| **Slowloris** (slow headers) | CRITICAL: `recv()` blocks forever | Socket read timeout (Phase 7) |
| **Slow POST** (slow body) | CRITICAL: body read has no deadline | Body read timeout (Phase 7) |
| **Connection exhaustion** | HIGH: thread-per-connection, no cap | Thread pool with bounded queue (Phase 7) |
| **Plaintext credentials** | HIGH: no TLS | Pure C TLS 1.2+ (Phase 8) |
| **CSRF** | MEDIUM: no token validation | CSRF middleware with double-submit cookie (Phase 8) |
| **Request smuggling** | LOW: Content-Length + Transfer-Encoding conflict checked | Add duplicate Transfer-Encoding detection (Phase 7) |
| **Memory leaks under error paths** | LOW: parser malloc on line 1046 may leak on early return | Valgrind CI gate (Phase 7) |
| **No regression detection** | HIGH: no CI pipeline | GitHub Actions on every push (Phase 7) |
| **Silent failures** | MEDIUM: errors go to fprintf(stderr) | Structured logging middleware (Phase 8) |
| **Stale connections after shutdown** | MEDIUM: no drain phase | Graceful shutdown state machine (Phase 7) |

---

## 6. Design Iteration — v2.0 Phase Architecture

Based on first-principles analysis, phases are ordered by **compounding value** — each phase unlocks performance or capability that subsequent phases build upon:

```
Phase 11 (v1.1.0): Memory Architecture Revolution
   ├── Arena/slab allocator (per-connection memory pool)
   ├── Object pool for hot structures (lock-free freelist)
   ├── Cache-line alignment (64-byte boundaries)
   └── Memory usage dashboard (/debug/memory endpoint)

Phase 12 (v1.2.0): io_uring & Zero-Copy I/O
   ├── io_uring event backend (registered buffers, multishot accept)
   ├── sendfile() for static files
   └── Fallback chain: io_uring → epoll → kqueue → poll

Phase 13 (v1.3.0): SIMD-Accelerated HTTP Parser
   ├── SSE4.2/AVX2 paths (x86_64)
   ├── ARM NEON path (aarch64)
   └── Scalar fallback (portable)

Phase 14 (v1.4.0): Pure C TLS 1.3
   ├── Full TLS 1.3 (RFC 8446)
   ├── AES-NI/ARM-CE hardware acceleration
   ├── X25519 key exchange
   └── Certificate parsing, SNI, 0-RTT resumption

Phase 15 (v1.5.0): HTTP/2 Protocol Engine
   ├── Binary framing layer
   ├── HPACK header compression
   ├── Stream multiplexing + flow control
   └── Server push, ALPN negotiation

Phase 16 (v1.6.0): AI Inference Serving Primitives
   ├── Server-Sent Events (SSE) + streaming JSON parser
   ├── Batch request coalescing + inference queue
   ├── Model routing middleware + backpressure
   └── GPU-friendly buffer alignment

Phase 17 (v1.7.0): Lock-Free Concurrency & Work-Stealing
   ├── Lock-free MPMC queue
   ├── Work-stealing scheduler
   ├── RCU for routing table updates
   └── Per-CPU connection affinity + atomic metrics

Phase 18 (v1.8.0): AI Agent Orchestration Protocol
   ├── JSON-RPC 2.0 over WebSocket
   ├── Agent-to-agent routing + tool calling protocol
   ├── Context window management
   └── Structured output enforcement + agent health/heartbeat

Phase 19 (v1.9.0): Observability, Profiling & Production Hardening
   ├── Prometheus metrics exporter
   ├── OpenTelemetry trace context propagation
   ├── Structured JSON logging + built-in profiler
   └── Connection forensics + chaos testing hooks

Phase 20 (v2.0.0): World's Fastest AI-Native C Web Library
   ├── TechEmpower benchmark submission
   ├── AI inference example server + agent orchestration example
   ├── Automated benchmark CI + viral documentation
   └── Migration guide + release engineering
```

---

## 7. Milestone Roadmap — v2.0 (Phases 11–20)

### Phase 11 — v1.1.0: Memory Architecture Revolution

| Component | Deliverables | Disruption Target |
|-----------|-------------|-------------------|
| **Arena Allocator** | Per-connection memory pool with configurable block sizes (4KB/16KB/64KB); O(1) pointer-bump allocation; per-connection reuse via `arena_reset()` | 0 heap allocations per request in steady state |
| **Object Pool** | Pre-allocated pools for `http_request_t`, `http_response_t`, `json_value_t`; lock-free freelist with CAS operations; exhaustion fallback to arena | Eliminate malloc/free overhead for hot structures |
| **Cache-Line Alignment** | All hot structs padded to 64-byte boundaries; `CACHE_LINE_ALIGN` macro; `static_assert` verification | Eliminate false sharing in concurrent access |
| **Memory Dashboard** | `GET /debug/memory` endpoint; JSON response with arena utilization, pool hit rates, peak usage | Runtime visibility into memory behavior |
| **Tests** | Arena lifecycle, pool exhaustion/recovery, alignment verification, leak detection under load | Full coverage of memory subsystem |

### Phase 12 — v1.2.0: io_uring & Zero-Copy I/O

| Component | Deliverables | Disruption Target |
|-----------|-------------|-------------------|
| **io_uring Backend** | `io_uring` event backend with submission/completion queue management; registered buffers for zero-copy I/O; multishot accept for connection handling | 1,000,000+ RPS plaintext |
| **sendfile() Integration** | Zero-copy static file serving via `sendfile()` on Linux, `sendfile()` on macOS | Eliminate user-space copies for static content |
| **Fallback Chain** | Runtime detection: `io_uring` → `epoll` → `kqueue` → `poll`; seamless degradation on older kernels | <10μs p99 latency on supported platforms |

### Phase 13 — v1.3.0: SIMD-Accelerated HTTP Parser

| Component | Deliverables | Disruption Target |
|-----------|-------------|-------------------|
| **SSE4.2 Path** | `_mm_cmpistri` for delimiter scanning; character class validation in parallel | 10-20x faster header parsing vs scalar |
| **AVX2 Path** | 32-byte-wide scanning for bulk header processing; compile-time feature detection | 2GB/s parsing throughput on modern x86 |
| **ARM NEON Path** | Equivalent SIMD acceleration for ARM64 platforms | Competitive parsing speed on ARM servers |
| **Scalar Fallback** | Portable C implementation; automatic selection when SIMD unavailable | Correct behavior on all platforms |

### Phase 14 — v1.4.0: Pure C TLS 1.3

| Component | Deliverables | Disruption Target |
|-----------|-------------|-------------------|
| **TLS 1.3 Core** | Full TLS 1.3 from RFC 8446; handshake state machine; record layer | Pure C TLS 1.3 with zero external dependencies |
| **Hardware Acceleration** | AES-NI acceleration for AES-128/256-GCM; ARM-CE for ARM platforms | Hardware-speed encryption without OpenSSL |
| **Key Exchange** | X25519 (Curve25519) Diffie-Hellman key exchange | Modern, fast key agreement |
| **Certificate & Features** | PEM certificate parsing; SNI support; 0-RTT resumption | Production-ready TLS configuration |

### Phase 15 — v1.5.0: HTTP/2 Protocol Engine

| Component | Deliverables | Disruption Target |
|-----------|-------------|-------------------|
| **Binary Framing** | HTTP/2 frame parser/serializer; frame type dispatch | Full HTTP/2 in pure C |
| **HPACK Compression** | Static + dynamic header table; Huffman encoding/decoding | Efficient header compression |
| **Stream Multiplexing** | Concurrent streams over single connection; stream prioritization; flow control | Multiplexed request handling |
| **Server Push & ALPN** | Server push support; ALPN negotiation for protocol upgrade | Complete HTTP/2 feature set |

### Phase 16 — v1.6.0: AI Inference Serving Primitives

| Component | Deliverables | Disruption Target |
|-----------|-------------|-------------------|
| **SSE & Streaming** | Server-Sent Events (SSE) with `text/event-stream`; streaming JSON parser for incremental token output | Purpose-built AI inference serving layer |
| **Batch Coalescing** | Batch request coalescing for inference efficiency; inference queue with configurable backpressure | Optimal GPU utilization |
| **Model Routing** | Model routing middleware; tensor binary protocol for efficient data transfer | Multi-model serving from single server |
| **GPU Alignment** | GPU-friendly buffer alignment for zero-copy tensor transfer | Minimize CPU-GPU data transfer overhead |

### Phase 17 — v1.7.0: Lock-Free Concurrency & Work-Stealing

| Component | Deliverables | Disruption Target |
|-----------|-------------|-------------------|
| **Lock-Free Queue** | Lock-free MPMC (multi-producer multi-consumer) queue with CAS operations | Linear throughput scaling to 128+ cores |
| **Work-Stealing Scheduler** | Per-core work queues; idle cores steal from busy cores; adaptive load balancing | Optimal CPU utilization across all cores |
| **RCU Routing** | Read-Copy-Update for routing table; zero-overhead reads; deferred reclamation | Hot-path routing with zero locks |
| **CPU Affinity & Metrics** | Per-CPU connection affinity; atomic metrics counters; NUMA-aware allocation | Hardware-topology-aware scheduling |

### Phase 18 — v1.8.0: AI Agent Orchestration Protocol

| Component | Deliverables | Disruption Target |
|-----------|-------------|-------------------|
| **JSON-RPC 2.0** | JSON-RPC 2.0 over WebSocket; request/response/notification support | First pure C AI agent orchestration server |
| **Agent Routing** | Agent-to-agent routing; tool calling protocol with schema validation | Multi-agent communication backbone |
| **Context Management** | Context window management; token counting; conversation history tracking | Efficient LLM context handling |
| **Health & Output** | Structured output enforcement (JSON schema); agent health monitoring; heartbeat protocol | Production-ready agent infrastructure |

### Phase 19 — v1.9.0: Observability, Profiling & Production Hardening

| Component | Deliverables | Disruption Target |
|-----------|-------------|-------------------|
| **Prometheus Metrics** | Built-in Prometheus exporter (`/metrics` endpoint); request rate, latency histograms, error rates | Production-grade observability matching Go/Rust ecosystem maturity |
| **OpenTelemetry** | Trace context propagation (W3C TraceContext); span creation for request lifecycle | Distributed tracing without external agents |
| **Logging & Profiling** | Structured JSON logging with configurable levels; built-in profiler with flame graph output | Zero-dependency diagnostics |
| **Chaos & Forensics** | Connection forensics (per-connection metadata); chaos testing hooks (fault injection, latency injection) | Production hardening through controlled failure |

### Phase 20 — v2.0.0: World's Fastest AI-Native C Web Library

| Component | Deliverables | Disruption Target |
|-----------|-------------|-------------------|
| **Benchmarks** | TechEmpower benchmark submission (plaintext, JSON, fortunes); automated benchmark CI with regression detection | The definitive pure C web library for AI workloads |
| **Examples** | AI inference example server (SSE streaming, batch coalescing); agent orchestration example (multi-agent chat) | Viral adoption through compelling demos |
| **Documentation** | Migration guide from v1.0 → v2.0; API reference; architecture deep-dive; performance tuning guide | Developer experience that drives adoption |
| **Release** | Release engineering; semantic versioning; CHANGELOG; package distribution | Professional release quality |

---

## 8. Atomic Task Breakdown

### Phase 11 — Memory Architecture Revolution (v1.1.0)

#### 11.1 Arena Allocator
| # | Task | File(s) | Acceptance Criteria | Est. |
|---|------|---------|-------------------|------|
| 11.1.1 | Implement `arena_create()`/`arena_destroy()` with configurable block sizes (4KB/16KB/64KB) | `src/arena.c` (new), `include/weblib.h` | Arena allocates initial block; destroy frees all blocks; configurable block size parameter | 3h |
| 11.1.2 | Implement `arena_alloc()` with O(1) pointer-bump allocation | `src/arena.c` | Allocation returns aligned pointer; O(1) fast path; automatic block chaining when current block is exhausted | 2h |
| 11.1.3 | Implement `arena_reset()` for per-connection reuse | `src/arena.c` | Reset returns arena to initial state without freeing blocks; all subsequent allocations reuse existing memory | 2h |
| 11.1.4 | Integrate arena into `http_request_t`/`http_response_t` lifecycle | `src/http_server.c` | Each connection gets an arena; request/response allocations use arena; arena reset between keep-alive requests | 4h |
| 11.1.5 | Unit tests for arena lifecycle, allocation, reset, exhaustion | `tests/test_weblib.c` | Tests cover: create/destroy, sequential allocations, reset and reuse, block exhaustion and chaining, alignment correctness | 3h |

#### 11.2 Object Pool
| # | Task | File(s) | Acceptance Criteria | Est. |
|---|------|---------|-------------------|------|
| 11.2.1 | Implement `pool_create()`/`pool_destroy()` with pre-allocated slabs | `src/pool.c` (new), `include/weblib.h` | Pool pre-allocates N objects of fixed size; destroy frees all slabs; configurable pool capacity | 3h |
| 11.2.2 | Lock-free `pool_acquire()`/`pool_release()` with CAS operations | `src/pool.c` | Acquire/release use `__atomic_compare_exchange_n` on freelist head; no mutex on hot path; ABA prevention | 4h |
| 11.2.3 | Pool exhaustion fallback to arena allocation | `src/pool.c` | When pool is exhausted, allocate from connection arena; log pool miss for dashboard metrics | 2h |
| 11.2.4 | Integration with hot structures (`http_request_t`, `http_response_t`, `json_value_t`) | `src/http_server.c`, `src/json.c` | Hot structures acquired from pool instead of `malloc()`; released back to pool instead of `free()` | 4h |
| 11.2.5 | Unit tests for pool create/acquire/release/exhaustion | `tests/test_weblib.c` | Tests cover: create with capacity, acquire until empty, release and reacquire, exhaustion fallback, concurrent acquire/release | 3h |

#### 11.3 Cache-Line Alignment
| # | Task | File(s) | Acceptance Criteria | Est. |
|---|------|---------|-------------------|------|
| 11.3.1 | Add `CACHE_LINE_ALIGN` macro (64-byte alignment) | `include/weblib.h` | Macro uses `__attribute__((aligned(64)))` on GCC/Clang, `__declspec(align(64))` on MSVC; `CACHE_LINE_SIZE` constant defined | 1h |
| 11.3.2 | Apply alignment to hot structures | `include/weblib.h`, `src/http_server.c` | `http_request_t`, `http_response_t`, `json_value_t`, pool freelist nodes are cache-line aligned | 2h |
| 11.3.3 | Verify alignment with `static_assert` and runtime checks | `tests/test_weblib.c` | Compile-time `static_assert` for struct sizes; runtime test verifies pointer alignment with `((uintptr_t)ptr % 64) == 0` | 2h |

#### 11.4 Memory Dashboard
| # | Task | File(s) | Acceptance Criteria | Est. |
|---|------|---------|-------------------|------|
| 11.4.1 | Implement `memory_stats_t` tracking structure | `src/memory_stats.c` (new), `include/weblib.h` | Atomic counters for: arena bytes allocated/reset, pool acquires/releases/misses, peak memory usage | 3h |
| 11.4.2 | Register `GET /debug/memory` endpoint | `src/memory_stats.c` | Endpoint registered via router; returns current memory statistics; only available when compiled with `DEBUG_MEMORY` flag | 2h |
| 11.4.3 | JSON response with arena utilization, pool hit rates, peak usage | `src/memory_stats.c` | Response includes: arena block count, total bytes, utilization %; pool capacity, in-use count, hit rate %; peak RSS | 3h |
| 11.4.4 | Unit tests for dashboard endpoint | `tests/test_weblib.c` | Tests cover: stats initialization, counter increments, JSON response format, endpoint registration | 2h |

### Phase 12–20 — Summary Task Estimates

| Phase | Version | Key Deliverables | Est. Duration |
|-------|---------|-----------------|---------------|
| Phase 12 | v1.2.0 | io_uring backend, registered buffers, sendfile(), fallback chain | 4 weeks |
| Phase 13 | v1.3.0 | SSE4.2/AVX2/NEON HTTP parser, scalar fallback, benchmarks | 3 weeks |
| Phase 14 | v1.4.0 | TLS 1.3 (RFC 8446), AES-NI/ARM-CE, X25519, certificates | 6 weeks |
| Phase 15 | v1.5.0 | HTTP/2 framing, HPACK, stream multiplexing, server push | 5 weeks |
| Phase 16 | v1.6.0 | SSE, streaming JSON, batch coalescing, model routing | 4 weeks |
| Phase 17 | v1.7.0 | Lock-free MPMC, work-stealing, RCU routing, CPU affinity | 4 weeks |
| Phase 18 | v1.8.0 | JSON-RPC 2.0, agent routing, tool calling, context management | 4 weeks |
| Phase 19 | v1.9.0 | Prometheus, OpenTelemetry, structured logging, chaos testing | 3 weeks |
| Phase 20 | v2.0.0 | TechEmpower submission, examples, documentation, release | 3 weeks |

---

## 9. Parallel Build Strategy

Tasks that share no data dependencies can be developed simultaneously:

```
PHASE 11 PARALLEL GROUPS:

PARALLEL GROUP A (Week 1-2):
├── 11.1 Arena Allocator         ← new files (src/arena.c); standalone module
├── 11.3 Cache-Line Alignment    ← header-only changes (include/weblib.h)
└── 11.4.1 Memory Stats Struct   ← new file (src/memory_stats.c); standalone

PARALLEL GROUP B (Week 2-3):
├── 11.2 Object Pool             ← new file (src/pool.c); depends on 11.1 for fallback
└── 11.4.2-11.4.4 Dashboard      ← depends on 11.4.1 (stats struct)

SEQUENTIAL (Week 3-4):
├── 11.1.4 Arena Integration     ← depends on 11.1.1-11.1.3 (arena API complete)
└── 11.2.4 Pool Integration      ← depends on 11.2.1-11.2.3 (pool API complete)

FINAL (Week 4):
└── All unit tests + integration  ← depends on all implementations complete

FUTURE PHASE DEPENDENCIES:
Phase 12 (io_uring)              ← benefits from Phase 11 arena (registered buffers)
Phase 13 (SIMD parser)           ← benefits from Phase 11 alignment (SIMD requires aligned buffers)
Phase 17 (lock-free concurrency) ← builds on Phase 11 CAS patterns from object pool
```

---

## 10. Build Validation — Success Criteria per Module

### Phase 11 Checkpoints

| Module | Unit Test Gate | Integration Test Gate | Performance Gate |
|--------|---------------|----------------------|-----------------|
| Arena Allocator | Create/destroy; 1000 sequential allocations; reset and reuse; block exhaustion triggers new block; Valgrind clean | Arena integrated into request lifecycle; 10K requests with zero leaks | Arena alloc < 10ns (pointer bump); zero `malloc()` calls per request in steady state |
| Object Pool | Create with capacity; acquire until empty; release and reacquire; exhaustion falls back to arena; concurrent acquire/release from multiple threads | Pool used for all hot structures; 10K requests with pool reuse | Pool acquire/release < 20ns (CAS); pool hit rate > 95% under normal load |
| Cache-Line Alignment | `static_assert(sizeof(http_request_t) % 64 == 0)`; runtime pointer alignment check | No false sharing under concurrent load (perf stat verification) | No measurable regression from padding overhead |
| Memory Dashboard | Stats counters increment correctly; JSON response parses correctly; endpoint returns 200 | Dashboard accessible during load test; values update in real-time | Dashboard response < 1ms; zero impact on request hot path |

### Phase 12–20 Checkpoints (High-Level)

| Phase | Key Validation |
|-------|---------------|
| Phase 12 | io_uring backend passes all existing tests; >1M RPS plaintext on Linux 5.6+; fallback chain works on older kernels |
| Phase 13 | SIMD parser produces identical results to scalar; >2GB/s parsing throughput; all platforms pass |
| Phase 14 | TLS 1.3 passes RFC 8446 test vectors; `curl --tlsv1.3` connects; browser shows green lock |
| Phase 15 | `curl --http2` returns 200; multiplexed streams work; HPACK compression correct |
| Phase 16 | SSE streaming delivers tokens in real-time; batch coalescing reduces latency; backpressure prevents OOM |
| Phase 17 | Linear scaling to 128 cores; zero mutex contention on hot path; RCU routing zero-overhead reads |
| Phase 18 | JSON-RPC 2.0 conformance tests pass; multi-agent chat example works; heartbeat detects agent failure |
| Phase 19 | Prometheus scrape returns valid metrics; OpenTelemetry traces propagate; chaos hooks trigger correctly |
| Phase 20 | TechEmpower benchmark completes; all examples build and run; documentation renders correctly |

---

## 11. QA Pipeline

### Automated Testing (Every Commit)

```
┌─────────────────────────────────────────────────────┐
│  GitHub Actions CI Pipeline                          │
│                                                      │
│  1. Build (Linux gcc + macOS clang)                  │
│     └── cmake -DCMAKE_C_FLAGS="-Werror" ..           │
│                                                      │
│  2. Unit Tests                                       │
│     └── ./tests/test_weblib  (all must pass)         │
│                                                      │
│  3. Integration Tests                                │
│     └── ./tests/integration/run_all  (protocol +     │
│         malformed + concurrent)                      │
│                                                      │
│  4. Memory Safety (Linux only)                       │
│     └── valgrind --leak-check=full --error-          │
│         exitcode=1 ./tests/test_weblib               │
│                                                      │
│  5. Static Analysis (optional)                       │
│     └── cppcheck --enable=all --error-exitcode=1     │
│                                                      │
│  6. Benchmark Regression (Phase 9+)                  │
│     └── Compare req/s against baseline ±10%          │
└─────────────────────────────────────────────────────┘
```

### Manual Testing Checkpoints (Per Release)

| # | Test | Method | Pass Criteria |
|---|------|--------|--------------|
| M1 | Slow client resistance | `slowhttptest -c 1000` against server | Zero crashes; all slow connections timeout |
| M2 | Large file upload | `curl -F "file=@100MB.bin" http://...` | Server accepts up to configured limit; rejects larger |
| M3 | Browser TLS handshake | Chrome/Firefox navigate to `https://localhost:8443` | Green lock icon; certificate details visible |
| M4 | Graceful shutdown under load | Send SIGTERM during `wrk` benchmark run | All in-flight requests complete; zero dropped |
| M5 | Memory under sustained load | Run 1M requests via `wrk`; monitor RSS | RSS stable (no unbounded growth); Valgrind clean |
| M6 | Cross-platform build | Build on Linux (gcc), macOS (clang), Windows (MSVC) | Zero errors, zero warnings on all platforms |

---

## 12. Security Review — Threat Model

### Assets Under Protection

| Asset | Location | Sensitivity |
|-------|----------|-------------|
| HTTP request data | `http_request_t` in memory | Contains credentials (cookies, auth headers) |
| Session store | `session_store_t` in-process memory | Session IDs = authentication tokens |
| TLS private keys | Loaded from PEM file at startup | Compromise = full traffic decryption |
| User file uploads | Temporary buffer in `body_parser_data_t` | User-supplied; potential malware |

### Threat Model (STRIDE)

| Threat | Category | Target | Mitigation |
|--------|----------|--------|-----------|
| **Slowloris / Slow POST** | Denial of Service | Connection pool | Socket timeouts (Phase 7.1); thread pool bounds (Phase 7.2) |
| **Request smuggling** | Tampering | HTTP parser | Duplicate header detection (Phase 7.6); strict parser mode |
| **Path traversal** | Information Disclosure | Static file middleware | Already mitigated (`../` prevention in `middleware_static.c`); add canonicalization |
| **Session hijacking** | Spoofing | Session cookies | `Secure` + `HttpOnly` + `SameSite=Strict` flags (already in v0.6.0); add CSRF tokens (Phase 8) |
| **Credential exposure** | Information Disclosure | HTTP transport | TLS encryption (Phase 8); HSTS header |
| **Buffer overflow** | Elevation of Privilege | All parsers | `MAX_HEADER_BYTES`, `MAX_BODY_BYTES` limits (already enforced); add fuzz testing (Phase 9) |
| **Timing attacks** | Information Disclosure | Auth middleware | Constant-time comparison for JWT signature verification (verify in Phase 8) |
| **Memory disclosure** | Information Disclosure | Error responses | Never include stack traces or internal paths in HTTP error bodies |
| **TLS downgrade** | Tampering | TLS handshake | Reject protocols < TLS 1.2; no SSLv3/TLS 1.0/1.1 (Phase 8) |
| **Private key theft** | Information Disclosure | TLS key material | Zero key material in logs; `mlock()` key pages; zero on free (Phase 8) |

### Vulnerability Checklist (Per-Phase Gate)

Each phase release MUST pass:

- [ ] No compiler warnings with `-Wall -Wextra -Werror -pedantic`
- [ ] Valgrind memcheck: zero errors, zero leaks (definite + indirect)
- [ ] No unbounded allocations from user input
- [ ] All `malloc()` return values checked
- [ ] All `sprintf()` replaced with `snprintf()` (bounded writes)
- [ ] No `strcpy()` — only `strncpy()` or manual bounds-checked copies
- [ ] Session IDs generated from `/dev/urandom` (or `CryptGenRandom` on Windows)
- [ ] TLS key material zeroed with `explicit_bzero()` / `SecureZeroMemory()` before `free()`
- [ ] All public APIs validate NULL pointers and return error codes
- [ ] No information leakage in error responses (status code only, no internals)

### Access Control Matrix

| Component | Read Access | Write Access | Notes |
|-----------|------------|-------------|-------|
| `http_request_t` fields | Route handlers, middleware | Parser only (immutable after parse) | Enforce via const pointers in API |
| Session data | `session_get_data()` only | `session_set_data()` only | Key-scoped; no bulk enumeration API |
| TLS private key | TLS handshake module only | Loaded once at startup | Never exposed via any API |
| Rate limit counters | Rate limit middleware | Rate limit middleware | IP-scoped; auto-expire |

---

## 13. Risk Mitigation Notes

| Risk | Probability | Impact | Mitigation Strategy |
|------|------------|--------|-------------------|
| Pure C TLS is insecure / buggy | HIGH | CRITICAL | Extensive test vector validation; optional compile-time flag to disable; security audit before v1.0 |
| HTTP/2 complexity causes regressions | MEDIUM | HIGH | Feature-flag behind `http_server_enable_http2()`; default off; comprehensive integration tests |
| Thread pool deadlock | LOW | HIGH | No nested locks; work items never wait on pool; timeout on condition variable wait |
| Windows platform divergence | MEDIUM | MEDIUM | Abstract platform APIs behind `src/platform.h`; CI matrix validates Windows build |
| Performance regression between versions | MEDIUM | MEDIUM | Benchmark suite in CI with ±10% tolerance gate |
| API breaking changes | LOW | HIGH | Semantic versioning; deprecated API kept for one major version |

---

## Revision History

| Version | Date | Changes |
|---------|------|---------|
| 1.0 | 2025-01-12 | Initial roadmap (Phases 4–6) |
| 2.0 | 2026-02-19 | Complete rewrite for Phases 7–10: first-principles design, adversarial review, atomic task breakdown, security threat model |
| 3.0 | 2026-02-22 | v2.0 roadmap (Phases 11–20): memory architecture, io_uring, SIMD parser, TLS 1.3, HTTP/2, AI inference, lock-free concurrency, agent orchestration, observability, release |

---

**Maintained by**: MCWL Core Team
**Last Updated**: 2026-02-22
**Status**: Active — Phase 11 implementation pending
**License**: MIT (see LICENSE file)

For questions or discussions about this roadmap, please open an issue on GitHub or contact the maintainers.
