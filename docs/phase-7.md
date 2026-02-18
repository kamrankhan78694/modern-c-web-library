# Phase 7: Competitive Edge — Features No One Else Has (v0.7.0 Target)

## Mission Statement

Phase 7 transforms the Modern C Web Library from a capable pure-C web framework into the **only C web framework that offers features previously exclusive to higher-level ecosystems** — while maintaining the zero-dependency, pure-C philosophy that defines this project.

Every feature in this phase was selected by answering one question:  
**"Does any existing C web server/framework already provide this?"**  
If the answer is yes, it doesn't belong here. If the answer is no, it's a competitive edge.

---

## Competitive Landscape Analysis

Before planning, we analyzed every major C/C++ web framework:

| Framework       | License      | TLS         | HTTP/2 | WebSocket | Async    | Embedded DB | Hot Reload | API Docs Gen | Observability | Plugin System |
|-----------------|-------------|-------------|--------|-----------|----------|-------------|------------|--------------|---------------|---------------|
| **Mongoose**    | GPL/Commercial | mbedTLS   | No     | Yes       | Event    | No          | No         | No           | No            | No            |
| **Civetweb**    | MIT          | OpenSSL     | No     | Yes       | Both     | No          | No         | No           | No            | CGI only      |
| **H2O**         | MIT          | Built-in    | Yes    | Limited   | Event    | No          | No         | No           | No            | No            |
| **facil.io**    | MIT          | OpenSSL     | No     | Yes       | Event    | No          | No         | No           | No            | No            |
| **libmicrohttpd** | LGPL      | GnuTLS      | No     | No        | Both     | No          | No         | No           | No            | No            |
| **Kore**        | ISC          | Built-in    | No     | Yes       | Workers  | No          | No         | No           | No            | Modules       |
| **lwan**        | GPL          | mbedTLS     | No     | No        | Event    | No          | No         | No           | No            | No            |
| **MCWL (Ours)** | MIT          | Planned     | No     | Yes       | Both     | Pool only   | No         | No           | No            | No            |

**Key Finding**: No C web framework offers auto-generated API documentation, built-in observability, coroutine-style async, runtime plugin loading, structured logging, or self-describing APIs. These are gaps we can own.

**Extended Finding**: Looking beyond C into ALL web frameworks (Node.js, Python, Go, Rust, Java), we identified additional capabilities that **no framework in any language** provides as built-in features: request recording & replay, embedded chaos/fault injection, automatic API contract drift detection, per-route resource budgets, built-in idempotency, in-process shadow/canary testing, and runtime diagnostic consoles. These are currently only available via external tooling, service meshes, or cloud platform services. Building them into the framework itself is unprecedented.

---

## Phase 7 Features

### 7.1 🏆 Self-Documenting API with Auto-Generated REST Documentation

**Why This Is Unique**: No C web framework generates API documentation from code. Python has FastAPI/Swagger, Go has swag, Rust has utoipa — but C has nothing. We would be the **first**.

**What It Does**:
- Routes registered with `router_add_route()` are automatically collected into a machine-readable API registry
- A built-in `GET /__docs` endpoint serves auto-generated HTML documentation
- A built-in `GET /__openapi.json` endpoint serves an OpenAPI 3.0 spec
- Developers can annotate routes with descriptions, parameter types, and response schemas at registration time
- Zero manual documentation maintenance — docs always match the running server

**API Design**:
```c
// Enhanced route registration with documentation metadata
route_doc_t doc = {
    .summary = "Get user by ID",
    .description = "Retrieves a user profile by their unique identifier",
    .params = (param_doc_t[]){
        { .name = "id", .in = PARAM_PATH, .type = "integer", .required = true,
          .description = "Unique user identifier" },
        { 0 }  // sentinel
    },
    .response_description = "User object with profile data",
    .response_content_type = "application/json",
    .tags = "users"
};
router_add_route_documented(router, HTTP_GET, "/users/:id", handle_user, &doc);

// Serve docs at runtime
router_enable_api_docs(router, "/__docs", "My API v1.0");
```

**Implementation Strategy**:
- `route_doc_t` struct stores metadata alongside each route in the existing route table
- `router_enable_api_docs()` registers two internal routes (`/__docs` and `/__openapi.json`)
- OpenAPI 3.0 JSON is generated on-the-fly using the existing JSON serializer
- HTML documentation page is embedded as a C string constant (minimal, self-contained HTML+CSS)
- No external tools, no build step, no code generation — purely runtime

**Files Affected**:
- `include/weblib.h` — `route_doc_t`, `param_doc_t`, `router_add_route_documented()`, `router_enable_api_docs()`
- `src/router.c` — Documentation metadata storage and retrieval
- `src/api_docs.c` (new) — OpenAPI JSON generator and HTML renderer
- `tests/test_weblib.c` — API docs generation tests

**Acceptance Criteria**:
- `GET /__openapi.json` returns valid OpenAPI 3.0 JSON for all documented routes
- `GET /__docs` returns human-readable HTML documentation
- Undocumented routes still work but appear with minimal info in docs
- Zero runtime overhead when docs endpoint is not enabled

---

### 7.2 🏆 Built-In Structured Logging & Observability Pipeline

**Why This Is Unique**: No C web framework has built-in structured logging. All rely on `printf`/`fprintf` with ad-hoc formats. Enterprise frameworks in Go (zerolog, zap), Node (pino, winston), and Rust (tracing) have moved to structured JSON logging — C web frameworks have not.

**What It Does**:
- Structured log entries as key-value pairs (output as JSON or human-readable)
- Request-scoped context (request ID, method, path, duration auto-attached)
- Log levels: TRACE, DEBUG, INFO, WARN, ERROR, FATAL
- Pluggable log sinks (stdout, file, ringbuffer for programmatic access)
- Request lifecycle tracing (start → route matched → handler entered → response sent)
- Built-in request metrics (latency histogram, status code counters, active connections)
- `GET /__metrics` endpoint exposing Prometheus-compatible metrics text
- Zero-allocation fast path for disabled log levels

**API Design**:
```c
// Configure logging
log_config_t log_cfg = {
    .level = LOG_INFO,
    .format = LOG_FORMAT_JSON,     // or LOG_FORMAT_TEXT
    .output = LOG_OUTPUT_STDOUT,   // or LOG_OUTPUT_FILE
    .file_path = "/var/log/myapp.log",
    .include_timestamp = true,
    .include_source_location = true
};
http_server_set_logging(server, &log_cfg);

// Structured logging in handlers
void handle_user(http_request_t *req, http_response_t *res) {
    const char *id = http_request_get_param(req, "id");

    // Structured log with request context auto-attached
    LOG_INFO(req, "user.fetch", "id", id, "source", "api");
    // Output: {"level":"INFO","ts":"2026-02-11T19:00:00Z","msg":"user.fetch",
    //          "id":"42","source":"api","req_id":"abc123","method":"GET",
    //          "path":"/users/42","remote_addr":"10.0.0.1"}

    http_response_send_text(res, HTTP_OK, "OK");
}

// Enable metrics endpoint
http_server_enable_metrics(server, "/__metrics");
// GET /__metrics returns:
//   http_requests_total{method="GET",path="/users/:id",status="200"} 1523
//   http_request_duration_seconds{quantile="0.5"} 0.003
//   http_request_duration_seconds{quantile="0.99"} 0.042
//   http_active_connections 12
```

