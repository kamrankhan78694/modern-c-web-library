# Modern C Web Library - API Reference

**Version 1.0.0** — Production Ready

This document provides a comprehensive API reference for the Modern C Web Library.

---

## Table of Contents

1. [HTTP Server](#http-server)
2. [Router](#router)
3. [Request/Response Helpers](#requestresponse-helpers)
4. [JSON API](#json-api)
5. [Event Loop](#event-loop)
6. [WebSocket](#websocket)
7. [Body Parser](#body-parser)
8. [Cookie Handling](#cookie-handling)
9. [CORS Middleware](#cors-middleware)
10. [Rate Limiting Middleware](#rate-limiting-middleware)
11. [Static File Middleware](#static-file-middleware)
12. [Session Management](#session-management)
13. [Template Engine](#template-engine)
14. [Authentication Middleware](#authentication-middleware)
15. [Database Connection Pool](#database-connection-pool)
16. [Thread Pool & Server Hardening](#thread-pool--server-hardening)
17. [CSRF Middleware](#csrf-middleware)
18. [Logging Middleware](#logging-middleware)
19. [Error Handler Middleware](#error-handler-middleware)
20. [Input Validation](#input-validation)
21. [Health Check](#health-check)
22. [In-Memory Cache](#in-memory-cache)
23. [Metrics Middleware](#metrics-middleware)
24. [Response Compression](#response-compression)
25. [Benchmarking](#benchmarking)

---

## HTTP Server

### `http_server_create()`
Create a new HTTP server instance.
```c
http_server_t *http_server_create(void);
// Returns: Server instance or NULL on failure
```

### `http_server_listen()`
Start the server listening on a port.
```c
int http_server_listen(http_server_t *server, uint16_t port);
// Returns: 0 on success, -1 on failure
```

### `http_server_stop()`
Stop the running server.
```c
void http_server_stop(http_server_t *server);
```

### `http_server_destroy()`
Free all server resources.
```c
void http_server_destroy(http_server_t *server);
```

### `http_server_set_router()`
Attach a router to the server.
```c
void http_server_set_router(http_server_t *server, router_t *router);
```

### `http_server_set_async()`
Enable/disable async I/O mode.
```c
int http_server_set_async(http_server_t *server, bool enable);
```

### `http_server_get_event_loop()`
Get the event loop (async mode only).
```c
event_loop_t *http_server_get_event_loop(http_server_t *server);
```

---

## Router

### `router_create()` / `router_destroy()`
```c
router_t *router_create(void);
void router_destroy(router_t *router);
```

### `router_add_route()`
```c
int router_add_route(router_t *router, http_method_t method,
                     const char *path, route_handler_t handler);
// Supports path parameters: "/users/:id"
```

### `router_use_middleware()`
```c
int router_use_middleware(router_t *router, middleware_fn_t middleware);
// Middleware executes in order added, before route handler
```

---

## Request/Response Helpers

### Request
```c
const char *http_request_get_header(http_request_t *req, const char *key);
const char *http_request_get_param(http_request_t *req, const char *key);
```

### Response
```c
void http_response_set_header(http_response_t *res, const char *key, const char *value);
void http_response_send_text(http_response_t *res, http_status_t status, const char *text);
void http_response_send_json(http_response_t *res, http_status_t status, json_value_t *json);
void http_response_send_template(http_response_t *res, http_status_t status,
                                  const char *template_str, template_context_t *ctx);
```

---

## JSON API

### Creation
```c
json_value_t *json_object_create(void);
json_value_t *json_array_create(void);
json_value_t *json_string_create(const char *str);
json_value_t *json_number_create(double num);
json_value_t *json_bool_create(bool val);
```

### Object Operations
```c
void json_object_set(json_value_t *obj, const char *key, json_value_t *value);
json_value_t *json_object_get(json_value_t *obj, const char *key);
```

### Array Operations
```c
int json_array_append(json_value_t *arr, json_value_t *value);
json_value_t *json_array_get(json_value_t *arr, size_t index);
size_t json_array_length(json_value_t *arr);
```

### Parse / Stringify / Free
```c
json_value_t *json_parse(const char *json_str);
char *json_stringify(json_value_t *value);  // Caller must free
void json_value_free(json_value_t *value);
```

---

## Session Management

### Session Store
```c
session_store_t *session_store_create(void);
void session_store_destroy(session_store_t *store);
```

### Session Lifecycle
```c
char *session_create(session_store_t *store, int max_age);  // Returns ID, caller must free
session_t *session_get(session_store_t *store, const char *session_id);
void session_destroy(session_store_t *store, const char *session_id);
```

### Session Data
```c
void session_set_data(session_t *session, const char *key, const char *value);
const char *session_get_data(session_t *session, const char *key);
void session_remove_data(session_t *session, const char *key);
```

### Session Utilities
```c
const char *session_get_id(session_t *session);
bool session_is_expired(session_t *session);
int session_cleanup_expired(session_store_t *store);
session_t *session_from_request(session_store_t *store, http_request_t *req);
void session_set_cookie(http_response_t *res, const char *session_id,
                        int max_age, const char *path);
```

### Example
```c
session_store_t *store = session_store_create();

// Login handler
void handle_login(http_request_t *req, http_response_t *res) {
    char *sid = session_create(store, 3600);
    session_t *sess = session_get(store, sid);
    session_set_data(sess, "user_id", "42");
    session_set_cookie(res, sid, 3600, "/");
    free(sid);
    http_response_send_text(res, HTTP_OK, "Logged in");
}

// Protected route
void handle_profile(http_request_t *req, http_response_t *res) {
    session_t *sess = session_from_request(store, req);
    if (!sess) {
        http_response_send_text(res, HTTP_UNAUTHORIZED, "Login required");
        return;
    }
    const char *user_id = session_get_data(sess, "user_id");
    json_value_t *json = json_object_create();
    json_object_set(json, "user_id", json_string_create(user_id ? user_id : "unknown"));
    http_response_send_json(res, HTTP_OK, json);
    json_value_free(json);
}
```

---

## Template Engine

### Context
```c
template_context_t *template_context_create(void);
void template_context_set(template_context_t *ctx, const char *key, const char *value);
const char *template_context_get(template_context_t *ctx, const char *key);
void template_context_destroy(template_context_t *ctx);
```

### Rendering
```c
char *template_render(const char *template_str, template_context_t *ctx);  // Caller must free
char *template_load_file(const char *filename);  // Caller must free
void http_response_send_template(http_response_t *res, http_status_t status,
                                  const char *template_str, template_context_t *ctx);
```

### Example
```c
template_context_t *ctx = template_context_create();
template_context_set(ctx, "name", "Alice");
template_context_set(ctx, "role", "Engineer");

const char *tmpl = "<h1>{{ name }}</h1><p>Role: {{ role }}</p>";
http_response_send_template(res, HTTP_OK, tmpl, ctx);

template_context_destroy(ctx);
```

---

## Authentication Middleware

### Basic Auth
```c
typedef bool (*auth_verify_cb_t)(const char *username, const char *password, void *user_data);

basic_auth_config_t config = {
    .realm = "Admin",
    .verify = my_verify_fn,
    .user_data = NULL
};

middleware_fn_t mw = basic_auth_middleware_create(&config);
if (mw != NULL) {
    router_use_middleware(router, mw);
}
basic_auth_middleware_destroy();
```

### API Key Auth
```c
typedef bool (*apikey_verify_cb_t)(const char *api_key, void *user_data);

apikey_auth_config_t config = {
    .header_name = "X-API-Key",
    .verify = my_key_verify_fn,
    .user_data = NULL
};

middleware_fn_t mw = apikey_auth_middleware_create(&config);
if (mw != NULL) {
    router_use_middleware(router, mw);
}
apikey_auth_middleware_destroy();
```

### JWT Auth (HMAC-SHA256)
```c
jwt_auth_config_t config = {
    .secret = "my-secret-key",
    .secret_len = 13,
    .header_name = NULL  // Defaults to "Authorization"
};

middleware_fn_t mw = jwt_auth_middleware_create(&config);
if (mw != NULL) {
    router_use_middleware(router, mw);
}
jwt_auth_middleware_destroy();
```

---

## Database Connection Pool

See `include/db_pool.h` for the full API.

```c
#include "db_pool.h"

db_pool_config_t config = db_pool_config_default(DB_TYPE_GENERIC, "db://localhost/app");
config.min_connections = 2;
config.max_connections = 10;

db_pool_t *pool = db_pool_create(&config);

db_connection_t *conn = db_pool_acquire(pool);
if (conn != NULL) {
    void *handle = db_connection_get_handle(conn);
    (void)handle;
    db_pool_release(pool, conn);
}

db_pool_destroy(pool);
```

---

## Status Codes

```c
HTTP_OK             = 200
HTTP_CREATED        = 201
HTTP_ACCEPTED       = 202
HTTP_NO_CONTENT     = 204
HTTP_NOT_MODIFIED   = 304
HTTP_BAD_REQUEST    = 400
HTTP_UNAUTHORIZED   = 401
HTTP_FORBIDDEN      = 403
HTTP_NOT_FOUND      = 404
HTTP_TOO_MANY_REQUESTS = 429
HTTP_INTERNAL_ERROR = 500
```

---

---

## Thread Pool & Server Hardening

### Server Lifecycle
```c
void http_server_set_timeout(http_server_t *server, int read_sec, int write_sec);
void http_server_set_thread_count(http_server_t *server, int count);  // Clamped to [1, 256]
int http_server_shutdown(http_server_t *server, int timeout_sec);     // Graceful shutdown with drain
int http_server_get_state(http_server_t *server);  // STOPPED=0, RUNNING=1, DRAINING=2
```

Server state transitions: `STOPPED → RUNNING → DRAINING → STOPPED`

---

## CSRF Middleware

```c
typedef struct {
    const char *cookie_name;   // Default: "_csrf"
    const char *header_name;   // Default: "X-CSRF-Token"
    int token_length;          // Default: 32
} csrf_config_t;

middleware_fn_t csrf_middleware_create(const csrf_config_t *config);
void csrf_middleware_destroy(void);
```

Safe methods (GET, HEAD, OPTIONS) pass through. Unsafe methods require a valid token in the header that matches the cookie value.

---

## Logging Middleware

```c
typedef struct {
    int level;       // LOG_LEVEL_DEBUG=0, LOG_LEVEL_INFO=1, LOG_LEVEL_WARN=2, LOG_LEVEL_ERROR=3
    FILE *output;    // Output stream (default: stderr)
} log_config_t;

middleware_fn_t log_middleware_create(const log_config_t *config);
void log_middleware_destroy(void);
```

Output format: `[YYYY-MM-DD HH:MM:SS] LEVEL METHOD /path`

---

## Error Handler Middleware

```c
middleware_fn_t error_handler_middleware_create(void *config);
void error_handler_middleware_destroy(void);
void error_handler_apply(http_response_t *res);
```

Automatically generates `{"error":"Not Found","status":404}` JSON responses for unhandled 4xx/5xx status codes.

---

## Input Validation

```c
bool input_validate_length(const char *input, size_t min_len, size_t max_len);
bool input_validate_charset(const char *input, const char *allowed_chars);
bool input_validate_integer(const char *input, int min_val, int max_val);
bool input_validate_email(const char *input);
bool input_is_alphanumeric(const char *input);
char *input_sanitize_html(const char *input);  // Caller must free; escapes < > & " '
```

---

## Health Check

```c
void health_check_register(router_t *router);  // Registers GET /healthz
```

Response: `{"status":"ok","uptime_seconds":N}`

---

## In-Memory Cache

```c
cache_t *cache_create(size_t max_entries);
void cache_destroy(cache_t *cache);
int cache_set(cache_t *cache, const char *key, const void *value, size_t value_len, int ttl_sec);
const void *cache_get(cache_t *cache, const char *key, size_t *value_len);
int cache_delete(cache_t *cache, const char *key);
void cache_clear(cache_t *cache);
```

LRU eviction when `max_entries` is reached. TTL of 0 means no expiration.

---

## Metrics Middleware

```c
middleware_fn_t metrics_middleware_create(void);
void metrics_middleware_destroy(void);
void metrics_register(router_t *router);       // Registers GET /metrics
void metrics_record_status(int status_code);   // Record a status code
```

Returns JSON metrics: request count, per-method counts, status code ranges (2xx/3xx/4xx/5xx).

---

## Response Compression

```c
int compression_negotiate(const char *accept_encoding);  // Returns COMPRESS_GZIP or COMPRESS_NONE
bool compression_should_compress(const char *content_type, size_t content_length);
int gzip_compress(const void *input, size_t input_len, void **output, size_t *output_len);
uint32_t crc32_compute(const void *data, size_t len);
```

Pure C gzip (RFC 1952) with DEFLATE (RFC 1951). Automatically applied when client sends `Accept-Encoding: gzip`.

---

## Benchmarking

```c
benchmark_t *benchmark_create(const char *name, const char *url);
void benchmark_destroy(benchmark_t *bench);
int benchmark_run(benchmark_t *bench, int num_requests, int concurrency);
void benchmark_print(benchmark_t *bench);
uint64_t benchmark_timestamp_us(void);  // Microsecond timestamp
```

Measures throughput (requests/sec), latency percentiles (p50/p95/p99), and success rate.

---

## Status Codes

```c
HTTP_SWITCHING_PROTOCOLS = 101
HTTP_OK                  = 200
HTTP_CREATED             = 201
HTTP_ACCEPTED            = 202
HTTP_NO_CONTENT          = 204
HTTP_NOT_MODIFIED        = 304
HTTP_BAD_REQUEST         = 400
HTTP_UNAUTHORIZED        = 401
HTTP_FORBIDDEN           = 403
HTTP_NOT_FOUND           = 404
HTTP_TOO_MANY_REQUESTS   = 429
HTTP_INTERNAL_ERROR      = 500
```

---

*Generated for Modern C Web Library v1.0.0 — Pure C, Zero Dependencies*
