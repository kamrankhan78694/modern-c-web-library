# AI Inference Serving — Modern C Web Library

> **Status**: Planned for Phase 16 (v1.6.0). This document describes the architecture and deployment patterns for AI inference serving using the Modern C Web Library.

---

## Overview

The Modern C Web Library is being designed to be the **world's fastest backend for serving AI model predictions** — purpose-built for LLMs and ML models, implemented entirely in pure C with zero external dependencies.

### Why Pure C for AI Serving?

| Advantage | Detail |
|-----------|--------|
| **Minimal latency** | No garbage collection, no runtime overhead, no interpreter |
| **Memory efficiency** | Arena allocators eliminate per-request malloc overhead |
| **Hardware proximity** | Direct access to AES-NI, AVX2, NEON SIMD instructions |
| **Edge deployment** | Run on Raspberry Pi, embedded devices, IoT gateways |
| **Integration** | Native C ABI compatible with llama.cpp, ONNX Runtime C API, TensorRT |

---

## AI Inference Architecture (Planned)

### Request Flow

```
AI Client (HTTP/SSE)
    │
    ▼
┌─────────────────────────┐
│  HTTP Server (io_uring)  │  Phase 12: 1M+ RPS
└────────────┬────────────┘
             │
             ▼
┌─────────────────────────┐
│  Model Router Middleware │  Phase 16: /v1/models/:name/predict
│  (route by path/header)  │
└────────────┬────────────┘
             │
             ▼
┌─────────────────────────┐
│  Batch Coalescer        │  Phase 16: Collect requests within time window
│  (configurable window)   │  Reduces model invocations 10-100x
└────────────┬────────────┘
             │
             ▼
┌─────────────────────────┐
│  Inference Queue        │  Phase 16: Bounded queue with backpressure
│  (503 + Retry-After      │  Returns 503 when overloaded
│   when full)             │
└────────────┬────────────┘
             │
             ▼
┌─────────────────────────┐
│  Model Handler          │  User-provided inference function
│  (llama.cpp, ONNX, etc) │  Receives batched inputs
└────────────┬────────────┘
             │
             ▼
┌─────────────────────────┐
│  SSE Token Streamer     │  Phase 16: Stream tokens as they're generated
│  (text/event-stream)     │  LLM-optimized chunked output
└─────────────────────────┘
```

### Key Components

#### Server-Sent Events (SSE)

For LLM token-by-token streaming:

```c
// Planned API (Phase 16)
void handle_predict(http_request_t *req, http_response_t *res) {
    sse_stream_t *stream = sse_stream_create(res);

    // Start inference (pseudo-code)
    for (each token from model) {
        sse_stream_send(stream, "token", token_text);
    }

    sse_stream_send(stream, "done", "");
    sse_stream_close(stream);
}
```

Client-side consumption:

```javascript
const source = new EventSource('/v1/models/llama/predict?prompt=Hello');
source.addEventListener('token', (e) => {
    process.stdout.write(e.data);
});
source.addEventListener('done', () => {
    source.close();
});
```

#### Batch Request Coalescing

Middleware that collects multiple inference requests within a configurable time window:

```c
// Planned API (Phase 16)
batch_config_t config = {
    .window_ms = 50,           // Collect requests for 50ms
    .max_batch_size = 32,      // Maximum batch size
    .timeout_ms = 5000         // Request timeout
};
middleware_fn_t batch_mw = batch_middleware_create(&config);
router_use_middleware(router, batch_mw);
```

#### Tensor Binary Protocol

Efficient binary serialization for model inputs/outputs:

```
Content-Type: application/x-tensor

┌──────────────────────────────────────────┐
│ Header (16 bytes)                         │
│  ├── magic: 0x54454E53 ("TENS")          │
│  ├── version: uint16                      │
│  ├── dtype: uint16 (float16/32, int8)    │
│  ├── ndim: uint16                         │
│  └── reserved: uint16                     │
├──────────────────────────────────────────┤
│ Shape (ndim × 8 bytes)                    │
│  └── dimensions: uint64[]                 │
├──────────────────────────────────────────┤
│ Data (contiguous, zero-copy)              │
│  └── raw tensor data                      │
└──────────────────────────────────────────┘
```

#### Model Routing

Route requests to different model handlers:

