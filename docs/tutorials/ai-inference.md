# Tutorial: AI Inference Serving with Modern C Web Library

> **Status**: This tutorial describes the planned AI inference serving capabilities (Phase 16, v1.6.0). The APIs shown are planned and not yet implemented.

---

## Overview

This tutorial demonstrates how to build an AI inference endpoint using the Modern C Web Library. By the end, you'll understand how to:

1. Create an HTTP endpoint that accepts inference requests
2. Stream model outputs using Server-Sent Events (SSE)
3. Use batch request coalescing to improve throughput
4. Implement model routing for multi-model serving

---

## Prerequisites

- Modern C Web Library v1.6.0+ (Phase 16)
- C compiler (GCC 7+ or Clang 6+)
- CMake 3.10+
- (Optional) An AI model library (llama.cpp, ONNX Runtime C API)

---

## Step 1: Basic Inference Endpoint

Start with a simple endpoint that accepts a prompt and returns a prediction:

```c
#include "weblib.h"

void handle_predict(http_request_t *req, http_response_t *res) {
    /* Parse the request body as JSON */
    const char *body = req->body;
    json_value_t *input = json_parse(body);

    if (!input) {
        http_response_send_text(res, HTTP_BAD_REQUEST, "Invalid JSON");
        return;
    }

    json_value_t *prompt = json_object_get(input, "prompt");
    if (!prompt) {
        json_value_free(input);
        http_response_send_text(res, HTTP_BAD_REQUEST, "Missing 'prompt' field");
        return;
    }

    /* Perform inference (replace with actual model call) */
    const char *result = "This is a sample model output.";

    /* Build response */
    json_value_t *response = json_object_create();
    json_object_set(response, "model", json_string_create("my-model"));
    json_object_set(response, "output", json_string_create(result));

    http_response_send_json(res, HTTP_OK, response);

    json_value_free(response);
    json_value_free(input);
}

int main(void) {
    http_server_t *server = http_server_create();
    router_t *router = router_create();

    router_add_route(router, HTTP_POST, "/v1/predict", handle_predict);

    http_server_set_router(server, router);
    http_server_listen(server, 8080);

    router_destroy(router);
    http_server_destroy(server);
    return 0;
}
```

**Test it:**
```bash
curl -X POST http://localhost:8080/v1/predict \
  -H "Content-Type: application/json" \
  -d '{"prompt": "Hello, world!"}'
```

---

## Step 2: SSE Token Streaming

For LLM inference, you want to stream tokens as they're generated rather than waiting for the complete response:

```c
#include "weblib.h"

void handle_stream(http_request_t *req, http_response_t *res) {
    /* Create an SSE stream (planned Phase 16 API) */
    sse_stream_t *stream = sse_stream_create(res);

    /* Simulate token-by-token generation */
    const char *tokens[] = {"Hello", " ", "world", "!", NULL};

    for (int i = 0; tokens[i] != NULL; i++) {
        sse_stream_send(stream, "token", tokens[i]);
        /* In real usage, tokens come from model inference */
    }

    /* Signal completion */
    sse_stream_send(stream, "done", "[DONE]");
    sse_stream_close(stream);
}

int main(void) {
    http_server_t *server = http_server_create();
    router_t *router = router_create();

    router_add_route(router, HTTP_POST, "/v1/stream", handle_stream);

    http_server_set_router(server, router);
    http_server_listen(server, 8080);

    router_destroy(router);
    http_server_destroy(server);
    return 0;
}
```

**Client-side (JavaScript):**
```javascript
const response = await fetch('/v1/stream', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ prompt: 'Tell me a story' })
});

const reader = response.body.getReader();
const decoder = new TextDecoder();

while (true) {
    const { done, value } = await reader.read();
    if (done) break;
    const text = decoder.decode(value);
    // Parse SSE events from text
    process.stdout.write(text);
}
```

---

## Step 3: Batch Request Coalescing

When multiple clients send inference requests simultaneously, batch them together for efficient GPU utilization:

