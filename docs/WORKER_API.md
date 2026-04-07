# Cloudflare Worker API Reference

Complete API reference for the Cloudflare Workers support layer in the
Modern C Web Library.  All functions listed here are `WASM_EXPORT` and
available from both native builds (for local testing) and Emscripten WASM
builds (for deployment to Cloudflare Workers).

> **Header:** `#include "kamran.k"`

---

## Table of Contents

- [Worker Request](#worker-request)
- [Worker Response](#worker-response)
- [Fetch Handler](#fetch-handler)
- [KV Namespace](#kv-namespace)
- [R2 Object Storage](#r2-object-storage)
- [D1 Database](#d1-database)
- [Queues Producer](#queues-producer)
- [Environment Context](#environment-context)
- [Runtime Info](#runtime-info)

---

## Worker Request

Models an incoming HTTP request from the Cloudflare Workers fetch event.

### `worker_request_create`

```c
worker_request_t *worker_request_create(const char *method, const char *url);
```

Create a new Worker request.

| Parameter | Type | Description |
|-----------|------|-------------|
| `method` | `const char *` | HTTP method ("GET", "POST", etc.) |
| `url` | `const char *` | Request URL path, optionally with query string |

**Returns:** New request or `NULL` on failure (null params or OOM).

### `worker_request_set_header`

```c
void worker_request_set_header(worker_request_t *req, const char *name, const char *value);
```

Add a header to the request.  Up to 64 headers are supported.

### `worker_request_set_body`

```c
void worker_request_set_body(worker_request_t *req, const char *body, size_t len);
```

Set the request body (data is copied).  Pass `(NULL, 0)` to clear.

### `worker_request_get_method` / `get_url` / `get_header` / `get_body` / `get_body_len`

```c
const char *worker_request_get_method(worker_request_t *req);
const char *worker_request_get_url(worker_request_t *req);
const char *worker_request_get_header(worker_request_t *req, const char *name);  /* case-insensitive */
const char *worker_request_get_body(worker_request_t *req);
size_t      worker_request_get_body_len(worker_request_t *req);
```

### `worker_request_destroy`

```c
void worker_request_destroy(worker_request_t *req);
```

Free a Worker request.  Safe to call with `NULL`.

---

## Worker Response

Models an outgoing HTTP response.

### `worker_response_create`

```c
worker_response_t *worker_response_create(void);
```

Create a new response with default status 200 and no body.

### `worker_response_set_status` / `set_header` / `set_body`

```c
void worker_response_set_status(worker_response_t *res, int status);
void worker_response_set_header(worker_response_t *res, const char *name, const char *value);
void worker_response_set_body(worker_response_t *res, const char *body, size_t len);
```

`set_header` replaces an existing header with the same name (case-insensitive).

### `worker_response_set_text`

```c
void worker_response_set_text(worker_response_t *res, int status, const char *text);
```

Convenience: sets status, `Content-Type: text/plain; charset=utf-8`, and body.

### `worker_response_set_json`

```c
void worker_response_set_json(worker_response_t *res, int status, json_value_t *json);
```

Convenience: serializes JSON, sets status, `Content-Type: application/json; charset=utf-8`, and body.

### Getters

```c
int         worker_response_get_status(worker_response_t *res);
const char *worker_response_get_header(worker_response_t *res, const char *name);
int         worker_response_get_header_count(worker_response_t *res);
const char *worker_response_get_header_name(worker_response_t *res, int index);
const char *worker_response_get_header_value(worker_response_t *res, int index);
const char *worker_response_get_body(worker_response_t *res);
size_t      worker_response_get_body_len(worker_response_t *res);
```

### `worker_response_destroy`

```c
void worker_response_destroy(worker_response_t *res);
```

---

## Fetch Handler

### `worker_handle_fetch`

```c
worker_response_t *worker_handle_fetch(worker_request_t *req, router_t *router);
```

Routes a Worker request through a `router_t` (including middleware) and
returns a `worker_response_t`.  The caller must destroy the response.

Internally creates temporary `http_request_t` / `http_response_t` objects,
dispatches through the router, and converts the result back.

**Returns:** Worker response or `NULL` on error.

---

## KV Namespace

Models the [Cloudflare Workers KV](https://developers.cloudflare.com/kv/)
binding.  Provides an in-memory key-value store with TTL support and key
listing.

| CF JS API | C API |
|-----------|-------|
| `env.KV.get(key)` | `worker_kv_get(kv, key)` |
| `env.KV.put(key, value)` | `worker_kv_put(kv, key, value)` |
| `env.KV.put(key, value, {expirationTtl})` | `worker_kv_put_with_ttl(kv, key, value, ttl)` |
| `env.KV.delete(key)` | `worker_kv_delete(kv, key)` |
| `env.KV.list({prefix, limit})` | `worker_kv_list(kv, prefix, limit, &keys, &count)` |

### Functions

```c
worker_kv_t *worker_kv_create(void);
int          worker_kv_put(worker_kv_t *kv, const char *key, const char *value);
int          worker_kv_put_with_ttl(worker_kv_t *kv, const char *key,
                                    const char *value, int ttl_seconds);
const char  *worker_kv_get(worker_kv_t *kv, const char *key);
int          worker_kv_delete(worker_kv_t *kv, const char *key);
int          worker_kv_list(worker_kv_t *kv, const char *prefix, int limit,
                            const char ***out_keys, int *out_count);
void         worker_kv_list_free(const char **keys);
void         worker_kv_destroy(worker_kv_t *kv);
```

### Limits

| Limit | Value | Cloudflare |
|-------|-------|------------|
| Max entries | 1024 | Unlimited (namespace) |
| Max key length | 512 bytes | 512 bytes |
| Default list limit | 1000 | 1000 |

---

## R2 Object Storage

Models the [Cloudflare R2](https://developers.cloudflare.com/r2/) bucket
binding.  Provides in-memory object storage with metadata.

| CF JS API | C API |
|-----------|-------|
| `env.BUCKET.put(key, body)` | `worker_r2_put(r2, key, data, size, content_type)` |
| `env.BUCKET.get(key)` | `worker_r2_get(r2, key, &size)` |
| `env.BUCKET.head(key)` | `worker_r2_head(r2, key, &size, &content_type)` |
| `env.BUCKET.delete(key)` | `worker_r2_delete(r2, key)` |
| `env.BUCKET.list({prefix})` | `worker_r2_list(r2, prefix, limit, &keys, &count)` |

### Functions

```c
worker_r2_bucket_t *worker_r2_create(void);
int          worker_r2_put(worker_r2_bucket_t *r2, const char *key,
                           const char *data, size_t size,
                           const char *content_type);
const char  *worker_r2_get(worker_r2_bucket_t *r2, const char *key,
                           size_t *out_size);
int          worker_r2_head(worker_r2_bucket_t *r2, const char *key,
                            size_t *out_size, const char **out_content_type);
int          worker_r2_delete(worker_r2_bucket_t *r2, const char *key);
int          worker_r2_list(worker_r2_bucket_t *r2, const char *prefix,
                            int limit, const char ***out_keys, int *out_count);
void         worker_r2_list_free(const char **keys);
void         worker_r2_destroy(worker_r2_bucket_t *r2);
```

### Limits

| Limit | Value | Cloudflare |
|-------|-------|------------|
| Max objects | 256 | Unlimited |
| Max key length | 1024 bytes | 1024 bytes |

---

## D1 Database

Models the [Cloudflare D1](https://developers.cloudflare.com/d1/) edge SQL
database binding.  Provides in-memory SQL execution with CREATE TABLE,
INSERT, and SELECT (with WHERE) support.

| CF JS API | C API |
|-----------|-------|
| `env.DB.prepare(sql).run()` | `worker_d1_exec(d1, sql)` |
| `env.DB.prepare(sql).all()` | `worker_d1_query(d1, sql)` |

### Functions

```c
worker_d1_t        *worker_d1_create(void);
int                 worker_d1_exec(worker_d1_t *d1, const char *sql);
worker_d1_result_t *worker_d1_query(worker_d1_t *d1, const char *sql);
int                 worker_d1_result_get_row_count(worker_d1_result_t *result);
int                 worker_d1_result_get_col_count(worker_d1_result_t *result);
const char         *worker_d1_result_get_col_name(worker_d1_result_t *result, int col);
const char         *worker_d1_result_get_value(worker_d1_result_t *result, int row, int col);
bool                worker_d1_result_is_success(worker_d1_result_t *result);
const char         *worker_d1_result_get_error(worker_d1_result_t *result);
void                worker_d1_result_destroy(worker_d1_result_t *result);
void                worker_d1_destroy(worker_d1_t *d1);
```

### Supported SQL

| Statement | Example |
|-----------|---------|
| CREATE TABLE | `CREATE TABLE users (id TEXT, name TEXT)` |
| CREATE TABLE IF NOT EXISTS | `CREATE TABLE IF NOT EXISTS users (id TEXT)` |
| INSERT INTO … VALUES | `INSERT INTO users VALUES ('1', 'Alice')` |
| SELECT * FROM | `SELECT * FROM users` |
| SELECT * FROM … WHERE | `SELECT * FROM users WHERE id = '1'` |

### Limits

| Limit | Value |
|-------|-------|
| Max tables | 8 |
| Max columns per table | 32 |
| Max rows per table | 256 |
| Max bind parameters | 32 |

---

## Queues Producer

Models the [Cloudflare Queues](https://developers.cloudflare.com/queues/)
producer binding.  Messages are stored in memory for local testing.

| CF JS API | C API |
|-----------|-------|
| `env.QUEUE.send(body)` | `worker_queue_send(q, body, len)` |
| `env.QUEUE.sendBatch(msgs)` | `worker_queue_send_batch(q, bodies, lengths, count)` |

### Functions

```c
worker_queue_t *worker_queue_create(void);
int             worker_queue_send(worker_queue_t *q, const char *body, size_t len);
int             worker_queue_send_batch(worker_queue_t *q, const char **bodies,
                                        const size_t *lengths, int count);
int             worker_queue_get_count(worker_queue_t *q);
const char     *worker_queue_peek(worker_queue_t *q, int index, size_t *out_len);
void            worker_queue_destroy(worker_queue_t *q);
```

### Limits

| Limit | Value | Cloudflare |
|-------|-------|------------|
| Max message size | 128 KB | 128 KB |
| Max batch size | 100 messages | 100 messages |
| Max batch total | 256 KB | 256 KB |

---

## Environment Context

Models the Cloudflare Workers
[`env` object](https://developers.cloudflare.com/workers/runtime-apis/handlers/fetch/#parameters)
that holds named bindings.  Matches the `wrangler.toml` binding pattern.

### Functions

```c
worker_env_t       *worker_env_create(void);
int                 worker_env_bind_kv(worker_env_t *env, const char *name, worker_kv_t *kv);
int                 worker_env_bind_r2(worker_env_t *env, const char *name, worker_r2_bucket_t *r2);
int                 worker_env_bind_d1(worker_env_t *env, const char *name, worker_d1_t *d1);
int                 worker_env_bind_queue(worker_env_t *env, const char *name, worker_queue_t *q);
worker_kv_t        *worker_env_get_kv(worker_env_t *env, const char *name);
worker_r2_bucket_t *worker_env_get_r2(worker_env_t *env, const char *name);
worker_d1_t        *worker_env_get_d1(worker_env_t *env, const char *name);
worker_queue_t     *worker_env_get_queue(worker_env_t *env, const char *name);
void                worker_env_destroy(worker_env_t *env);
```

**Ownership:** `worker_env_t` does **not** take ownership of bound
resources.  The caller must destroy each binding separately after
destroying the env.

### Limits

| Limit | Value |
|-------|-------|
| Max bindings | 32 |

### Mapping to wrangler.toml

```toml
# wrangler.toml
[vars]
ENV = "production"

[[kv_namespaces]]
binding = "CACHE"
id = "abc123"

[[r2_buckets]]
binding = "ASSETS"
bucket_name = "my-assets"

[[d1_databases]]
binding = "DB"
database_name = "my-app"
database_id = "xxx"

[[queues.producers]]
queue = "my-job-queue"
binding = "JOBS"
```

```c
/* C equivalent */
worker_env_bind_kv(env, "CACHE", kv);
worker_env_bind_r2(env, "ASSETS", r2);
worker_env_bind_d1(env, "DB", d1);
worker_env_bind_queue(env, "JOBS", q);
```

---

## Runtime Info

### `worker_runtime_version`

```c
const char *worker_runtime_version(void);
```

Returns `"weblib-worker/1.0"`.

### `worker_runtime_is_supported`

```c
bool worker_runtime_is_supported(void);
```

Returns `true` when Worker runtime support is compiled in.