**Implementation Strategy**:
- Log entries are stack-allocated structs with key-value pair arrays (zero heap allocation for hot path)
- Log level check is a single integer comparison (compiled out at higher levels)
- Request context pointer carries auto-generated request ID, start timestamp
- Metrics stored in lock-free atomic counters (per-route, per-status-code)
- Prometheus text format is simple key-value text — no external library needed
- Ringbuffer sink allows programmatic log access for testing and diagnostics

**Files Affected**:
- `include/weblib.h` — Log types, macros, config structs, metrics API
- `src/logging.c` (new) — Structured logging engine
- `src/metrics.c` (new) — Request metrics collection and Prometheus exporter
- `src/http_server.c` — Request lifecycle instrumentation hooks
- `tests/test_weblib.c` — Logging and metrics tests

**Acceptance Criteria**:
- JSON log output parseable by standard log aggregators (ELK, Loki, Datadog)
- Metrics endpoint produces valid Prometheus text exposition format
- Logging overhead < 100ns per disabled log call
- No heap allocations in logging hot path
- Thread-safe for concurrent request logging

---

### 7.3 🏆 Coroutine-Style Async Handlers (Stackless Coroutines in Pure C)

**Why This Is Unique**: No C web framework offers coroutine-based async request handling. Drogon (C++) uses C++20 coroutines, but no pure-C framework has this. All C frameworks either use threads or raw callback-based event loops with manual state machines.

**What It Does**:
- Handlers can `yield` mid-execution, allowing other requests to be processed
- Eliminates callback hell — write sequential-looking async code in C
- Stackless coroutines using Duff's device / `switch`-`case` state machine pattern
- Cooperative multitasking within the event loop — no OS threads needed
- Async sleep, async I/O wait, async database query (future integration point)

**API Design**:
```c
// Coroutine-style handler using ASYNC macros
ASYNC_HANDLER(handle_slow_query) {
    ASYNC_BEGIN(req, res);

    const char *id = http_request_get_param(req, "id");

    // Simulate async work — yields control, resumes when timer fires
    ASYNC_SLEEP(100);  // yield for 100ms, other requests proceed

    // Continues here after 100ms
    json_value_t *json = json_object_create();
    json_object_set(json, "id", json_string_create(id));
    json_object_set(json, "status", json_string_create("ready"));

    http_response_send_json(res, HTTP_OK, json);
    json_value_free(json);

    ASYNC_END();
}

// Register like any other handler
router_add_route(router, HTTP_GET, "/query/:id", handle_slow_query);
```

**Implementation Strategy**:
- Stackless coroutines via Duff's device (`switch(__state) { case 0: ... }`)
- Each coroutine state is stored in a heap-allocated context struct
- `ASYNC_BEGIN` / `ASYNC_END` / `ASYNC_SLEEP` / `ASYNC_YIELD` are macros that expand to `switch`/`case`/`return`
- Coroutine context is associated with the connection and re-invoked by the event loop
- Zero dependency on `setjmp`/`longjmp`, `ucontext`, or OS-specific fiber APIs
- Compatible with both threaded and async server modes
- Inspiration: Protothreads (Adam Dunkels), Simon Tatham's coroutines, `picohttpparser`

**Macro Definitions** (conceptual):
```c
#define ASYNC_BEGIN(req, res) \
    async_ctx_t *__ctx = http_request_get_async_ctx(req); \
    switch (__ctx->state) { case 0:

#define ASYNC_YIELD() \
    do { __ctx->state = __LINE__; return; case __LINE__:; } while(0)

#define ASYNC_SLEEP(ms) \
    do { __ctx->timer_ms = (ms); __ctx->state = __LINE__; return; \
         case __LINE__:; } while(0)

#define ASYNC_END() \
    __ctx->state = -1; }
```

**Files Affected**:
- `include/weblib.h` — Async handler macros, `async_ctx_t` struct
- `src/async_handler.c` (new) — Coroutine scheduler and context management
- `src/event_loop.c` — Timer-based coroutine resumption
- `src/http_server.c` — Coroutine-aware request dispatch
- `tests/test_weblib.c` — Coroutine handler tests

**Acceptance Criteria**:
- Async handlers compile and run on all supported platforms
- A single event loop thread handles 1000+ concurrent coroutine handlers
- No stack corruption or memory leaks with deeply nested async yields
- Coroutine overhead < 1μs per yield/resume cycle

---

### 7.4 🏆 Runtime Plugin System with Hot Module Loading

**Why This Is Unique**: No C web framework supports runtime-loaded plugins with hot reloading. Kore has compile-time modules, Apache/Nginx have static modules — but none let you drop a `.so`/`.dll` file and have new routes appear without restarting the server.

**What It Does**:
- Load route handlers and middleware from shared libraries (`.so` on Linux/macOS, `.dll` on Windows) at runtime
- Hot reload: replace a plugin without restarting the server (old connections drain, new requests go to updated plugin)
- Plugin discovery: scan a directory for plugin shared objects on startup
- Plugin lifecycle hooks: `plugin_init()`, `plugin_routes()`, `plugin_destroy()`
- Plugin isolation: each plugin has its own context, preventing one plugin from corrupting another
- Versioned plugin API for forward/backward compatibility

**API Design**:
```c
// Server-side: enable plugin loading
plugin_config_t plugin_cfg = {
    .plugin_dir = "./plugins",
    .auto_discover = true,
    .hot_reload = true,
    .watch_interval_ms = 2000  // check for changes every 2 seconds
};
http_server_enable_plugins(server, &plugin_cfg);

// Or load a specific plugin manually
plugin_handle_t *plugin = http_server_load_plugin(server, "./plugins/auth.so");
http_server_unload_plugin(server, plugin);
```

```c
// Plugin side (compiled as shared library):
// plugins/hello_plugin.c

#include "weblib.h"
#include "weblib_plugin.h"  // Plugin API header

PLUGIN_EXPORT int plugin_version(void) { return WEBLIB_PLUGIN_API_VERSION; }

PLUGIN_EXPORT int plugin_init(plugin_context_t *ctx) {
    // Initialize plugin resources
    return 0;
}

PLUGIN_EXPORT void plugin_routes(router_t *router) {
    router_add_route(router, HTTP_GET, "/hello-plugin", my_handler);
}

PLUGIN_EXPORT void plugin_destroy(plugin_context_t *ctx) {
    // Cleanup plugin resources
}
```

**Implementation Strategy**:
- Linux/macOS: `dlopen()` / `dlsym()` / `dlclose()` (POSIX `dlfcn.h`)
- Windows: `LoadLibrary()` / `GetProcAddress()` / `FreeLibrary()`
- File watcher: `inotify` (Linux), `kqueue` (macOS), `ReadDirectoryChangesW` (Windows), or polling fallback
- Hot reload: load new `.so`, migrate active connections to old handler, swap route table, unload old `.so` after drain
- Plugin API version check prevents loading incompatible plugins
- Plugin context provides sandboxed access to server APIs

**Files Affected**:
- `include/weblib.h` — Plugin config, handle types
- `include/weblib_plugin.h` (new) — Plugin author API
- `src/plugin_loader.c` (new) — Dynamic loading, hot reload, directory watching
- `src/router.c` — Plugin route table management
- `src/http_server.c` — Plugin lifecycle integration
- `tests/test_weblib.c` — Plugin load/unload/reload tests
- `examples/plugins/` (new) — Example plugins

**Acceptance Criteria**:
- Load a plugin `.so` and serve its routes without server restart
- Hot-reload a plugin with zero dropped requests (during drain period)
- Graceful handling of malformed/crashing plugins (no server crash)
- Plugin API version mismatch produces clear error message
- Works on Linux and macOS; Windows support via `LoadLibrary()`

