# Tutorial: AI Agent Orchestration with Modern C Web Library

> **Status**: This tutorial describes the planned agent orchestration capabilities (Phase 18, v1.8.0). The APIs shown are planned and not yet implemented.

---

## Overview

This tutorial demonstrates how to build a multi-agent AI orchestration server using the Modern C Web Library. By the end, you'll understand how to:

1. Register AI agents with the server
2. Route messages between agents using JSON-RPC 2.0
3. Implement tool calling for agent capabilities
4. Manage agent context windows
5. Monitor agent health and status

---

## Prerequisites

- Modern C Web Library v1.8.0+ (Phase 18)
- C compiler (GCC 7+ or Clang 6+)
- CMake 3.10+

---

## Concepts

### What is Agent Orchestration?

Agent orchestration is the coordination of multiple AI agents that communicate with each other to accomplish complex tasks. Each agent has:

- **Identity**: A unique ID and role description
- **Capabilities**: Tools it can call (web search, code execution, etc.)
- **Context**: A sliding window of conversation history
- **Communication**: JSON-RPC 2.0 messages over WebSocket

### Architecture

```
┌──────────┐     ┌───────────────────────────┐     ┌──────────┐
│ Agent A  │◀───▶│  Modern C Web Library     │◀───▶│ Agent B  │
│(WebSocket)│     │  Agent Orchestration      │     │(WebSocket)│
└──────────┘     │  Server                   │     └──────────┘
                 │  ┌─────────────────────┐  │
┌──────────┐     │  │ Agent Registry      │  │     ┌──────────┐
│ Agent C  │◀───▶│  │ Message Router      │  │◀───▶│ Tool     │
│(WebSocket)│     │  │ Tool Dispatcher     │  │     │ Handlers │
└──────────┘     │  │ Context Manager     │  │     └──────────┘
                 │  └─────────────────────┘  │
                 └───────────────────────────┘
```

---

## Step 1: Basic Agent Server

Create a server that accepts agent connections:

```c
#include "weblib.h"

/* Agent registry (planned Phase 18 API) */
static agent_registry_t *registry;

void handle_agent_connect(http_request_t *req, http_response_t *res) {
    /* WebSocket upgrade for agent connection */
    if (!websocket_handle_upgrade(req, res)) {
        return;
    }

    /* Agent registers itself via first JSON-RPC message */
}

int main(void) {
    http_server_t *server = http_server_create();
    router_t *router = router_create();

    /* Create agent registry */
    registry = agent_registry_create();

    /* Agent WebSocket endpoint */
    router_add_route(router, HTTP_GET, "/agents/:agent_id", handle_agent_connect);

    /* Agent status dashboard */
    router_add_route(router, HTTP_GET, "/agents/status", handle_agent_status);

    http_server_set_router(server, router);
    http_server_listen(server, 8080);

    agent_registry_destroy(registry);
    router_destroy(router);
    http_server_destroy(server);
    return 0;
}
```

---

## Step 2: JSON-RPC 2.0 Messages

Agents communicate using JSON-RPC 2.0 over WebSocket:

### Request
```json
{
    "jsonrpc": "2.0",
    "method": "chat",
    "params": {
        "from": "agent-planner",
        "to": "agent-researcher",
        "message": "Find information about quantum computing"
    },
    "id": 1
}
```

### Response
```json
{
    "jsonrpc": "2.0",
    "result": {
        "message": "Here are the key findings about quantum computing...",
        "sources": ["arxiv:2301.12345", "nature:quantum-2024"]
    },
    "id": 1
}
```

### Notification (no response expected)
```json
{
    "jsonrpc": "2.0",
    "method": "status_update",
    "params": {
        "agent_id": "agent-researcher",
        "status": "processing",
        "progress": 0.45
    }
}
```

---

## Step 3: Tool Calling

Agents can call tools registered with the server:

```c
#include "weblib.h"

/* Tool handler: web search */
void tool_web_search(json_value_t *args, json_value_t **result) {
    json_value_t *query = json_object_get(args, "query");
    /* Perform search (mock implementation) */

    *result = json_object_create();
    json_object_set(*result, "results", json_array_create());
    /* ... populate results ... */
}

/* Tool handler: code execution */
void tool_run_code(json_value_t *args, json_value_t **result) {
    json_value_t *code = json_object_get(args, "code");
    json_value_t *language = json_object_get(args, "language");
    /* Execute code in sandbox (mock implementation) */

    *result = json_object_create();
    json_object_set(*result, "output", json_string_create("Code executed successfully"));
}

int main(void) {
    /* ... server setup ... */

    /* Register tools (planned Phase 18 API) */
    agent_registry_register_tool(registry, "web_search", tool_web_search);
    agent_registry_register_tool(registry, "run_code", tool_run_code);

    /* ... start server ... */
}
```

