# Benchmarks

**Version 2.0.0**

The library has shipped a benchmarking module (`src/benchmark.c`) since v0.9.0.
Until now nothing called it — no example, no test, no CI job — so the
"Benchmarking Suite" feature produced numbers for nobody, and no baseline
existed to detect a regression against. This document records the first one.

## How to reproduce

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build --parallel
./build/examples/benchmark_server                     # defaults: 2000 requests
./build/examples/benchmark_server --requests 5000 --port 9100
```

TLS is measured separately, by an external client (see *Why TLS is measured
differently* below):

```bash
cmake -S . -B build-tls -DCMAKE_BUILD_TYPE=RelWithDebInfo -DWEBLIB_ENABLE_TLS=ON
cmake --build build-tls --parallel
bash tests/benchmark_tls.sh ./build-tls/examples/tls_server 40
```

Neither is registered as a ctest suite. They are measurement tools, not
correctness gates: `TlsInteropOpenssl` already gates handshake correctness, and
an ungated performance number in CI is log noise that also costs wall time.
Adding a performance *gate* would need a threshold policy and a stable runner —
a separate decision, not a side effect of this tooling.

## Baseline — 2026-07-28

| Scenario | Throughput | avg | p50 | p95 | p99 | Failed |
|---|---|---|---|---|---|---|
| `GET /plain` (2-byte text body) | 21,464 req/s | 21.3 µs | 20 µs | 25 µs | 57 µs | 0 |
| `GET /json` (small JSON object) | 20,685 req/s | 21.8 µs | 21 µs | 25 µs | 31 µs | 0 |

3000 requests per scenario after a discarded 200-request warm-up.

| TLS scenario | Rate | Mean | Failed |
|---|---|---|---|
| Full TLS 1.3 handshake + `GET /` | 86 handshakes/s | 11 ms | 0 of 40 |

**Environment:** Apple M5, macOS 26.5, Apple clang 21.0.0, `RelWithDebInfo`,
loopback. Numbers from a different machine or a Linux CI runner will differ —
compare against a baseline you took yourself on the same host.

### What these numbers are, and are not

`benchmark_run()` opens **a fresh TCP connection per request** and sends one
`GET`, **sequentially, from a single thread, over loopback**. So the figures are:

- a **per-request latency floor** — real networks add far more than 21 µs;
- a **single-client throughput ceiling** — not a capacity number.

Nothing here measures concurrent clients, keep-alive connection reuse, large
bodies, or behaviour under load. The threaded server runs a 16-worker pool that
this benchmark never exercises in parallel. Treat a regression in these figures
as a signal worth investigating, not as a production SLO, and do not quote them
as "the library handles N requests per second".

One observation worth keeping: JSON serialisation costs essentially nothing
against the plain-text route (within run-to-run noise), which says the JSON path
is not doing anything pathological with allocation.

### Why TLS is measured differently

The in-process benchmark client speaks plain HTTP over a raw socket and **cannot
be extended to HTTPS**: this library implements TLS 1.3 **server-side only** —
there is no TLS client anywhere in the tree, and writing one is out of scope
(see [`../src/tls/README.md`](../src/tls/README.md)). The only way to exercise
the TLS path is to drive it with a real external client, which is exactly what
`tests/interop_openssl.sh` already does for correctness.

`tests/benchmark_tls.sh` therefore spawns a fresh `openssl s_client` per
iteration. Each one performs a complete handshake — X25519 key exchange, Ed25519
signature, the key schedule — plus one `GET` over the encrypted channel.

**That rate includes `openssl` process spawn (fork + exec + init), which
dominates it.** Read 86 handshakes/s as a conservative floor and a regression
signal, not as the speed of the C crypto code. Removing that overhead would
require an in-process TLS client, which does not exist.

The TLS layer remains **EXPERIMENTAL and UNAUDITED**; performance figures say
nothing about its security.

## Known gaps

These are deliberate omissions, not oversights:

- **No concurrency benchmark.** The most useful missing number: how the 16-worker
  pool behaves under parallel load.
- **No keep-alive benchmark.** Every request pays a fresh TCP connect, so
  connection reuse — which the server supports — is unmeasured.
- **Async mode is unmeasured by this runner.** `http_server_listen()` runs the
  event loop on the calling thread and never returns, so a single-process runner
  has no thread left to drive the client. Run `examples/async_server` in one
  terminal and a client in another.
- **No CI trend.** Nothing stores these over time, so a slow drift across many
  commits would go unnoticed even though a sudden regression would be visible to
  anyone who runs the tool.