```c
#include "weblib.h"

int main(void) {
    http_server_t *server = http_server_create();
    router_t *router = router_create();

    /* Configure batch coalescing (planned Phase 16 API) */
    batch_config_t batch_cfg = {
        .window_ms = 50,          /* Collect requests for 50ms */
        .max_batch_size = 32,     /* Max 32 requests per batch */
        .timeout_ms = 5000        /* 5 second timeout per request */
    };
    middleware_fn_t batch_mw = batch_middleware_create(&batch_cfg);
    router_use_middleware(router, batch_mw);

    router_add_route(router, HTTP_POST, "/v1/predict", handle_predict);

    http_server_set_router(server, router);
    http_server_listen(server, 8080);

    router_destroy(router);
    http_server_destroy(server);
    return 0;
}
```

The batch middleware automatically:
1. Collects incoming requests within the time window
2. Groups them into a single batch
3. Dispatches the batch to the handler
4. Distributes results back to individual clients

---

## Step 4: Model Routing

Serve multiple models from a single server:

```c
#include "weblib.h"

void handle_llama(http_request_t *req, http_response_t *res) {
    /* LLaMA model inference */
    http_response_send_text(res, HTTP_OK, "LLaMA model response");
}

void handle_whisper(http_request_t *req, http_response_t *res) {
    /* Whisper model inference */
    http_response_send_text(res, HTTP_OK, "Whisper transcription");
}

void handle_models(http_request_t *req, http_response_t *res) {
    json_value_t *models = json_array_create();
    json_array_append(models, json_string_create("llama-7b"));
    json_array_append(models, json_string_create("whisper-base"));

    json_value_t *response = json_object_create();
    json_object_set(response, "models", models);

    http_response_send_json(res, HTTP_OK, response);
    json_value_free(response);
}

int main(void) {
    http_server_t *server = http_server_create();
    router_t *router = router_create();

    /* Model-specific routes */
    router_add_route(router, HTTP_POST, "/v1/models/llama-7b/predict", handle_llama);
    router_add_route(router, HTTP_POST, "/v1/models/whisper/predict", handle_whisper);
    router_add_route(router, HTTP_GET,  "/v1/models", handle_models);

    http_server_set_router(server, router);
    http_server_listen(server, 8080);

    router_destroy(router);
    http_server_destroy(server);
    return 0;
}
```

---

## Step 5: Production Configuration

For production AI inference deployment:

```c
#include "weblib.h"
#include <signal.h>

static http_server_t *g_server = NULL;

void sighandler(int sig) {
    (void)sig;
    if (g_server) http_server_shutdown(g_server, 10);
}

int main(void) {
    g_server = http_server_create();
    router_t *router = router_create();

    signal(SIGINT, sighandler);
    signal(SIGTERM, sighandler);

    /* Production settings */
    http_server_set_thread_count(g_server, 16);
    http_server_set_timeout(g_server, 60, 60);  /* Longer for inference */

    /* Middleware stack */
    log_config_t log_cfg = { .level = LOG_LEVEL_INFO, .output = stderr };
    router_use_middleware(router, log_middleware_create(&log_cfg));
    router_use_middleware(router, error_handler_middleware_create(NULL));

    /* Health check */
    health_check_register(router);

    /* Model routes */
    router_add_route(router, HTTP_POST, "/v1/predict", handle_predict);

    http_server_set_router(g_server, router);
    http_server_listen(g_server, 8080);

    router_destroy(router);
    http_server_destroy(g_server);
    return 0;
}
```

---

## What's Next

- [Agent Orchestration Tutorial](agent-orchestration.md) — Build multi-agent AI systems
- [REST API Tutorial](rest-api.md) — Build a complete REST API
- [Getting Started Tutorial](getting-started.md) — Getting started with the library
- [AI Serving Architecture](../../AI_SERVING.md) — Detailed architecture documentation

---

**Last Updated**: February 2026
**Status**: Planned — Phase 16 (v1.6.0)