---

### 7.5 🏆 Built-In Configuration System with Live Reload

**Why This Is Unique**: No C web framework has a built-in configuration system. All require developers to hand-write config file parsing or hard-code values. Go has Viper, Node has dotenv/convict — C has nothing.

**What It Does**:
- Parse configuration from multiple sources with priority: CLI args > environment variables > config file > defaults
- Support JSON config files (using existing JSON parser — zero new dependencies)
- Type-safe config access: string, integer, boolean, array
- Live reload: watch config file for changes and apply updates without restart
- Validation: required fields, value ranges, enum constraints
- Built-in config for all server settings (port, max connections, timeouts, log level, etc.)
- `GET /__config` debug endpoint showing active configuration (with sensitive values masked)

**API Design**:
```c
// Define configuration schema
config_schema_t schema[] = {
    CONFIG_INT("server.port", 8080, "Port to listen on"),
    CONFIG_INT("server.max_connections", 128, "Maximum concurrent connections"),
    CONFIG_INT("server.read_timeout_ms", 30000, "Read timeout in milliseconds"),
    CONFIG_STRING("server.name", "MCWL", "Server name for headers"),
    CONFIG_BOOL("logging.enabled", true, "Enable request logging"),
    CONFIG_STRING("logging.level", "info", "Log level: trace|debug|info|warn|error"),
    CONFIG_STRING("logging.format", "json", "Log format: json|text"),
    CONFIG_STRING("tls.cert_path", NULL, "Path to TLS certificate"),
    CONFIG_STRING("tls.key_path", NULL, "Path to TLS private key"),
    CONFIG_END()
};

// Load configuration
config_t *config = config_create(schema);
config_load_file(config, "server.json");     // Load from JSON file
config_load_env(config, "MCWL_");            // Override with env vars (MCWL_SERVER_PORT=9090)
config_load_argv(config, argc, argv);        // Override with CLI args (--server.port=9090)

// Access values
int port = config_get_int(config, "server.port");
const char *level = config_get_string(config, "logging.level");
bool logging = config_get_bool(config, "logging.enabled");

// Apply to server
http_server_apply_config(server, config);

// Enable live reload (watches config file for changes)
config_enable_live_reload(config, "server.json", on_config_change);

// Cleanup
config_destroy(config);
```

**Implementation Strategy**:
- Config schema is a static array of descriptors (type, key, default, description)
- JSON config file parsed using existing `json_parse()` — zero new parsers needed
- Environment variable mapping: `"server.port"` → `MCWL_SERVER_PORT` (prefix + uppercase + underscores)
- CLI argument parsing: `--server.port=8080` or `--server.port 8080`
- Live reload: file watcher (same as plugin system) + callback on change
- Config debug endpoint reuses API docs HTML renderer infrastructure

**Files Affected**:
- `include/weblib.h` — Config types, macros, access functions
- `src/config.c` (new) — Configuration loading, merging, validation, live reload
- `src/http_server.c` — `http_server_apply_config()` integration
- `tests/test_weblib.c` — Configuration parsing and override tests

**Acceptance Criteria**:
- JSON config files parsed correctly using existing JSON parser
- Environment variables override config file values
- CLI arguments override environment variables
- Invalid config values produce clear error messages with key name and expected type
- Live reload applies changes within 2 seconds of file modification
- `GET /__config` shows current config with sensitive values masked

---

### 7.6 🏆 Declarative Request Validation & Schema Enforcement

**Why This Is Unique**: No C web framework has request validation. Express has joi/zod, FastAPI has Pydantic, Go has go-playground/validator — but no C framework validates incoming request data against a schema before it reaches the handler.

**What It Does**:
- Declare expected request schema (query params, headers, body fields) at route registration
- Automatic validation before handler is invoked — invalid requests get 400/422 with detailed error JSON
- Type coercion: string query params auto-converted to int/bool where declared
- Required/optional field enforcement
- String constraints: min/max length, regex pattern matching
- Number constraints: min/max value, integer-only
- Reusable schema definitions for common patterns (pagination, auth headers, etc.)

**API Design**:
```c
// Define validation schema
request_schema_t user_create_schema = {
    .body_fields = (field_schema_t[]){
        { .name = "username", .type = FIELD_STRING, .required = true,
          .min_length = 3, .max_length = 32,
          .description = "Username (3-32 characters)" },
        { .name = "email", .type = FIELD_STRING, .required = true,
          .pattern = "^[^@]+@[^@]+\\.[^@]+$",
          .description = "Valid email address" },
        { .name = "age", .type = FIELD_INTEGER, .required = false,
          .min_value = 0, .max_value = 150,
          .description = "User age" },
        { 0 }  // sentinel
    },
    .query_params = (field_schema_t[]){
        { .name = "notify", .type = FIELD_BOOLEAN, .required = false,
          .description = "Send welcome notification" },
        { 0 }
    }
};

// Register route with validation
router_add_route_validated(router, HTTP_POST, "/users", handle_create_user,
                           &user_create_schema);

// handler receives only validated data — no manual checks needed
void handle_create_user(http_request_t *req, http_response_t *res) {
    // These are guaranteed to be present and valid
    const char *username = http_request_get_form_field(req, "username");
    const char *email = http_request_get_form_field(req, "email");
    // ... create user
}

// If validation fails, client automatically receives:
// 422 Unprocessable Entity
// {
//   "error": "Validation failed",
//   "details": [
//     {"field": "username", "message": "Required field missing"},
//     {"field": "email", "message": "Does not match pattern: ^[^@]+@[^@]+\\.[^@]+$"}
//   ]
// }
```

**Implementation Strategy**:
- Schema structs stored alongside routes (extends route table)
- Validation runs as an implicit middleware before the handler
- Regex matching via a minimal pure-C regex engine (subset: anchors, character classes, quantifiers, alternation)
- Validation errors collected into a JSON array and returned as 422 response
- Schemas also feed into the API documentation system (7.1) for automatic parameter documentation

**Synergy**: Validation schemas from 7.6 automatically appear in OpenAPI docs from 7.1 — a feature combination that no C framework can match.

**Files Affected**:
- `include/weblib.h` — Schema types, validation API
- `src/validation.c` (new) — Schema validation engine
- `src/regex_lite.c` (new) — Minimal regex engine for pattern matching
- `src/router.c` — Validation middleware integration
- `tests/test_weblib.c` — Validation tests (valid, invalid, edge cases)

**Acceptance Criteria**:
- Required fields enforced with clear error messages
- Type coercion works for string→int, string→bool
- Regex patterns validated correctly (email, UUID, dates)
- Validation errors return structured JSON with all failures (not just the first)
- Validated schemas automatically appear in API docs (7.1 integration)
- No heap allocations for validation of simple schemas

---

### 7.7 🏆 Built-In Health Check & Readiness Protocol

**Why This Is Unique**: While health check endpoints are common in managed cloud services, no C web framework provides a built-in, structured health check system with dependency probing, readiness gates, and standard response format. Developers always build this from scratch.

**What It Does**:
- `GET /__health` — Kubernetes-compatible health check with structured status
- Liveness probe: is the server process alive and accepting connections?
- Readiness probe: is the server ready to serve traffic? (all dependencies healthy)
- Pluggable health check providers: register custom checks (database connectivity, disk space, memory usage)
- Standard response format compatible with Kubernetes, AWS ALB, and Consul
- Configurable failure thresholds (N consecutive failures → unhealthy)