```c
// Planned API (Phase 16)
router_add_route(router, HTTP_POST, "/v1/models/:model_name/predict", model_predict_handler);
router_add_route(router, HTTP_GET,  "/v1/models/:model_name/info",    model_info_handler);
router_add_route(router, HTTP_GET,  "/v1/models",                     model_list_handler);
```

---

## Deployment Patterns

### Pattern 1: llama.cpp Integration

The most anticipated deployment pattern — combining the fastest pure C web library with the most popular C/C++ LLM inference engine:

```
┌──────────┐     ┌───────────────────┐     ┌──────────────┐
│ Clients  │────▶│ Modern C Web Lib  │────▶│ llama.cpp    │
│ (HTTP)   │◀────│ - SSE streaming   │◀────│ - LLM model  │
│          │     │ - Batch coalescing│     │ - GGUF format│
│          │     │ - Rate limiting   │     │              │
└──────────┘     └───────────────────┘     └──────────────┘
```

```c
// Example: llama.cpp integration (pseudo-code)
#include "weblib.h"
#include "llama.h"  // llama.cpp header

static llama_model *model;
static llama_context *ctx;

void handle_predict(http_request_t *req, http_response_t *res) {
    const char *prompt = http_request_get_form_field(req, "prompt");

    sse_stream_t *stream = sse_stream_create(res);

    // Tokenize and generate
    // ... llama.cpp inference loop ...
    // For each generated token:
    //   sse_stream_send(stream, "token", token_text);

    sse_stream_close(stream);
}
```

### Pattern 2: ONNX Runtime Integration

For traditional ML models (classification, regression, embeddings):

```
┌──────────┐     ┌───────────────────┐     ┌──────────────┐
│ Clients  │────▶│ Modern C Web Lib  │────▶│ ONNX Runtime │
│ (HTTP)   │◀────│ - Tensor protocol │◀────│ C API        │
│          │     │ - Batch coalescing│     │ - .onnx model│
└──────────┘     └───────────────────┘     └──────────────┘
```

### Pattern 3: Edge/IoT Inference

Run AI inference on resource-constrained devices:

```bash
# Cross-compile for ARM
arm-linux-gnueabihf-gcc -O2 -o ai_server ai_server.c -lweblib -lpthread

# Deploy to Raspberry Pi
scp ai_server pi@raspberry:/opt/ai/
ssh pi@raspberry '/opt/ai/ai_server 8080'
```

Memory footprint target: <10 MB RSS for basic inference server.

### Pattern 4: Multi-Model Serving

Serve multiple models from a single server instance:

```c
router_add_route(router, HTTP_POST, "/v1/models/llama-7b/predict",   llama_handler);
router_add_route(router, HTTP_POST, "/v1/models/whisper/transcribe", whisper_handler);
router_add_route(router, HTTP_POST, "/v1/models/clip/embed",         clip_handler);
router_add_route(router, HTTP_GET,  "/v1/models",                    list_models_handler);
```

---

## Performance Targets

| Metric | Target | Phase |
|--------|--------|-------|
| Inference request routing | <1 μs overhead | Phase 11 (arena allocator) |
| SSE token delivery latency | <100 μs per token | Phase 16 |
| Batch coalescing overhead | <1 ms | Phase 16 |
| Tensor deserialization | Zero-copy | Phase 16 |
| Concurrent SSE streams | 10,000+ | Phase 12 (io_uring) |
| GPU DMA buffer alignment | Page-aligned | Phase 16 |

---

## Non-Goals

- **Model training**: This library is for inference serving only
- **Model format conversion**: Use external tools (llama.cpp quantize, ONNX converter)
- **GPU compute**: Inference computation is delegated to model libraries (llama.cpp, ONNX Runtime)
- **Model management**: Downloading, versioning, and storing models is out of scope

---

## Related Documentation

- [ARCHITECTURE.md](ARCHITECTURE.md) — System architecture overview
- [BENCHMARKS.md](BENCHMARKS.md) — Performance benchmarks and targets
- [NEXT_PHASE.md](NEXT_PHASE.md) — Detailed implementation roadmap
- [docs/tutorials/ai-inference.md](docs/tutorials/ai-inference.md) — AI Inference tutorial
- [docs/DEPLOYMENT.md](docs/DEPLOYMENT.md) — Deployment guide

---

**Last Updated**: February 2026
**Status**: Planned — Phase 16 (v1.6.0)
