# Cloudflare Worker API Reference

Complete API reference for the Cloudflare Workers support layer in the
Modern C Web Library.  All functions listed here are available from both
native builds (for local testing) and Emscripten WASM builds (for
deployment to Cloudflare Workers).  The main entry point
`worker_handle_fetch()` is additionally marked `WASM_EXPORT`.

> **Header:** `#include "kamran.k"`

> **About the limits tables.** Every service below has a `### Limits` section.
> Where a table has a **Cloudflare** column, the `Value` column is what this
> library actually enforces and the `Cloudflare` column is the real service's
> limit — the two mostly agree, and each row that differs says so. Rows marked
> **(in-memory impl)** are capacities of the native simulation you run locally,
> from arrays sized at compile time in `src/worker_kv.c`, `src/worker_r2.c`,
> `src/worker_d1.c`, and `src/worker_queues.c`. They have no Cloudflare
> equivalent, so hitting one locally is not a sign your Worker will fail in
> production. (The D1 table is entirely of this kind and has no Cloudflare
> column.)

---

## Table of Contents

- [Types & Enums](#types--enums)
- [Worker Request](#worker-request)
- [Worker Response](#worker-response)
- [Fetch Handler](#fetch-handler)
- [Environment Context](#environment-context)
- [KV Namespace](#kv-namespace)
- [R2 Object Storage](#r2-object-storage)
- [D1 Database](#d1-database)
- [Queues](#queues)

---

## Types & Enums

### Binding Type Enum

```c
typedef enum {
    WORKER_BINDING_KV,       /* KV Namespace            */
    WORKER_BINDING_R2,       /* R2 Object Storage       */
    WORKER_BINDING_D1,       /* D1 SQL Database         */
    WORKER_BINDING_QUEUE,    /* Queue (producer/consumer) */
    WORKER_BINDING_SECRET,   /* Secret / env variable   */
    WORKER_BINDING_SERVICE   /* Service binding (Worker-to-Worker) */
} worker_binding_type_t;
```

### Forward-Declared Opaque Types

| Type | Description |
|------|-------------|
| `worker_request_t` | Incoming Worker request |
| `worker_response_t` | Outgoing Worker response |
| `worker_env_t` | Environment binding container |
| `worker_kv_t` | KV namespace handle |
| `worker_r2_bucket_t` | R2 bucket handle |
| `worker_d1_t` | D1 database handle |
| `worker_d1_stmt_t` | D1 prepared statement |
| `worker_queue_t` | Queue handle |

### Fetch Handler Typedef

```c
typedef worker_response_t *(*worker_fetch_handler_t)(
    worker_request_t *req, worker_env_t *env);
```

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
| `method` | `const char *` | HTTP method (`"GET"`, `"POST"`, etc.) |
| `url` | `const char *` | Request URL path, optionally with query string |

**Returns:** New request or `NULL` on failure.

### `worker_request_destroy`

```c
void worker_request_destroy(worker_request_t *req);
```

Free a Worker request and all associated memory.

### `worker_request_set_header`

```c
int worker_request_set_header(worker_request_t *req, const char *name,
                              const char *value);
```

Set a header on the request.

**Returns:** `0` on success, `-1` on failure.

### `worker_request_get_header`

```c
const char *worker_request_get_header(const worker_request_t *req,
                                      const char *name);
```

Get a header value by name (case-insensitive).

**Returns:** Header value or `NULL` if absent.

### `worker_request_set_body`

```c
int worker_request_set_body(worker_request_t *req, const char *body,
                            size_t len);
```

Set the request body (data is copied).  Pass `(NULL, 0)` to clear.

**Returns:** `0` on success, `-1` on failure.

### `worker_request_get_method` / `get_url` / `get_body`

```c
const char *worker_request_get_method(const worker_request_t *req);
const char *worker_request_get_url(const worker_request_t *req);
const char *worker_request_get_body(const worker_request_t *req,
                                    size_t *out_len);
```

`get_body` writes the body length to `*out_len` when `out_len` is
non-`NULL`.

---

## Worker Response

Models an outgoing HTTP response.

### `worker_response_create`

```c
worker_response_t *worker_response_create(int status);
```

Create a new response with the given HTTP status code.

| Parameter | Type | Description |
|-----------|------|-------------|
| `status` | `int` | HTTP status code (e.g. `200`, `404`) |

**Returns:** New response or `NULL` on failure.

### `worker_response_destroy`

```c
void worker_response_destroy(worker_response_t *res);
```

Free a Worker response and all associated memory.

### `worker_response_set_header`

```c
int worker_response_set_header(worker_response_t *res, const char *name,
                               const char *value);
```

Set a header on the response (replaces existing, case-insensitive match).

**Returns:** `0` on success, `-1` on failure.

### `worker_response_get_header`

```c
const char *worker_response_get_header(const worker_response_t *res,
                                       const char *name);
```

Get a response header value by name (case-insensitive).

### `worker_response_set_body`

```c
int worker_response_set_body(worker_response_t *res, const char *body,
                             size_t len);
```

Set the response body (binary data, copied).

### `worker_response_set_body_text`

```c
int worker_response_set_body_text(worker_response_t *res, const char *text);
```

Set the response body from a NUL-terminated string.

### `worker_response_set_json`

```c
int worker_response_set_json(worker_response_t *res, json_value_t *json);
```

Serialise a `json_value_t` and set it as the response body.  Also sets
`Content-Type: application/json`.

### `worker_response_get_status`

```c
int worker_response_get_status(const worker_response_t *res);
```

Get the response HTTP status code.

### `worker_response_get_body`

```c
const char *worker_response_get_body(const worker_response_t *res,
                                     size_t *out_len);
```

Get the response body.  Writes the body length to `*out_len` when
`out_len` is non-`NULL`.

---

## Fetch Handler

### `worker_set_fetch_handler`

```c
void worker_set_fetch_handler(worker_fetch_handler_t handler);
```

Register a custom fetch handler.  When set, `worker_handle_fetch()`
delegates to this handler instead of the library router.

### `worker_set_router`

```c
void worker_set_router(router_t *router);
```

Set the library router for automatic route matching.  Used by
`worker_handle_fetch()` when no custom fetch handler is registered.

### `worker_handle_fetch`

```c
WASM_EXPORT
worker_response_t *worker_handle_fetch(worker_request_t *req,
                                       worker_env_t *env);
```

Handle an incoming Worker fetch event.  This is the WASM-exported entry
point called by the JavaScript glue layer.

- If a custom fetch handler is set via `worker_set_fetch_handler()`,
  delegates to it.
- Otherwise uses the router set via `worker_set_router()` for automatic
  route matching.

| Parameter | Type | Description |
|-----------|------|-------------|
| `req` | `worker_request_t *` | Incoming request |
| `env` | `worker_env_t *` | Environment bindings |

**Returns:** Worker response (caller must destroy) or `NULL` on error.

---

## Environment Context

Models the Cloudflare Workers
[`env` object](https://developers.cloudflare.com/workers/runtime-apis/handlers/fetch/#parameters)
that holds named bindings.  Uses a generic `worker_env_add_binding()` /
`worker_env_get_binding()` API with `worker_binding_type_t` for type
safety.

### `worker_env_create` / `worker_env_destroy`

```c
worker_env_t *worker_env_create(void);
void          worker_env_destroy(worker_env_t *env);
```

Create / destroy the environment container.  `worker_env_destroy()` does
**not** destroy the bound handles — the caller owns them.

### `worker_env_add_binding`

```c
int worker_env_add_binding(worker_env_t *env, const char *name,
                           worker_binding_type_t type, void *handle);
```

Register a named binding of the given type.

| Parameter | Type | Description |
|-----------|------|-------------|
| `name` | `const char *` | Binding name (matches `wrangler.toml`) |
| `type` | `worker_binding_type_t` | One of the `WORKER_BINDING_*` values |
| `handle` | `void *` | Pointer to the binding handle |

**Returns:** `0` on success, `-1` on failure.

### `worker_env_get_binding`

```c
void *worker_env_get_binding(const worker_env_t *env, const char *name,
                             worker_binding_type_t type);
```

Look up a binding by name and expected type.

**Returns:** Binding handle or `NULL` if absent or type mismatch.

### `worker_env_binding_count`

```c
int worker_env_binding_count(const worker_env_t *env);
```

Return the number of registered bindings.

### Mapping to `wrangler.toml`

```toml
# wrangler.toml
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
worker_kv_t        *kv = worker_kv_create("CACHE");
worker_r2_bucket_t *r2 = worker_r2_bucket_create("my-assets");
worker_d1_t        *db = worker_d1_create("my-app");
worker_queue_t     *q  = worker_queue_create("my-job-queue");

worker_env_t *env = worker_env_create();
worker_env_add_binding(env, "CACHE",  WORKER_BINDING_KV,    kv);
worker_env_add_binding(env, "ASSETS", WORKER_BINDING_R2,    r2);
worker_env_add_binding(env, "DB",     WORKER_BINDING_D1,    db);
worker_env_add_binding(env, "JOBS",   WORKER_BINDING_QUEUE, q);

/* Retrieve bindings */
worker_kv_t *cache = worker_env_get_binding(env, "CACHE", WORKER_BINDING_KV);
```

---

## KV Namespace

Models the [Cloudflare Workers KV](https://developers.cloudflare.com/kv/)
binding.  Provides an in-memory key-value store with TTL, absolute
expiration, metadata, and paginated listing.

| CF JS API | C API |
|-----------|-------|
| `env.KV.get(key)` | `worker_kv_get(kv, key)` |
| `env.KV.get(key, {type: "text"})` | `worker_kv_get_with_metadata(kv, key, &meta)` |
| `env.KV.put(key, value)` | `worker_kv_put(kv, key, value, NULL)` |
| `env.KV.put(key, value, {expirationTtl, metadata})` | `worker_kv_put(kv, key, value, &opts)` |
| `env.KV.delete(key)` | `worker_kv_delete(kv, key)` |
| `env.KV.list({prefix, limit, cursor})` | `worker_kv_list(kv, &opts)` |

### Types

```c
typedef struct {
    int64_t     expiration;      /* Unix timestamp for absolute expiration */
    int         expiration_ttl;  /* Seconds from now until expiration     */
    const char *metadata;        /* Optional JSON metadata (max 1024 B)  */
} worker_kv_put_options_t;

typedef struct {
    const char *prefix;   /* Only return keys starting with this prefix */
    int         limit;    /* Max keys to return (default 1000, max 1000) */
    int         cursor;   /* Pagination cursor from previous result     */
} worker_kv_list_options_t;

typedef struct {
    char **keys;          /* Array of key strings (caller frees)  */
    int    count;         /* Number of keys returned              */
    bool   list_complete; /* true if all matching keys returned   */
    int    cursor;        /* Cursor for next page (0 if complete) */
} worker_kv_list_result_t;
```

### Functions

```c
worker_kv_t *worker_kv_create(const char *namespace_name);
void         worker_kv_destroy(worker_kv_t *kv);

const char  *worker_kv_get_namespace(const worker_kv_t *kv);

char        *worker_kv_get(worker_kv_t *kv, const char *key);
char        *worker_kv_get_with_metadata(worker_kv_t *kv, const char *key,
                                         char **out_metadata);

int          worker_kv_put(worker_kv_t *kv, const char *key,
                           const char *value,
                           const worker_kv_put_options_t *opts);

int          worker_kv_delete(worker_kv_t *kv, const char *key);

worker_kv_list_result_t *worker_kv_list(worker_kv_t *kv,
                                        const worker_kv_list_options_t *opts);
void worker_kv_list_result_destroy(worker_kv_list_result_t *result);
```

**Key points:**
- `worker_kv_create()` takes the namespace name as a string.
- `worker_kv_get()` returns a newly-allocated `char *` the caller must
  `free()`, or `NULL` if the key does not exist or is expired.
- `worker_kv_get_with_metadata()` additionally writes a newly-allocated
  metadata JSON string to `*out_metadata` (or `NULL`).  Caller frees both.
- `worker_kv_put()` accepts an optional `worker_kv_put_options_t *` for
  TTL, absolute expiration, and metadata.  Pass `NULL` for defaults.
- `worker_kv_list()` returns a `worker_kv_list_result_t *` that must be
  freed with `worker_kv_list_result_destroy()`.
- The native/in-memory implementation holds at most 1024 live keys per
  namespace.  Once full, `worker_kv_put()` returns `-1` for a *new* key;
  updates to existing keys and `worker_kv_delete()` still succeed.  This
  is a limit of the local test implementation, not of Cloudflare KV.

### Limits

| Limit | Value | Cloudflare |
|-------|-------|------------|
| Max key length | 512 bytes | 512 bytes |
| Max value length | 25 MiB | 25 MiB |
| Default list limit | 1000 | 1000 |
| Max metadata size | 1024 bytes | 1024 bytes |
| Max entries per namespace | 1024 (in-memory impl) | no documented key-count limit |

---

## R2 Object Storage

Models the [Cloudflare R2](https://developers.cloudflare.com/r2/) bucket
binding.  Provides in-memory object storage with full metadata (ETag,
content type, upload timestamp).

| CF JS API | C API |
|-----------|-------|
| `env.BUCKET.put(key, body, {httpMetadata})` | `worker_r2_put(bucket, key, body, len, &opts)` |
| `env.BUCKET.get(key)` | `worker_r2_get(bucket, key)` |
| `env.BUCKET.head(key)` | `worker_r2_head(bucket, key)` |
| `env.BUCKET.delete(key)` | `worker_r2_delete(bucket, key)` |
| `env.BUCKET.list({prefix, limit, cursor})` | `worker_r2_list(bucket, &opts)` |

### Types

```c
typedef struct {
    char    *key;           /* Object key                                  */
    uint8_t *body;          /* Object body (NULL for head/list results)    */
    size_t   body_len;      /* Body length                                 */
    size_t   size;          /* Total object size (available without body)  */
    char    *etag;          /* ETag string                                 */
    char    *content_type;  /* Content-Type header                         */
    int64_t  uploaded;      /* Upload timestamp (Unix epoch)               */
} worker_r2_object_t;

typedef struct {
    const char *content_type;   /* MIME type for the object */
} worker_r2_put_options_t;

typedef struct {
    const char *prefix;   /* Prefix filter           */
    int         limit;    /* Max results (default 1000) */
    int         cursor;   /* Pagination cursor       */
} worker_r2_list_options_t;

typedef struct {
    worker_r2_object_t **objects;  /* Array of object pointers (metadata only) */
    int    count;                  /* Number of objects returned */
    bool   truncated;              /* true if more results available */
    int    cursor;                 /* Cursor for next page */
} worker_r2_list_result_t;
```

### Functions

```c
worker_r2_bucket_t *worker_r2_bucket_create(const char *bucket_name);
void                worker_r2_bucket_destroy(worker_r2_bucket_t *bucket);

const char         *worker_r2_bucket_get_name(const worker_r2_bucket_t *bucket);

worker_r2_object_t *worker_r2_get(worker_r2_bucket_t *bucket, const char *key);
worker_r2_object_t *worker_r2_head(worker_r2_bucket_t *bucket, const char *key);

int worker_r2_put(worker_r2_bucket_t *bucket, const char *key,
                  const uint8_t *body, size_t body_len,
                  const worker_r2_put_options_t *opts);

int worker_r2_delete(worker_r2_bucket_t *bucket, const char *key);

worker_r2_list_result_t *worker_r2_list(worker_r2_bucket_t *bucket,
                                        const worker_r2_list_options_t *opts);

void worker_r2_object_destroy(worker_r2_object_t *obj);
void worker_r2_list_result_destroy(worker_r2_list_result_t *result);
```

**Key points:**
- `worker_r2_bucket_create()` takes the bucket name as a string.
- `worker_r2_put()` takes a `const uint8_t *` body and an optional
  `worker_r2_put_options_t *` for content type.  Pass `NULL` for defaults.
- `worker_r2_get()` returns a `worker_r2_object_t *` with the body
  populated, or `NULL` if not found.  Caller frees with
  `worker_r2_object_destroy()`.
- `worker_r2_head()` returns a `worker_r2_object_t *` with metadata only
  (body is `NULL`).
- `worker_r2_list()` returns a `worker_r2_list_result_t *` with metadata-
  only objects.  Free with `worker_r2_list_result_destroy()`.
- The in-memory bucket holds at most 1024 objects.  Once full,
  `worker_r2_put()` returns `-1` for a *new* key; overwriting an existing
  key still succeeds.

### Limits

| Limit | Value | Cloudflare |
|-------|-------|------------|
| Max key length | not enforced | 1024 bytes |
| Max objects per bucket | 1024 (in-memory impl) | no documented object-count limit |

The in-memory R2 simulation does not validate key length: keys longer than
Cloudflare's 1024-byte limit are accepted locally but would be rejected by real
R2.  Keep your own keys within 1024 bytes.

---

## D1 Database

Models the [Cloudflare D1](https://developers.cloudflare.com/d1/) edge SQL
database binding.  Provides in-memory SQL execution with prepared
statements, parameter binding, and batch execution.

| CF JS API | C API |
|-----------|-------|
| `env.DB.prepare(sql)` | `worker_d1_prepare(db, sql)` |
| `stmt.bind(...)` | `worker_d1_stmt_bind(stmt, index, value)` |
| `stmt.run()` | `worker_d1_stmt_run(stmt)` |
| `stmt.first()` | `worker_d1_stmt_first(stmt)` |
| `stmt.all()` | `worker_d1_stmt_all(stmt)` |
| `env.DB.exec(sql)` | `worker_d1_exec(db, sql)` |
| `env.DB.batch([...])` | `worker_d1_batch(db, stmts, count, &out_count)` (not atomic — see key points) |

### Types

```c
typedef struct {
    bool          success;        /* Whether the statement succeeded      */
    json_value_t *results;        /* JSON array of row objects (SELECT)   */
    int           meta_changes;   /* Number of rows affected (INSERT/DELETE) */
} worker_d1_result_t;
```

### Functions

```c
worker_d1_t      *worker_d1_create(const char *database_name);
void              worker_d1_destroy(worker_d1_t *db);
const char       *worker_d1_get_name(const worker_d1_t *db);

/* Prepared statements */
worker_d1_stmt_t *worker_d1_prepare(worker_d1_t *db, const char *sql);
void              worker_d1_stmt_destroy(worker_d1_stmt_t *stmt);
int               worker_d1_stmt_bind(worker_d1_stmt_t *stmt, int index,
                                      const char *value);

/* Execution */
worker_d1_result_t  *worker_d1_stmt_run(worker_d1_stmt_t *stmt);
json_value_t        *worker_d1_stmt_first(worker_d1_stmt_t *stmt);
json_value_t        *worker_d1_stmt_all(worker_d1_stmt_t *stmt);

/* Raw execution (no parameter binding) */
worker_d1_result_t  *worker_d1_exec(worker_d1_t *db, const char *sql);

/* Batch execution */
worker_d1_result_t **worker_d1_batch(worker_d1_t *db,
                                     worker_d1_stmt_t **stmts, int count,
                                     int *out_count);

/* Cleanup */
void worker_d1_result_destroy(worker_d1_result_t *result);
```

**Key points:**
- `worker_d1_create()` takes the database name as a string.
- Use `worker_d1_prepare()` + `worker_d1_stmt_bind()` for parameterised
  queries.  Bind parameters are 1-based.  Pass `NULL` for SQL `NULL`.
- `worker_d1_stmt_run()` returns a `worker_d1_result_t *` with metadata
  (`success`, `meta_changes`).
- `worker_d1_stmt_first()` returns the first row as a `json_value_t *`
  JSON object, or `NULL` if no rows.
- `worker_d1_stmt_all()` returns all rows as a `json_value_t *` JSON
  array.
- `worker_d1_exec()` executes raw SQL without parameter binding (useful
  for DDL like `CREATE TABLE`).
- `worker_d1_batch()` executes an array of prepared statements in
  sequence.  Returns an array of result pointers (`*out_count` results).
  **The batch is not atomic.**  On failure it returns `NULL` and sets
  `*out_count` to 0, but the statements that ran before the failing one
  have already been applied and are not rolled back — and the zeroed
  count means the caller cannot tell how many landed.  This differs from
  Cloudflare D1's `env.DB.batch()`, which runs as an implicit
  transaction.  Do not rely on all-or-nothing semantics.

### Example

```c
worker_d1_t *db = worker_d1_create("my-app");

/* Create table */
worker_d1_result_t *r = worker_d1_exec(db,
    "CREATE TABLE users (id TEXT, name TEXT)");
worker_d1_result_destroy(r);

/* Insert with prepared statement */
worker_d1_stmt_t *stmt = worker_d1_prepare(db,
    "INSERT INTO users VALUES (?, ?)");
worker_d1_stmt_bind(stmt, 1, "1");
worker_d1_stmt_bind(stmt, 2, "Alice");
r = worker_d1_stmt_run(stmt);
worker_d1_result_destroy(r);
worker_d1_stmt_destroy(stmt);

/* Query */
stmt = worker_d1_prepare(db, "SELECT * FROM users WHERE id = ?");
worker_d1_stmt_bind(stmt, 1, "1");
json_value_t *row = worker_d1_stmt_first(stmt);
json_value_free(row);
worker_d1_stmt_destroy(stmt);

worker_d1_destroy(db);
```

### Limits

These are all limits of the in-memory D1 simulation — several (max rows, max
tables) have no Cloudflare equivalent at all, so there is no comparison column
here.

| Limit | Value |
|-------|-------|
| Max bind parameters | 100 |
| Max tables | 32 |
| Max columns per table | 32 |
| Max rows per table | 1024 |
| Max SQL statement length | 8191 bytes |
| Max cell value length | 1023 bytes (longer values are truncated) |
| Max table/column name length | 63 bytes |

`worker_d1_prepare()` returns `NULL` for over-long SQL, and an `INSERT` returns
`NULL` once a table has 1024 rows.  Cell values over 1023 bytes are silently
truncated rather than rejected, so keep them short if exact round-tripping
matters.

---

## Queues

Models the [Cloudflare Queues](https://developers.cloudflare.com/queues/)
binding.  Supports both producing and consuming messages with
acknowledgement.  Messages are stored in memory for local testing.

| CF JS API | C API |
|-----------|-------|
| `env.QUEUE.send(body)` | `worker_queue_send(q, body, len)` |
| `env.QUEUE.send(text)` | `worker_queue_send_text(q, text)` |
| `env.QUEUE.send(json)` | `worker_queue_send_json(q, json)` |
| `env.QUEUE.sendBatch(msgs)` | `worker_queue_send_batch(q, bodies, lengths, count)` |
| Queue consumer handler | `worker_queue_consume(q, max_batch, max_wait)` |
| `message.ack()` | `worker_queue_message_ack(msg)` |

### Types

```c
typedef struct {
    char   *id;         /* Unique message ID              */
    char   *body;       /* Message body                   */
    size_t  body_len;   /* Body length                    */
    int64_t timestamp;  /* Enqueue timestamp (Unix epoch) */
    int     attempts;   /* Delivery attempt count         */
    bool    acked;      /* Whether message has been acknowledged */
} worker_queue_message_t;

typedef struct {
    worker_queue_message_t **messages;   /* Array of message pointers */
    int                      count;      /* Number of messages        */
    char                    *queue_name; /* Source queue name (owned)  */
} worker_queue_batch_t;
```

### Functions

```c
worker_queue_t *worker_queue_create(const char *queue_name);
void            worker_queue_destroy(worker_queue_t *q);

const char     *worker_queue_get_name(const worker_queue_t *q);
int             worker_queue_get_depth(const worker_queue_t *q);

/* Producer */
int  worker_queue_send(worker_queue_t *q, const char *body, size_t body_len);
int  worker_queue_send_text(worker_queue_t *q, const char *text);
int  worker_queue_send_json(worker_queue_t *q, json_value_t *json);
int  worker_queue_send_batch(worker_queue_t *q, const char **bodies,
                             const size_t *lengths, int count);

/* Consumer */
worker_queue_batch_t *worker_queue_consume(worker_queue_t *q, int max_batch,
                                           int max_wait_seconds);
void worker_queue_message_ack(worker_queue_message_t *msg);

/* Cleanup */
void worker_queue_message_destroy(worker_queue_message_t *msg);
void worker_queue_batch_destroy(worker_queue_batch_t *batch);
```

**Key points:**
- `worker_queue_create()` takes the queue name as a string.
- `worker_queue_send()` enqueues a binary body.
  `worker_queue_send_text()` and `worker_queue_send_json()` are
  convenience wrappers.
- `worker_queue_send_batch()` validates the whole batch up front — count,
  each message size, and the resulting queue depth — and returns `-1`
  without enqueuing anything if any check fails.  Up to 100 messages per
  batch.  (The only way to get a partial enqueue is an allocation failure
  part-way through the send loop.)
- `worker_queue_consume()` dequeues messages in a batch.  A `max_batch`
  of 0 or less defaults to 10, and anything above 100 is clamped to 100.
  The `max_wait_seconds` parameter is ignored in the in-memory
  implementation — it returns immediately with whatever is queued.
- `worker_queue_message_ack()` marks a message as processed.
- `worker_queue_batch_destroy()` frees the batch and all contained
  messages.

### Limits

| Limit | Value | Cloudflare |
|-------|-------|------------|
| Max message size | 128 KiB | 128 KB |
| Max batch size | 100 messages | 100 messages |
| Max batch total | not enforced locally | 256 KB |
| Max queued messages | 4096 (in-memory impl) | no documented queue-depth limit |

`worker_queue_send_batch()` checks the message count, each individual message
size, and the resulting queue depth — but never the aggregate batch size.  A
100-message batch of 128 KiB messages (12.8 MiB) is accepted locally and would
be rejected by real Cloudflare Queues, so stay under 256 KB per batch yourself.