**API Design**:
```c
// Register health check providers
health_check_t db_check = {
    .name = "database",
    .check_fn = check_database_connection,
    .timeout_ms = 5000,
    .critical = true  // server reports unhealthy if this fails
};

health_check_t disk_check = {
    .name = "disk_space",
    .check_fn = check_disk_space,
    .timeout_ms = 1000,
    .critical = false  // warning only, server still serves traffic
};

http_server_add_health_check(server, &db_check);
http_server_add_health_check(server, &disk_check);
http_server_enable_health_endpoint(server, "/__health");

// GET /__health response:
// {
//   "status": "healthy",            // or "unhealthy", "degraded"
//   "uptime_seconds": 86400,
//   "version": "0.7.0",
//   "checks": {
//     "database": { "status": "healthy", "latency_ms": 3 },
//     "disk_space": { "status": "healthy", "free_gb": 42.5 }
//   }
// }
```

**Implementation Strategy**:
- Health checks run on a background timer (configurable interval, default 30s)
- Results cached to avoid running checks on every `/__health` request
- Response format is JSON (using existing serializer)
- HTTP status code: 200 = healthy, 503 = unhealthy, 200 with degraded status = some non-critical checks failing
- Startup readiness gate: server returns 503 until all critical health checks pass at least once

**Files Affected**:
- `include/weblib.h` — Health check types, registration API
- `src/health.c` (new) — Health check runner and endpoint handler
- `src/http_server.c` — Health endpoint registration
- `tests/test_weblib.c` — Health check tests

