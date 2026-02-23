# Benchmarks — Modern C Web Library

> **Status**: Planned for v2.0.0 (Phase 20). Benchmark infrastructure will be built incrementally across Phases 11–20.

---

## Benchmark Strategy

### Objective

Prove that the Modern C Web Library is the **fastest pure C web library** and competitive with the top entries across all languages on TechEmpower Framework Benchmarks.

### Target Benchmarks

| Benchmark | Category | Target | Phase |
|-----------|----------|--------|-------|
| TechEmpower Plaintext | Throughput | #1 pure C; top 3 overall | Phase 20 |
| TechEmpower JSON | Serialization + throughput | #1 pure C; top 5 overall | Phase 20 |
| TechEmpower Fortunes | Database + template rendering | Top 10 overall | Phase 20 |
| HTTP header parsing | Microbenchmark | 2 GB/s on modern x86 | Phase 13 |
| io_uring vs epoll | I/O comparison | 3x throughput gain | Phase 12 |
| Memory allocations/request | Allocation tracking | 0 heap allocs in steady state | Phase 11 |

### Comparison Targets

| Framework | Language | Notes |
|-----------|----------|-------|
| may-minihttp | Rust | Current TechEmpower leader in some categories |
| h2o | C | High-performance HTTP/2 server |
| nginx | C | Industry standard web server |
| Drogon | C++ | High-performance C++ framework |
| actix-web | Rust | Popular Rust web framework |
| fasthttp | Go | Fastest Go HTTP library |

---

## Benchmark Methodology

### Hardware Requirements

All benchmarks should be run on consistent hardware for reproducible results:

- **Recommended**: Dedicated bare-metal server or cloud instance with:
  - 8+ CPU cores (for multi-threaded benchmarks)
  - 16+ GB RAM
  - 10 Gbps networking (for throughput tests)
  - Linux kernel 5.1+ (for io_uring benchmarks)

### Tools

| Tool | Purpose | Phase |
|------|---------|-------|
| `wrk` | HTTP benchmarking (throughput, latency) | Available now |
| `wrk2` | Coordinated omission-free latency measurement | Phase 12+ |
| Custom C benchmark client | Internal microbenchmarks | Phase 11+ |
| TechEmpower harness | Official TechEmpower benchmark submission | Phase 20 |
| `perf` | CPU profiling and flame graphs | Phase 19 |

### Metrics Collected

| Metric | Unit | Description |
|--------|------|-------------|
| Requests/sec (RPS) | req/s | Total throughput |
| Latency p50 | μs | Median latency |
| Latency p95 | μs | 95th percentile latency |
| Latency p99 | μs | 99th percentile latency |
| Latency p99.9 | μs | 99.9th percentile (tail) latency |
| Memory RSS | MB | Resident set size under load |
| Heap allocations/req | count | Allocations per request in steady state |
| CPU utilization | % | Per-core CPU usage |

---

## Phase-Specific Benchmarks

### Phase 11 — Memory Architecture (v1.1.0)

| Benchmark | Metric | Target |
|-----------|--------|--------|
| Arena allocation throughput | allocs/sec | >100M allocs/sec |
| Object pool acquire/release | cycles | <50 cycles per acquire |
| Heap allocations per request | count | 0 in steady state |
| Memory fragmentation | ratio | <5% overhead vs ideal packing |

### Phase 12 — io_uring (v1.2.0)

| Benchmark | Metric | Target |
|-----------|--------|--------|
| Plaintext RPS (io_uring) | req/s | >1,000,000 |
| Plaintext RPS (epoll) | req/s | Baseline comparison |
| io_uring vs epoll speedup | ratio | >3x |
| p99 latency (io_uring) | μs | <10 |
| Static file throughput (sendfile) | GB/s | >5 GB/s on 10Gbps NIC |

### Phase 13 — SIMD Parser (v1.3.0)

| Benchmark | Metric | Target |
|-----------|--------|--------|
| Header parsing (SSE4.2) | GB/s | >1.5 |
| Header parsing (AVX2) | GB/s | >2.0 |
| Header parsing (scalar) | GB/s | Baseline comparison |
| SIMD vs scalar speedup | ratio | >10x |
| URI parsing throughput | GB/s | >1.0 |

### Phase 14 — TLS 1.3 (v1.4.0)

| Benchmark | Metric | Target |
|-----------|--------|--------|
| TLS handshake latency | μs | <500 (1-RTT) |
| TLS 0-RTT resumption | μs | <100 |
| AES-GCM throughput (AES-NI) | GB/s | >10 |
| AES-GCM throughput (software) | GB/s | Baseline comparison |
| HTTPS RPS | req/s | >500,000 |

### Phase 17 — Lock-Free Concurrency (v1.7.0)

| Benchmark | Metric | Target |
|-----------|--------|--------|
| Scaling: 1 core | req/s | Baseline |
| Scaling: 4 cores | req/s | ~4x baseline |
| Scaling: 16 cores | req/s | ~16x baseline |
| Scaling: 64 cores | req/s | ~60x baseline |
| Scaling: 128 cores | req/s | ~120x baseline |
| Work-stealing overhead | % | <2% |

---

## CI Benchmark Regression

Starting from Phase 12, every PR will run automated benchmarks:

```
┌─────────────────────────────────────────────────────┐
│  Benchmark CI Pipeline (per PR)                      │
│                                                      │
│  1. Build release binary (cmake -DCMAKE_BUILD_TYPE=  │
│     Release)                                         │
│                                                      │
│  2. Run benchmark suite                              │
│     └── wrk -t4 -c100 -d10s (plaintext, JSON)       │
│                                                      │
│  3. Compare against baseline                         │
│     └── Fail if throughput drops >10%                │
│     └── Fail if p99 latency increases >20%           │
│                                                      │
│  4. Update historical graph                          │
│     └── docs/benchmarks/ (auto-generated)            │
└─────────────────────────────────────────────────────┘
```

---

## Running Benchmarks Locally

### Current (v1.0.0)

```bash
# Build in release mode
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make

# Start server
./examples/simple_server 8080 &

# Run wrk benchmark
wrk -t4 -c100 -d30s http://localhost:8080/

# Run built-in benchmark suite
./tests/test_weblib  # Includes benchmark tests
```

### With Docker

```bash
./docker-run.sh test  # Includes benchmark suite
```

---

## Historical Results

> Results will be populated as phases are implemented and benchmarks are run.

| Version | Plaintext RPS | JSON RPS | p99 Latency | Memory RSS | I/O Backend |
|---------|--------------|----------|-------------|------------|-------------|
| v1.0.0 | TBD | TBD | TBD | TBD | epoll/kqueue |
| v1.1.0 | TBD | TBD | TBD | TBD | epoll/kqueue |
| v1.2.0 | TBD | TBD | TBD | TBD | io_uring |

---

**Last Updated**: February 2026
**Status**: Planned — benchmark infrastructure under development