### Tool Call Protocol

Agent sends:
```json
{
    "jsonrpc": "2.0",
    "method": "tool_call",
    "params": {
        "tool": "web_search",
        "args": { "query": "quantum computing breakthroughs 2026" }
    },
    "id": 42
}
```

Server responds:
```json
{
    "jsonrpc": "2.0",
    "result": {
        "tool": "web_search",
        "output": { "results": [...] }
    },
    "id": 42
}
```

---

## Step 4: Context Window Management

The server manages per-agent context windows:

```c
/* Context configuration (planned Phase 18 API) */
typedef struct {
    size_t max_tokens;           /* Maximum context window size */
    const char *truncation;      /* "fifo" or "summarize" */
} context_config_t;

/* Configure context management */
context_config_t ctx_cfg = {
    .max_tokens = 4096,
    .truncation = "fifo"         /* Remove oldest messages first */
};
agent_registry_set_context_config(registry, &ctx_cfg);
```

The context manager automatically:
1. Tracks conversation history per agent session
2. Truncates context when it exceeds `max_tokens`
3. Supports FIFO (remove oldest) and summarize-and-compact strategies

---

## Step 5: Agent Health Monitoring

Monitor agent health with built-in heartbeat:

```c
/* Health monitoring is automatic (planned Phase 18 API) */
/* Agents receive periodic ping; server tracks last response time */

void handle_agent_status(http_request_t *req, http_response_t *res) {
    /* Returns status of all registered agents */
    json_value_t *status = agent_registry_get_status(registry);
    http_response_send_json(res, HTTP_OK, status);
    json_value_free(status);
}
```

**Status endpoint response:**
```json
{
    "agents": [
        {
            "id": "agent-planner",
            "status": "active",
            "last_heartbeat": "2026-02-23T19:30:00Z",
            "messages_sent": 42,
            "messages_received": 38,
            "context_usage": 0.67
        },
        {
            "id": "agent-researcher",
            "status": "active",
            "last_heartbeat": "2026-02-23T19:30:01Z",
            "messages_sent": 156,
            "messages_received": 160,
            "context_usage": 0.89
        }
    ],
    "total_agents": 2,
    "total_messages": 396
}
```

---

## Step 6: Multi-Agent Chat Example

A complete example with a planner agent and a researcher agent:

```c
#include "weblib.h"
#include <signal.h>

static http_server_t *g_server = NULL;
static agent_registry_t *registry = NULL;

void sighandler(int sig) {
    (void)sig;
    if (g_server) http_server_shutdown(g_server, 10);
}

void handle_agent_ws(http_request_t *req, http_response_t *res) {
    const char *agent_id = http_request_get_param(req, "agent_id");
    if (!websocket_handle_upgrade(req, res)) return;
    /* Agent registration happens via JSON-RPC "register" method */
}

void handle_status(http_request_t *req, http_response_t *res) {
    json_value_t *status = agent_registry_get_status(registry);
    http_response_send_json(res, HTTP_OK, status);
    json_value_free(status);
}

void tool_web_search(json_value_t *args, json_value_t **result) {
    *result = json_object_create();
    json_object_set(*result, "results",
        json_string_create("Mock search results for agent orchestration"));
}

int main(void) {
    g_server = http_server_create();
    router_t *router = router_create();
    registry = agent_registry_create();

    signal(SIGINT, sighandler);
    signal(SIGTERM, sighandler);

    /* Server configuration */
    http_server_set_thread_count(g_server, 8);
    http_server_set_timeout(g_server, 60, 60);

    /* Middleware */
    log_config_t log_cfg = { .level = LOG_LEVEL_INFO, .output = stderr };
    router_use_middleware(router, log_middleware_create(&log_cfg));

    /* Register tools */
    agent_registry_register_tool(registry, "web_search", tool_web_search);

    /* Routes */
    router_add_route(router, HTTP_GET, "/agents/:agent_id", handle_agent_ws);
    router_add_route(router, HTTP_GET, "/agents/status", handle_status);
    health_check_register(router);

    http_server_set_router(g_server, router);
    printf("Agent orchestration server running on port 8080\n");
    http_server_listen(g_server, 8080);

    agent_registry_destroy(registry);
    router_destroy(router);
    http_server_destroy(g_server);
    return 0;
}
```

---

## What's Next

- [AI Inference Tutorial](ai-inference.md) — Build AI inference endpoints
- [REST API Tutorial](rest-api.md) — Build a complete REST API
- [WebSocket Tutorial](websocket.md) — WebSocket fundamentals
- [Agent Orchestration Architecture](../../AI_SERVING.md) — Detailed architecture documentation

---

**Last Updated**: February 2026
**Status**: Planned — Phase 18 (v1.8.0)