**Acceptance Criteria**:
- `GET /__health` returns valid JSON with status and check results
- Kubernetes liveness and readiness probes work out of the box
- Failing critical checks return HTTP 503
- Health checks timeout gracefully (don't block server)
- Uptime counter accurate to within 1 second

---

### 7.8 🏆 Graceful Zero-Downtime Restart (Binary Upgrade Without Dropping Connections)

**Why This Is Unique**: No C web framework supports socket inheritance for zero-downtime binary upgrades. Nginx does this at the infrastructure level, but no embeddable C library exposes this capability. This allows the server process to be replaced with a new binary while keeping all existing TCP connections alive.

**What It Does**:
- Pass listening socket file descriptors to a new server process via `exec()` or environment variables
- New process inherits listening sockets and begins accepting new connections
- Old process drains existing connections and exits
- No dropped connections, no load balancer failover required
- Works with systemd socket activation (Linux)

**API Design**:
```c
// Enable zero-downtime restart
http_server_enable_graceful_restart(server);

// On receiving SIGUSR2, the server:
// 1. Forks a new process
// 2. Passes listening socket FDs via environment (MCWL_LISTEN_FD=3)
// 3. New process calls http_server_inherit_sockets(server)
// 4. Old process stops accepting, drains active connections, exits

// In the new process (or after exec):
http_server_t *server = http_server_create();
if (http_server_inherit_sockets(server) == 0) {
    // Inherited sockets from parent — zero-downtime restart succeeded
    printf("Inherited listening sockets from previous process\n");
} else {
    // Fresh start — bind normally
    http_server_listen(server, 8080);
}

// Systemd socket activation support
if (http_server_inherit_systemd_sockets(server) == 0) {
    printf("Using systemd-provided sockets\n");
}
```

**Implementation Strategy**:
- Listening socket FDs set to `FD_CLOEXEC = false` before fork/exec
- FD numbers passed via environment variable `MCWL_LISTEN_FD` and `MCWL_LISTEN_FD_COUNT`
- Follows systemd socket activation protocol (`LISTEN_FDS`, `LISTEN_PID`) for native integration
- SIGUSR2 handler triggers the restart sequence on POSIX systems
- Old process sets a drain timeout (default 30s), then force-closes remaining connections
- Windows: not supported (no `fork()`), documented limitation

**Files Affected**:
- `include/weblib.h` — Graceful restart API
- `src/graceful_restart.c` (new) — Socket inheritance, fork, drain logic
- `src/http_server.c` — Signal handler integration, socket lifecycle
- `tests/test_weblib.c` — Restart with inherited sockets test

**Acceptance Criteria**:
- Zero dropped connections during binary upgrade (tested with concurrent load)
- Systemd socket activation works (tested with `systemd-socket-activate`)
- Old process exits within drain timeout
- Socket FD passing works across fork/exec boundary
- Clear documentation of Windows limitation

---

## Phase 7B: Cross-Ecosystem Competitive Edge — Features No Framework in Any Language Has

> **Note**: Features 7.1–7.8 above give MCWL an edge over other **C frameworks**. Features 7.9–7.15 below go further — these are capabilities that **no web framework in any language** (Node.js, Python, Go, Rust, Java) provides as built-in features. They are only available today via external tooling, service meshes, or cloud platform services. Building them directly into the framework is unprecedented.

### 7.9 🏆 Built-In Request Recording & Replay for Regression Testing

**Why This Is Unique**: No web framework — in ANY language — has built-in request recording and replay as a first-class server feature. Tools like Keploy, AREX, and Postman exist as separate external platforms. MCWL would be the **first framework where the server itself can record and replay traffic** with zero external tooling.

**What It Does**:
- Toggle recording mode: the server captures every request/response pair to a binary log file
- Replay mode: feed recorded traffic back through the server and diff responses against originals
- Automatic regression detection: flag any response that differs from the recording (status code, headers, body)
- Request anonymization: strip or mask sensitive headers (Authorization, Cookie) before saving
- Selective recording: filter by route pattern, method, or status code
- Replay speed control: 1x (real-time), 10x (stress test), or as-fast-as-possible
- Export recordings as JSON for external analysis or sharing

**API Design**:
```c
// Enable request recording
record_config_t rec_cfg = {
    .output_path = "./recordings/session_001.rec",
    .filter_paths = (const char*[]){"/api/*", NULL},  // only record API routes
    .anonymize_headers = (const char*[]){"Authorization", "Cookie", NULL},
    .max_file_size_mb = 100
};
http_server_start_recording(server, &rec_cfg);

// ... server handles traffic, all matching requests are recorded ...

http_server_stop_recording(server);

// Replay recorded traffic against current server
replay_config_t replay_cfg = {
    .input_path = "./recordings/session_001.rec",
    .speed = REPLAY_SPEED_FAST,    // as-fast-as-possible
    .diff_output = "./recordings/diff_report.json",
    .fail_on_diff = true           // return non-zero exit code if diffs found
};
int diffs = http_server_replay(server, &replay_cfg);
printf("Replay complete: %d differences found\n", diffs);
```

**Implementation Strategy**:
- Recording format: compact binary log with request method, path, headers, body, timestamp, and full response
- Replay engine: internal HTTP client sends recorded requests to `localhost` and compares responses
- Diff engine: structural comparison — ignores timestamps/request IDs but catches status code, body, and header changes
- File I/O uses standard `fopen`/`fwrite` — no external dependencies
- Anonymization is a simple header-name filter applied during write

**Files Affected**:
- `include/weblib.h` — Recording/replay config types, API
- `src/recorder.c` (new) — Request capture, binary log format, anonymization
- `src/replayer.c` (new) — Replay engine, diff comparison, report generation
- `src/http_server.c` — Recording hooks in request/response pipeline
- `tests/test_weblib.c` — Record/replay round-trip tests

**Acceptance Criteria**:
- Record 10,000 requests and replay with zero false-positive diffs
- Anonymized recordings contain no sensitive header values
- Replay at 10x speed produces identical results to 1x speed
- Diff report is valid JSON with per-request comparison details
- Recording overhead < 5% latency increase per request

---

### 7.10 🏆 Built-In Chaos / Fault Injection Middleware

**Why This Is Unique**: No web framework in any language has built-in chaos engineering as a first-class middleware. Netflix's Chaos Monkey, Gremlin, and AWS Fault Injection Simulator are all external tools. MCWL would be the **first framework where you can inject faults directly in the server configuration** — no sidecar, no proxy, no external tool.

**What It Does**:
- Configurable fault injection as middleware: latency injection, random errors, request dropping, response corruption
- Per-route fault profiles: different failure modes for different endpoints
- Header-triggered faults: clients can request specific failure modes via `X-Chaos-*` headers (for testing)
- Probability-based injection: "fail 5% of requests to `/api/payments`"
- Latency injection: add N milliseconds of artificial delay
- Circuit breaker simulation: after N failures, automatically start failing all requests to simulate cascade
- Kill switch: `GET /__chaos/disable` to instantly disable all fault injection

**API Design**:
```c
// Configure chaos middleware
chaos_config_t chaos_cfg = {
    .enabled = true,
    .seed = 42,  // deterministic randomness for reproducible tests
    .rules = (chaos_rule_t[]){
        {
            .path_pattern = "/api/payments",
            .fault = CHAOS_LATENCY,
            .latency_ms = 2000,
            .probability = 0.10  // 10% of requests get 2s delay
        },
        {
            .path_pattern = "/api/users",
            .fault = CHAOS_ERROR,
            .error_status = 503,
            .probability = 0.05  // 5% of requests get 503
        },
        {
            .path_pattern = "/api/orders",
            .fault = CHAOS_DROP,
            .probability = 0.02  // 2% of requests silently dropped
        },
        { 0 }  // sentinel
    },
    .allow_header_override = true,  // allow X-Chaos-Latency: 5000 from clients
    .admin_endpoint = "/__chaos"    // GET /__chaos/status, POST /__chaos/disable
};
http_server_enable_chaos(server, &chaos_cfg);

// Client-side testing (via headers):
// curl -H "X-Chaos-Latency: 3000" http://localhost:8080/api/users
// curl -H "X-Chaos-Error: 500" http://localhost:8080/api/users
// curl -H "X-Chaos-Drop: true" http://localhost:8080/api/users
```

**Implementation Strategy**:
- Chaos middleware runs early in the middleware chain (before routing)
- Probability check uses a fast PRNG seeded for reproducibility
- Latency injection: `usleep()` / `Sleep()` or event loop timer for async mode
- Error injection: short-circuit response with configured status code
- Drop injection: close socket immediately without response
- Admin endpoint allows runtime enable/disable without restart
- Header override only active when `allow_header_override = true` (disabled in production)

**Files Affected**:
- `include/weblib.h` — Chaos config types, rule structs, API
- `src/middleware_chaos.c` (new) — Fault injection engine
- `src/http_server.c` — Chaos middleware registration
- `tests/test_weblib.c` — Chaos injection tests (deterministic with seed)

**Acceptance Criteria**:
- Latency injection adds exactly ±10% of configured delay
- Error injection returns correct status code and standard error body
- Drop injection closes connection without sending any data
- Probability distribution matches configured rate within 1% over 10,000 requests
- Seed-based PRNG produces identical fault sequences across runs
- Admin disable endpoint stops all faults within 1 request cycle

---

### 7.11 🏆 Automatic API Versioning with Contract Drift Detection

**Why This Is Unique**: No web framework automatically manages API versions or detects breaking changes. Every framework (Express, Django, FastAPI, Gin, Spring) requires manual versioning via URL prefixes (`/v1/`, `/v2/`), headers, or query parameters. MCWL would be the **first framework that detects API contract changes and auto-versions** — the server knows when its API has changed.

**What It Does**:
- Automatic version tagging: server computes a version fingerprint from registered routes, methods, parameter schemas, and response types
- Contract snapshot: save current API contract to a file (`api_contract_v1.json`)
- Drift detection: on startup, compare current routes against saved contract and report additions, removals, and breaking changes
- Auto-versioned routing: serve old contract at `/v1/` and new contract at `/v2/` simultaneously
- Deprecation notices: mark old routes with `Deprecation` and `Sunset` headers (IETF RFC 8594)
- Breaking change classification: distinguish additions (safe) from removals/type changes (breaking)

**API Design**:
```c
// Save current API contract as baseline
api_contract_save(router, "./contracts/v1.json");

// On startup, detect drift against saved contract
api_drift_report_t *report = api_contract_check(router, "./contracts/v1.json");
if (report->has_breaking_changes) {
    printf("⚠ BREAKING CHANGES DETECTED:\n");
    for (int i = 0; i < report->change_count; i++) {
        printf("  - %s: %s %s\n",
            report->changes[i].type,    // "REMOVED", "CHANGED", "ADDED"
            report->changes[i].method,  // "GET"
            report->changes[i].path);   // "/users/:id"
    }
}
api_drift_report_free(report);

// Enable auto-versioned routing
api_versioning_config_t ver_cfg = {
    .strategy = API_VERSION_URL_PREFIX,   // /v1/, /v2/
    .contracts_dir = "./contracts/",
    .deprecation_header = true,           // add Deprecation header to old routes
    .sunset_after_days = 90               // old version sunset after 90 days
};
http_server_enable_api_versioning(server, &ver_cfg);
```

**Implementation Strategy**:
- Contract fingerprint: hash of all route definitions (method + path + param names + types)
- Contract file format: JSON (using existing serializer) — human-readable and diffable
- Drift detection: load saved contract JSON, compare against current router state
- Breaking change rules: removed route = breaking, removed parameter = breaking, added route = safe, added optional param = safe
- Auto-versioning: prefix-based routing that maps `/v1/users` → handler_v1, `/v2/users` → handler_v2
- Deprecation headers: `Deprecation: true`, `Sunset: Sat, 01 Jan 2027 00:00:00 GMT`

**Files Affected**:
- `include/weblib.h` — Contract types, versioning config, drift report
- `src/api_contract.c` (new) — Contract save/load, drift detection, fingerprinting
- `src/api_versioning.c` (new) — URL prefix routing, deprecation headers
- `src/router.c` — Contract extraction from route table
- `tests/test_weblib.c` — Contract drift detection tests

**Acceptance Criteria**:
- Contract save/load round-trips without data loss
- Breaking changes correctly classified (removal vs. addition)
- Auto-versioned routes serve correct handlers for each version prefix
- Deprecation and Sunset headers present on old-version responses
- Drift report is valid JSON with per-route change details

---

### 7.12 🏆 Per-Route Resource Budgets & Backpressure

**Why This Is Unique**: No web framework in any language provides per-route resource budgets. API gateways (AWS, Azure, Kong) offer rate limiting, but no framework lets you say "this route may use at most 10MB of memory and 500ms of CPU time per request." This enables fine-grained resource isolation without containers or process-level separation.

**What It Does**:
- Per-route memory budget: limit heap allocations within a handler to N bytes
- Per-route time budget: kill or warn if a handler exceeds N milliseconds
- Per-route concurrency limit: maximum simultaneous in-flight requests per route
- Backpressure: when a route hits its concurrency limit, new requests receive 503 with `Retry-After` header
- Resource usage reporting: `GET /__resources` shows per-route resource consumption
- Budget violation logging: structured log entries when budgets are exceeded

**API Design**:
```c
// Define resource budgets per route
route_budget_t user_budget = {
    .max_memory_bytes = 10 * 1024 * 1024,  // 10MB per request
    .max_duration_ms = 500,                 // 500ms timeout
    .max_concurrent = 50,                   // max 50 in-flight requests
    .on_exceed = BUDGET_RESPOND_503         // or BUDGET_LOG_WARNING
};
router_add_route_budgeted(router, HTTP_GET, "/users/:id", handle_user, &user_budget);

// Enable resource reporting endpoint
http_server_enable_resource_reporting(server, "/__resources");

// GET /__resources returns:
// {
//   "routes": {
//     "GET /users/:id": {
//       "budget": {"max_memory_bytes":10485760, "max_duration_ms":500, "max_concurrent":50},
//       "current": {"active_requests":12, "avg_duration_ms":45, "peak_memory_bytes":2340000},
//       "violations": {"timeout":3, "memory":0, "concurrency":15}
//     }
//   }
// }
```

**Implementation Strategy**:
- Concurrency limit: atomic counter per route, increment on entry, decrement on exit
- Time budget: record handler start time, check elapsed on response send; in async mode, use event loop timer for hard kill
- Memory budget: custom allocator wrapper that tracks per-request allocation totals (opt-in via `MCWL_TRACK_MEMORY` compile flag)
- Backpressure: when concurrency limit reached, respond 503 immediately without invoking handler
- Resource reporting: atomic counters aggregated into JSON on `/__resources` request
- Budget violations logged via structured logging (7.2) with route, budget, and actual values

**Files Affected**:
- `include/weblib.h` — Budget config types, API
- `src/resource_budget.c` (new) — Budget tracking, enforcement, reporting
- `src/router.c` — Budget-aware route dispatch
- `src/http_server.c` — Resource endpoint registration, request lifecycle hooks
- `tests/test_weblib.c` — Concurrency limit and timeout tests

**Acceptance Criteria**:
- Concurrency limit enforced with zero race conditions (atomic operations)
- Time budget kills handler within 10% of configured timeout
- 503 responses include `Retry-After` header with estimated wait time
- Resource report JSON accurately reflects current and historical usage
- Budget enforcement overhead < 1μs per request when no violation occurs

---

### 7.13 🏆 Built-In Request Idempotency Layer

**Why This Is Unique**: No web framework has built-in idempotency. Stripe famously implements idempotency keys in their API, but every developer must build this from scratch. AWS API Gateway offers it only for Lambda integrations. MCWL would be the **first framework where idempotency is a built-in middleware** — just enable it and non-idempotent routes automatically handle duplicate requests safely.

**What It Does**:
- Clients send `Idempotency-Key: <uuid>` header with POST/PUT/PATCH requests
- Server caches the response for a given key and returns the cached response on duplicate requests
- Prevents double-processing of payments, order creation, or any state-changing operation
- Configurable TTL: how long to remember idempotency keys (default 24 hours)
- Concurrent request deduplication: if two identical requests arrive simultaneously, only one handler executes; the second waits and receives the same response
- Storage: in-memory hash table (default), or pluggable backend interface for external stores

**API Design**:
```c
// Enable idempotency middleware
idempotency_config_t idemp_cfg = {
    .enabled = true,
    .header_name = "Idempotency-Key",        // standard header name
    .ttl_seconds = 86400,                     // remember keys for 24 hours
    .max_cached_responses = 10000,            // max keys in memory
    .applicable_methods = HTTP_POST | HTTP_PUT | HTTP_PATCH,
    .enforce_on_routes = (const char*[]){"/api/payments", "/api/orders", NULL}
    // NULL = apply to all routes matching applicable_methods
};
http_server_enable_idempotency(server, &idemp_cfg);

// Client usage:
// First request:
//   POST /api/payments
//   Idempotency-Key: 550e8400-e29b-41d4-a716-446655440000
//   → 201 Created {"id": "pay_123", "amount": 99.99}
//
// Duplicate request (same key):
//   POST /api/payments
//   Idempotency-Key: 550e8400-e29b-41d4-a716-446655440000
//   → 201 Created {"id": "pay_123", "amount": 99.99}  (cached, handler NOT re-executed)
//
// Response includes:
//   X-Idempotent-Replayed: true
```

**Implementation Strategy**:
- Hash table keyed by `(route + idempotency_key)` storing full serialized response (status, headers, body)
- Mutex-protected entry states: `PENDING` (handler executing), `COMPLETE` (response cached)
- Concurrent dedup: second request for same key finds `PENDING` entry, waits on condition variable, receives same response
- LRU eviction when `max_cached_responses` exceeded
- TTL enforcement: lazy eviction on access + periodic sweep on event loop timer
- `X-Idempotent-Replayed: true` header added to cached responses for client awareness

**Files Affected**:
- `include/weblib.h` — Idempotency config, API
- `src/middleware_idempotency.c` (new) — Key tracking, response caching, concurrent dedup
- `src/http_server.c` — Idempotency middleware registration
- `tests/test_weblib.c` — Idempotency tests (cache hit, expiry, concurrent requests)

**Acceptance Criteria**:
- Duplicate requests return identical cached response without re-executing handler
- Concurrent duplicate requests: only one handler invocation, both clients get same response
- TTL expiry causes key to be forgotten (subsequent request re-executes handler)
- LRU eviction keeps memory bounded
- `X-Idempotent-Replayed` header present on replayed responses
- No deadlocks under concurrent load with identical keys

---

### 7.14 🏆 Built-In Shadow Traffic / Canary Route Testing

**Why This Is Unique**: No web framework has built-in shadow traffic testing (also called traffic mirroring or dark launching). Istio and Envoy provide this at the service mesh level, but no application-level framework lets you mirror requests to an alternative handler and compare results — all within a single process, with zero infrastructure.

**What It Does**:
- Mirror a percentage of live traffic to a "shadow" handler without affecting the client response
- Compare shadow handler responses to primary handler responses and log differences
- Canary routing: gradually shift traffic from old handler to new handler (1% → 5% → 25% → 100%)
- Automatic rollback: if shadow/canary handler error rate exceeds threshold, revert to primary
- Zero impact on clients: shadow requests run asynchronously, clients always receive primary response

**API Design**:
```c
// Shadow testing: mirror 100% of /api/users traffic to new handler
shadow_config_t shadow_cfg = {
    .path = "/api/users",
    .shadow_handler = handle_users_v2,     // new handler to test
    .mirror_percentage = 100,               // mirror all traffic
    .compare_responses = true,              // log diffs between primary and shadow
    .log_diffs_only = true                  // only log when responses differ
};
router_add_shadow(router, &shadow_cfg);

// Canary routing: gradually shift traffic to new handler
canary_config_t canary_cfg = {
    .path = "/api/orders",
    .canary_handler = handle_orders_v2,
    .initial_percentage = 5,                // start with 5% of traffic
    .increment_percentage = 5,              // increase by 5% each interval
    .increment_interval_seconds = 300,      // every 5 minutes
    .max_error_rate = 0.01,                 // rollback if >1% errors
    .auto_promote = true                    // fully switch when 100% reached
};
router_add_canary(router, &canary_cfg);

// Monitor via admin endpoint
// GET /__canary returns:
// {
//   "canaries": [{
//     "path": "/api/orders",
//     "current_percentage": 25,
//     "primary_error_rate": 0.002,
//     "canary_error_rate": 0.001,
//     "status": "promoting"
//   }]
// }
```

**Implementation Strategy**:
- Shadow: after primary handler completes, dispatch shadow handler on a background thread (threaded mode) or as a deferred event loop task (async mode)
- Shadow response is captured but never sent to client
- Diff engine: compare status codes and body content (reuse replay diff from 7.9)
- Canary: route dispatch uses PRNG to select primary vs. canary handler based on percentage
- Error rate tracking: sliding window counter per handler (atomic operations)
- Auto-rollback: if canary error rate exceeds threshold, reset percentage to 0
- Admin endpoint: JSON status of all active canaries and shadows

**Files Affected**:
- `include/weblib.h` — Shadow/canary config types, API
- `src/shadow_canary.c` (new) — Shadow dispatch, canary routing, error tracking, auto-rollback
- `src/router.c` — Shadow/canary-aware route dispatch
- `tests/test_weblib.c` — Shadow mirroring and canary promotion tests

**Acceptance Criteria**:
- Shadow handler execution never delays client response
- Canary percentage increases on schedule when error rate is below threshold
- Auto-rollback triggers within one interval when error rate exceeds threshold
- Diff logging correctly identifies response differences between primary and shadow
- Admin endpoint accurately reflects current canary state

---

### 7.15 🏆 Embedded Diagnostic REPL & Runtime Introspection

**Why This Is Unique**: No web framework provides a built-in diagnostic console. Erlang/OTP has the remote shell, Ruby has `rails console`, but no C web server lets you connect to a running server and inspect its state — routes, active connections, memory usage, configuration, and metrics — in real-time via a simple TCP console.

**What It Does**:
- Optional TCP diagnostic port (e.g., `localhost:9090`) for connecting with `telnet` or `nc`
- Interactive command-line interface for inspecting running server state
- Commands: `routes`, `connections`, `config`, `metrics`, `memory`, `logs tail`, `help`
- Read-only by default — no mutation commands unless explicitly enabled
- Authentication: optional shared secret to prevent unauthorized access
- Minimal footprint: console handler runs on the same event loop, no extra threads

**API Design**:
```c
// Enable diagnostic REPL
diag_config_t diag_cfg = {
    .enabled = true,
    .port = 9090,
    .bind_address = "127.0.0.1",   // localhost only for security
    .secret = "my-debug-secret",   // optional auth token
    .allow_mutation = false         // read-only mode
};
http_server_enable_diagnostics(server, &diag_cfg);

// Connect: telnet localhost 9090
// > auth my-debug-secret
// Authenticated.
// > routes
// GET  /              → handle_root
// GET  /users/:id     → handle_user      [budget: 500ms, 50 concurrent]
// POST /api/payments  → handle_payment   [idempotent, validated]
// > connections
// Active: 47 | Peak: 231 | Total: 15,234
// WebSocket: 12 | Keep-Alive: 35
// > metrics
// Requests: 15,234 | Errors: 23 (0.15%)
// Avg Latency: 4.2ms | P99: 42ms
// > memory
// Heap: 12.4 MB | RSS: 28.7 MB | Route Budgets: 3/50 at limit
// > config get server.port
// 8080
// > logs tail 5
// [2026-02-11 19:00:01] INFO  GET /users/42 → 200 (3ms)
// [2026-02-11 19:00:01] WARN  GET /api/slow → 200 (487ms) [near budget]
// ...
```

**Implementation Strategy**:
- Diagnostic port listens on a separate TCP socket, registered with the event loop
- Line-based protocol: read command, execute, write output, prompt
- Each command is a function pointer in a command table (extensible)
- Auth: first command must be `auth <secret>`, otherwise connection closed after 3 failed attempts
- Read-only mode: mutation commands (`config set`, `chaos enable`) rejected unless `allow_mutation = true`
- Output formatting: plain text for human readability (not JSON — this is a human console)

**Files Affected**:
- `include/weblib.h` — Diagnostic config, API
- `src/diagnostics.c` (new) — REPL engine, command parser, TCP listener
- `src/http_server.c` — Diagnostic port lifecycle
- `src/event_loop.c` — Diagnostic socket event registration
- `tests/test_weblib.c` — Diagnostic command parsing tests

**Acceptance Criteria**:
- Connect via `telnet localhost 9090` and receive prompt
- All read-only commands return accurate real-time data
- Auth required when secret is configured (reject unauthorized connections)
- Connection closed after 3 failed auth attempts
- Diagnostic port does not interfere with HTTP performance
- No information leakage when `allow_mutation = false`

---

## Priority Matrix

| Feature | ID | Competitive Impact | Complexity | Est. Time | Dependencies |
|---------|----|--------------------|------------|-----------|--------------|
| Self-Documenting API | 7.1 | 🏆 First in any C framework | Medium | 5-7 days | JSON serializer |
| Structured Logging & Observability | 7.2 | 🏆 First in any C framework | Medium | 5-7 days | None |
| Coroutine-Style Async | 7.3 | 🏆 First in pure C | High | 7-10 days | Event loop |
| Runtime Plugin System | 7.4 | 🏆 First with hot reload | High | 7-10 days | Router, dlopen |
| Configuration System | 7.5 | 🏆 First in any C framework | Medium | 4-6 days | JSON parser |
| Request Validation | 7.6 | 🏆 First in any C framework | Medium | 5-7 days | Router, JSON |
| Health Check Protocol | 7.7 | 🏆 First with K8s compat | Low | 3-4 days | JSON serializer |
| Zero-Downtime Restart | 7.8 | 🏆 First embeddable C lib | Medium | 4-6 days | Server lifecycle |
| Request Recording & Replay | 7.9 | 🏆 First in ANY framework | High | 6-8 days | HTTP client, I/O |
| Chaos / Fault Injection | 7.10 | 🏆 First in ANY framework | Medium | 4-6 days | Middleware, PRNG |
| API Versioning & Drift Detection | 7.11 | 🏆 First in ANY framework | Medium | 5-7 days | Router, JSON |
| Per-Route Resource Budgets | 7.12 | 🏆 First in ANY framework | High | 5-7 days | Router, atomics |
| Request Idempotency Layer | 7.13 | 🏆 First in ANY framework | Medium | 4-6 days | Hash table, mutex |
| Shadow Traffic & Canary Testing | 7.14 | 🏆 First in ANY framework | High | 6-8 days | Router, threading |
| Diagnostic REPL & Introspection | 7.15 | 🏆 First in ANY framework | Medium | 5-7 days | Event loop, TCP |
| **Phase 7 Total** | | | | **~12-16 weeks** | |

---

## Implementation Order

Recommended sequence based on dependencies and impact:

```
Week 1-2:   7.2  Structured Logging (foundation for all other features)
Week 2-3:   7.5  Configuration System (enables configurable behavior everywhere)
Week 3-4:   7.1  Self-Documenting API (high visibility, moderate complexity)
Week 4-5:   7.6  Request Validation (builds on 7.1 for schema-driven docs)
Week 5-6:   7.7  Health Check Protocol (quick win, cloud-native readiness)
Week 6-7:   7.3  Coroutine-Style Async (deepest technical challenge)
Week 7-8:   7.4  Runtime Plugin System (requires stable APIs from above)
Week 8:     7.8  Zero-Downtime Restart (polish, requires stable server lifecycle)
Week 9:     7.13 Request Idempotency Layer (foundational for safe APIs)
Week 9-10:  7.12 Per-Route Resource Budgets (backpressure and isolation)
Week 10-11: 7.10 Chaos / Fault Injection (builds on middleware chain)
Week 11-12: 7.11 API Versioning & Drift Detection (builds on 7.1 contract model)
Week 12-13: 7.9  Request Recording & Replay (builds on full request pipeline)
Week 13-14: 7.14 Shadow Traffic & Canary Testing (builds on router + 7.9 diff engine)
Week 15-16: 7.15 Diagnostic REPL & Introspection (integrates all subsystems)
```

---

## Feature Synergies

These features compound each other — the combination creates more value than the sum:

| Feature A | + Feature B | = Synergy |
|-----------|-------------|-----------|
| 7.1 API Docs | + 7.6 Validation | Auto-generated docs include request schemas, parameter constraints, and example values |
| 7.2 Logging | + 7.7 Health Check | Health check failures auto-logged with structured context |
| 7.3 Coroutines | + 7.4 Plugins | Plugin handlers can be async coroutines — first C framework with async plugin handlers |
| 7.5 Config | + 7.2 Logging | Log level changeable at runtime via config live reload — no restart needed |
| 7.5 Config | + 7.4 Plugins | Plugin directory configurable, hot reload interval tunable |
| 7.7 Health | + 7.8 Restart | Health check can trigger automatic zero-downtime restart on degraded state |
| 7.9 Recording | + 7.14 Shadow | Recorded traffic can be replayed through shadow handlers for offline canary testing |
| 7.10 Chaos | + 7.9 Recording | Record traffic under chaos conditions to build resilience regression suites |
| 7.10 Chaos | + 7.7 Health | Health checks detect chaos-induced failures, triggering alerts and auto-recovery |
| 7.11 Versioning | + 7.1 API Docs | Each API version gets its own OpenAPI doc; drift report links to affected endpoints |
| 7.12 Budgets | + 7.2 Logging | Budget violations logged with structured context (route, limit, actual, request ID) |
| 7.12 Budgets | + 7.15 REPL | Diagnostic console shows real-time budget usage per route (`resources` command) |
| 7.13 Idempotency | + 7.9 Recording | Recorded replays with idempotency keys verify dedup correctness across deployments |
| 7.14 Canary | + 7.11 Versioning | Canary testing gates API version promotion — only promote when canary passes |
| 7.15 REPL | + 7.10 Chaos | Enable/disable chaos rules from diagnostic console without restart |
| 7.15 REPL | + 7.5 Config | Change config values live from the diagnostic console (`config set log.level debug`) |

---

## Competitive Edge Summary

After Phase 7, MCWL will be the **only** web framework in any language that combines:

### First Among C Frameworks (No C Framework Has These)

1. ✅ **Zero external dependencies** (unique among C frameworks)
2. ✅ **Self-documenting REST APIs** (no C framework has this)
3. ✅ **Structured JSON logging** (no C framework has this)
4. ✅ **Coroutine-style async in pure C** (no C framework has this)
5. ✅ **Hot-reloadable runtime plugins** (no C framework has this)
6. ✅ **Built-in configuration with live reload** (no C framework has this)
7. ✅ **Declarative request validation** (no C framework has this)
8. ✅ **Kubernetes-compatible health checks** (no C framework has this built-in)
9. ✅ **Zero-downtime binary upgrades** (no embeddable C library has this)

### First Among ALL Frameworks, Any Language (No Framework Has These Built-In)

10. ✅ **Built-in request recording & replay** (no framework has this — always external tooling)
11. ✅ **Built-in chaos / fault injection middleware** (no framework has this — always separate tools like Chaos Monkey)
12. ✅ **Automatic API versioning with contract drift detection** (no framework auto-detects breaking changes)
13. ✅ **Per-route resource budgets with backpressure** (no framework limits memory/time/concurrency per-route)
14. ✅ **Built-in request idempotency layer** (no framework has this — Stripe built it custom, everyone else DIYs it)
15. ✅ **Built-in shadow traffic & canary route testing** (no framework has this — requires service mesh like Istio)
16. ✅ **Embedded diagnostic REPL** (no web framework has a telnet-accessible runtime console)

**The message**: MCWL isn't just another C web server — it's the first web framework in **any language** that provides production resilience primitives (chaos testing, idempotency, canary routing, request replay, resource budgets) as built-in features rather than external infrastructure. It brings modern DX to systems programming while maintaining the purity and performance of native C.

---

## Success Metrics

### Phase 7 (v0.7.0)

#### Core DX Features (7.1–7.8)
- ✅ `GET /__openapi.json` returns valid OpenAPI 3.0 for all documented routes
- ✅ Structured logging output parseable by ELK/Loki/Datadog
- ✅ Prometheus metrics endpoint passes `promtool check metrics`
- ✅ 1000+ concurrent coroutine handlers on a single event loop thread
- ✅ Plugin hot-reload with zero dropped requests during swap
- ✅ Config live-reload applies changes within 2 seconds
- ✅ Request validation returns all errors (not just first) in structured JSON
- ✅ Health check endpoint compatible with Kubernetes liveness/readiness probes
- ✅ Zero-downtime restart passes 10,000 req/s load test with zero errors

#### Production Resilience Features (7.9–7.15)
- ✅ Record 10,000 requests and replay with zero false-positive diffs
- ✅ Chaos fault injection produces deterministic, reproducible failure sequences
- ✅ API contract drift detection catches all breaking changes (removal, type change)
- ✅ Per-route concurrency budgets enforce limits with zero race conditions
- ✅ Idempotency layer handles concurrent duplicate requests with only one handler execution
- ✅ Shadow handler execution adds zero latency to primary client response
- ✅ Canary auto-rollback triggers within one interval when error threshold exceeded
- ✅ Diagnostic REPL connects via telnet and responds to all read-only commands

---

## Notes for Contributors

1. **Phase 6 is complete** ✅ — Sessions, template engine, auth middleware (Basic/API Key/JWT), DB pooling, and API docs are all implemented. Phase 7 builds on this stable foundation.
2. **Branch naming**: Use `feature/phase7-api-docs`, `feature/phase7-logging`, etc.
3. **Commit messages**: Follow [Conventional Commits](https://www.conventionalcommits.org/) (e.g., `feat(docs): add self-documenting API endpoint`)
4. **Testing**: Every feature must have unit tests. Integration tests for cross-feature synergies.
5. **Pure C only**: All implementations must use standard C library + platform APIs. No external libraries.
6. **Platform coverage**: Features must work on Linux and macOS. Windows support where feasible (except 7.8).
7. **Remaining from earlier phases** (can be tackled alongside Phase 7):
   - SSL/TLS support (custom pure C implementation)
   - HTTP/2 and HTTP/3/QUIC protocol support
   - Async WebSocket mode (event loop integration)
   - Complete HTTP parser hardening
   - Response compression (gzip, deflate, brotli)
   - Graceful shutdown & thread management

---

## Revision History

| Version | Date | Changes |
|---------|------|---------|
| 1.0 | 2026-02-11 | Initial Phase 7 competitive edge roadmap (7.1–7.8) |
| 1.1 | 2026-02-11 | Added cross-ecosystem competitive gap features (7.9–7.15): request recording & replay, chaos testing, API versioning & drift detection, per-route resource budgets, idempotency, shadow/canary testing, diagnostic REPL |
| 1.2 | 2026-02-18 | Updated for Phase 6 completion; resolved merge conflicts with main; added remaining items documentation |

---

**Maintained by**: MCWL Core Team  
**Last Updated**: 2026-02-18  
**Status**: Planning — Ready for Implementation  
**License**: MIT (see LICENSE file)

For questions or discussions about this roadmap, please open an issue on GitHub or contact the maintainers.
