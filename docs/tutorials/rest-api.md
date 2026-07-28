# Building a REST API with Modern C Web Library

**Version 2.0.1**

## Introduction

In this tutorial, we'll build a complete REST API for managing a collection of tasks. Our
API will support full CRUD (Create, Read, Update, Delete) operations with the right HTTP
methods, JSON request/response handling, and the middleware stack you would actually want
in front of it: logging, CORS, rate limiting, error handling, metrics and a health check.

The in-memory store we build here is a teaching aid, not a production data layer — the
[note on concurrency](#a-note-on-concurrency) near the end says exactly what you would have
to change first.

**What we'll build:**
- A task management API with the following endpoints:
  - `POST /api/tasks` - Create a new task
  - `GET /api/tasks` - List all tasks
  - `GET /api/tasks/:id` - Get a specific task
  - `PUT /api/tasks/:id` - Update a task
  - `DELETE /api/tasks/:id` - Delete a task
- JSON request/response handling
- Error handling with appropriate HTTP status codes
- Production middleware (logging, CORS, rate limiting, health checks)

**Before you start:** work through [Getting Started](getting-started.md) first. This
tutorial assumes you can already build the library and compile a program against it.

## Project Setup

The library does not export a CMake package config, so `find_package(weblib)` will not
find it. Add the repository as a subdirectory instead — that gives you the `weblib`
target directly:

```cmake
cmake_minimum_required(VERSION 3.10)
project(TaskAPI C)

set(CMAKE_C_STANDARD 11)

# We only want the library itself, not its examples and tests
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)

# Point this at your checkout of modern-c-web-library
add_subdirectory(/path/to/modern-c-web-library modern-c-web-library-build)

add_executable(task_api main.c)
target_link_libraries(task_api PRIVATE weblib)
```

`weblib` carries its include directory and its platform libraries (pthread on Linux and
macOS, `ws2_32`/`bcrypt` on Windows) with it, so linking the one target is enough.

If you would rather not use CMake at all, compiling by hand is a one-liner:

```bash
cc -std=c11 -I /path/to/modern-c-web-library/include \
   main.c -o task_api \
   -L /path/to/modern-c-web-library/build -lweblib -lpthread
```

## Data Model

Everything public lives in one header, `kamran.k`. Here is our task structure and an
in-memory store:

```c
#include "kamran.k"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_TASKS 100
#define MAX_TITLE_LEN 256
#define MAX_DESC_LEN 1024

/* Task record */
typedef struct {
    int id;
    char title[MAX_TITLE_LEN];
    char description[MAX_DESC_LEN];
    bool completed;
    bool active;              /* false = free slot */
} task_t;

/* In-memory task store */
static task_t g_tasks[MAX_TASKS];
static int g_next_id = 1;

static task_t *find_task_by_id(int id) {
    int i;
    for (i = 0; i < MAX_TASKS; i++) {
        if (g_tasks[i].active && g_tasks[i].id == id) {
            return &g_tasks[i];
        }
    }
    return NULL;
}

/* Serialize one task. The caller owns the returned value. */
static json_value_t *task_to_json(const task_t *t) {
    json_value_t *obj = json_object_create();
    if (!obj) {
        return NULL;
    }
    json_object_set(obj, "id", json_number_create((double)t->id));
    json_object_set(obj, "title", json_string_create(t->title));
    json_object_set(obj, "description", json_string_create(t->description));
    json_object_set(obj, "completed", json_bool_create(t->completed));
    return obj;
}

/* Small helper so every error is JSON, not text/plain. */
static void send_error(http_response_t *res, http_status_t status, const char *message) {
    json_value_t *err = json_object_create();
    json_object_set(err, "error", json_string_create(message));
    http_response_send_json(res, status, err);
    json_value_free(err);
}
```

A few API facts worth pinning down before we write handlers:

- A route handler is `void handler(http_request_t *req, http_response_t *res)` — two
  arguments, no `user_data`.
- There is no `http_response_set_status()`. The status is the second argument to
  `http_response_send_text()` / `http_response_send_json()`.
- JSON nodes are all one type, `json_value_t *`. You read a value through the `type`
  field and the `data` union: `v->data.string_val`, `v->data.number_val`,
  `v->data.bool_val`. There are no `JsonString` / `JsonObject` subtypes to cast to.
- `json_object_set()` and `json_array_append()` take ownership of the value you pass, so
  free only the root.
- `http_response_send_json()` sets `Content-Type: application/json; charset=utf-8` for
  you; `http_response_send_text()` sets `text/plain; charset=utf-8`. That is why the
  error helper above builds a real JSON object rather than sending a hand-written
  `{"error":...}` string as text.

## Create Operation (POST /api/tasks)

The create operation parses JSON from the request body, validates it, fills a free slot,
and returns HTTP 201 Created. The request body is on the request struct as `req->body`
(with `req->body_length`); there is no getter function.

```c
static void create_task_handler(http_request_t *req, http_response_t *res) {
    json_value_t *body, *title_val, *desc_val, *out;
    task_t *task = NULL;
    int i;

    if (!req->body || req->body_length == 0) {
        send_error(res, HTTP_BAD_REQUEST, "missing request body");
        return;
    }

    body = json_parse(req->body);
    if (!body || body->type != JSON_OBJECT) {
        send_error(res, HTTP_BAD_REQUEST, "invalid JSON");
        json_value_free(body);          /* NULL-safe */
        return;
    }

    title_val = json_object_get(body, "title");
    desc_val = json_object_get(body, "description");

    if (!title_val || title_val->type != JSON_STRING) {
        send_error(res, HTTP_BAD_REQUEST, "'title' is required and must be a string");
        json_value_free(body);
        return;
    }
    if (!input_validate_length(title_val->data.string_val, 1, MAX_TITLE_LEN - 1)) {
        send_error(res, HTTP_BAD_REQUEST, "'title' must be 1-255 characters");
        json_value_free(body);
        return;
    }

    /* Find a free slot */
    for (i = 0; i < MAX_TASKS; i++) {
        if (!g_tasks[i].active) {
            task = &g_tasks[i];
            break;
        }
    }
    if (!task) {
        send_error(res, HTTP_INTERNAL_ERROR, "task store full");
        json_value_free(body);
        return;
    }

    task->id = g_next_id++;
    snprintf(task->title, sizeof(task->title), "%s", title_val->data.string_val);
    if (desc_val && desc_val->type == JSON_STRING) {
        snprintf(task->description, sizeof(task->description), "%s",
                 desc_val->data.string_val);
    } else {
        task->description[0] = '\0';
    }
    task->completed = false;
    task->active = true;

    json_value_free(body);

    out = task_to_json(task);
    http_response_send_json(res, HTTP_CREATED, out);
    json_value_free(out);
}
```

Two details that matter:

- The 500-level status constant is `HTTP_INTERNAL_ERROR`, not `HTTP_INTERNAL_SERVER_ERROR`.
- `input_validate_length()` comes from the library's input-validation helpers. Its
  siblings `input_validate_integer()`, `input_validate_email()` and friends are all in
  `kamran.k`, and using them beats hand-rolling bounds checks.

## Read All (GET /api/tasks)

Return all tasks wrapped in an object with a count — easier to extend later than a bare
array:

```c
static void get_all_tasks_handler(http_request_t *req, http_response_t *res) {
    json_value_t *arr, *root;
    size_t count;
    int i;

    (void)req;

    arr = json_array_create();
    if (!arr) {
        send_error(res, HTTP_INTERNAL_ERROR, "out of memory");
        return;
    }

    for (i = 0; i < MAX_TASKS; i++) {
        if (g_tasks[i].active) {
            json_array_append(arr, task_to_json(&g_tasks[i]));
        }
    }

    /* Read the length while we still own the array. */
    count = json_array_length(arr);

    root = json_object_create();
    json_object_set(root, "tasks", arr);   /* root now owns arr; don't touch it again */
    json_object_set(root, "count", json_number_create((double)count));

    http_response_send_json(res, HTTP_OK, root);
    json_value_free(root);   /* frees the array and every task object with it */
}
```

## Read One (GET /api/tasks/:id)

Retrieve a single task by ID using a route parameter. Validate the parameter rather than
calling `atoi()` on it — `atoi()` cannot tell you that `"12abc"` or `"99999999999999"`
was not a valid id:

```c
static void get_task_handler(http_request_t *req, http_response_t *res) {
    const char *id_str = http_request_get_param(req, "id");
    long long id_val = 0;
    task_t *task;
    json_value_t *out;

    if (!id_str) {
        send_error(res, HTTP_BAD_REQUEST, "missing task id");
        return;
    }
    if (!input_validate_integer(id_str, 1, 1000000, &id_val)) {
        send_error(res, HTTP_BAD_REQUEST, "invalid task id");
        return;
    }

    task = find_task_by_id((int)id_val);
    if (!task) {
        send_error(res, HTTP_NOT_FOUND, "task not found");
        return;
    }

    out = task_to_json(task);
    http_response_send_json(res, HTTP_OK, out);
    json_value_free(out);
}
```

## Update (PUT /api/tasks/:id)

Update an existing task. Each field is optional — only what the client sends is changed:

```c
static void update_task_handler(http_request_t *req, http_response_t *res) {
    const char *id_str = http_request_get_param(req, "id");
    long long id_val = 0;
    task_t *task;
    json_value_t *body, *title_val, *desc_val, *completed_val, *out;

    if (!id_str || !input_validate_integer(id_str, 1, 1000000, &id_val)) {
        send_error(res, HTTP_BAD_REQUEST, "invalid task id");
        return;
    }

    task = find_task_by_id((int)id_val);
    if (!task) {
        send_error(res, HTTP_NOT_FOUND, "task not found");
        return;
    }

    if (!req->body || req->body_length == 0) {
        send_error(res, HTTP_BAD_REQUEST, "missing request body");
        return;
    }

    body = json_parse(req->body);
    if (!body || body->type != JSON_OBJECT) {
        send_error(res, HTTP_BAD_REQUEST, "invalid JSON");
        json_value_free(body);
        return;
    }

    title_val = json_object_get(body, "title");
    if (title_val && title_val->type == JSON_STRING) {
        if (!input_validate_length(title_val->data.string_val, 1, MAX_TITLE_LEN - 1)) {
            send_error(res, HTTP_BAD_REQUEST, "'title' must be 1-255 characters");
            json_value_free(body);
            return;
        }
        snprintf(task->title, sizeof(task->title), "%s", title_val->data.string_val);
    }

    desc_val = json_object_get(body, "description");
    if (desc_val && desc_val->type == JSON_STRING) {
        snprintf(task->description, sizeof(task->description), "%s",
                 desc_val->data.string_val);
    }

    completed_val = json_object_get(body, "completed");
    if (completed_val && completed_val->type == JSON_BOOL) {
        task->completed = completed_val->data.bool_val;
    }

    json_value_free(body);

    out = task_to_json(task);
    http_response_send_json(res, HTTP_OK, out);
    json_value_free(out);
}
```

Note the type tag is `JSON_BOOL` (the enum is `JSON_NULL`, `JSON_BOOL`, `JSON_NUMBER`,
`JSON_STRING`, `JSON_ARRAY`, `JSON_OBJECT`), and `json_object_get()` returns a borrowed
pointer into the parsed tree — do not free it separately.

## Delete (DELETE /api/tasks/:id)

Remove a task from the store. Because the store is a slot array, deleting is just
clearing the `active` flag — no shuffling, and existing ids stay valid:

```c
static void delete_task_handler(http_request_t *req, http_response_t *res) {
    const char *id_str = http_request_get_param(req, "id");
    long long id_val = 0;
    task_t *task;
    json_value_t *out;

    if (!id_str || !input_validate_integer(id_str, 1, 1000000, &id_val)) {
        send_error(res, HTTP_BAD_REQUEST, "invalid task id");
        return;
    }

    task = find_task_by_id((int)id_val);
    if (!task) {
        send_error(res, HTTP_NOT_FOUND, "task not found");
        return;
    }

    task->active = false;

    out = json_object_create();
    json_object_set(out, "deleted", json_bool_create(true));
    http_response_send_json(res, HTTP_OK, out);
    json_value_free(out);
}
```

You could return `HTTP_NO_CONTENT` here instead. If you do, send an empty string with
`http_response_send_text(res, HTTP_NO_CONTENT, "")` — a 204 carrying a JSON body would be
a protocol violation.

## Adding Production Middleware

Real APIs need logging, CORS, rate limiting, and error handling. The library ships all
four. Each one is a *factory* that returns a `middleware_fn_t`, which you then register
with `router_use_middleware()`:

```c
static void setup_middleware(router_t *router) {
    log_config_t log_cfg = {0};
    cors_options_t cors_cfg = {0};
    ratelimit_config_t rl_cfg = {0};
    middleware_fn_t mw;

    /* Request logging: one line per request, to stderr */
    log_cfg.level = LOG_LEVEL_INFO;
    log_cfg.output = stderr;
    mw = log_middleware_create(&log_cfg);
    if (mw) router_use_middleware(router, mw);

    /* CORS. allowed_origins is a NULL-terminated array of strings;
     * leaving it NULL means "allow any origin". */
    cors_cfg.allowed_origins = NULL;
    cors_cfg.allowed_methods = "GET,POST,PUT,DELETE,OPTIONS";
    cors_cfg.allowed_headers = "Content-Type,Authorization";
    cors_cfg.max_age = 3600;
    mw = cors_middleware_create(&cors_cfg);
    if (mw) router_use_middleware(router, mw);

    /* Rate limiting (token bucket) */
    rl_cfg.requests_per_window = 100;
    rl_cfg.window_seconds = 60;
    rl_cfg.burst_size = 100;
    mw = ratelimit_middleware_create(&rl_cfg);
    if (mw) router_use_middleware(router, mw);

    /* Consistent error responses */
    mw = error_handler_middleware_create(NULL);
    if (mw) router_use_middleware(router, mw);

    /* Request metrics, exposed at GET /metrics */
    mw = metrics_middleware_create();
    if (mw) router_use_middleware(router, mw);

    /* Observability endpoints */
    health_check_register(router);   /* GET /healthz */
    metrics_register(router);        /* GET /metrics */
}
```

Each factory has a matching `*_destroy()` with no arguments — they hold process-global
state, so you tear them down once at shutdown, not per router.

## Complete Example

Here's the full `main()` putting it all together. Note that `http_server_create()` takes
no arguments — the port goes to `http_server_listen()`, which starts the accept loop and
returns immediately, so `main` keeps itself alive:

```c
#include <signal.h>
#include <unistd.h>

static http_server_t *g_server = NULL;
static volatile sig_atomic_t shutdown_requested = 0;

static void signal_handler(int signum) {
    (void)signum;
    shutdown_requested = 1;
}

static void destroy_middleware(void) {
    metrics_middleware_destroy();
    error_handler_middleware_destroy();
    ratelimit_middleware_destroy();
    cors_middleware_destroy();
    log_middleware_destroy();
}

int main(void) {
    router_t *router;

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    memset(g_tasks, 0, sizeof(g_tasks));

    g_server = http_server_create();
    if (!g_server) {
        fprintf(stderr, "Failed to create server\n");
        return 1;
    }

    /* Production settings. All must be set before http_server_listen(). */
    http_server_set_timeout(g_server, 30, 30);       /* read, write (seconds) */
    http_server_set_request_timeout(g_server, 60);   /* whole-request deadline */
    http_server_set_thread_count(g_server, 16);      /* worker threads */

    router = router_create();
    if (!router) {
        fprintf(stderr, "Failed to create router\n");
        http_server_destroy(g_server);
        return 1;
    }

    setup_middleware(router);

    router_add_route(router, HTTP_POST,   "/api/tasks",     create_task_handler);
    router_add_route(router, HTTP_GET,    "/api/tasks",     get_all_tasks_handler);
    router_add_route(router, HTTP_GET,    "/api/tasks/:id", get_task_handler);
    router_add_route(router, HTTP_PUT,    "/api/tasks/:id", update_task_handler);
    router_add_route(router, HTTP_DELETE, "/api/tasks/:id", delete_task_handler);

    http_server_set_router(g_server, router);

    printf("Task API server on http://localhost:8080\n");
    printf("  POST   /api/tasks      - create task\n");
    printf("  GET    /api/tasks      - list all tasks\n");
    printf("  GET    /api/tasks/:id  - get task\n");
    printf("  PUT    /api/tasks/:id  - update task\n");
    printf("  DELETE /api/tasks/:id  - delete task\n");
    printf("  GET    /healthz        - health check\n");
    printf("  GET    /metrics        - request metrics\n");
    printf("\nPress Ctrl+C to stop.\n\n");

    if (http_server_listen(g_server, 8080) < 0) {
        fprintf(stderr, "Failed to listen on port 8080\n");
        http_server_destroy(g_server);
        router_destroy(router);
        destroy_middleware();
        return 1;
    }

    while (!shutdown_requested) {
        sleep(1);
    }

    printf("\nShutting down...\n");

    /* Order matters. http_server_stop() joins the accept thread and drains the
     * worker pool, so once it returns nobody is routing any more; destroying the
     * server before the router keeps that true even if you later drop the
     * explicit stop() call (http_server_destroy() stops a running server for you).
     * The server does not own the router — you always free it yourself. */
    http_server_stop(g_server);
    http_server_destroy(g_server);

    router_destroy(router);
    destroy_middleware();
    return 0;
}
```

### A note on concurrency

The default server is multi-threaded (16 worker threads unless you change it), and the
`g_tasks` array above has no locking. That is fine for a tutorial you drive with `curl`,
but two concurrent writes will corrupt it. Before you put anything like this in front of
real traffic, guard the store with a mutex, or move it behind the connection pool in
`db_pool.h`.

## Testing with curl

Once your server is running, test each endpoint. Remember that JSON object keys come back
in reverse insertion order — that is a property of the library's JSON serializer, not a
bug in your handler.

### Create a task
```bash
curl -X POST http://localhost:8080/api/tasks \
  -H "Content-Type: application/json" \
  -d '{"title":"Buy groceries","description":"Milk, eggs, bread"}'

# Response: {"completed":false,"description":"Milk, eggs, bread","title":"Buy groceries","id":1}
```

### Get all tasks
```bash
curl http://localhost:8080/api/tasks

# Response: {"count":1,"tasks":[{"completed":false,...,"id":1}]}
```

### Get a specific task
```bash
curl http://localhost:8080/api/tasks/1

# Response: {"completed":false,"description":"Milk, eggs, bread","title":"Buy groceries","id":1}
```

### Update a task
```bash
curl -X PUT http://localhost:8080/api/tasks/1 \
  -H "Content-Type: application/json" \
  -d '{"completed":true}'

# Response: {"completed":true,"description":"Milk, eggs, bread","title":"Buy groceries","id":1}
```

`completed` is a JSON boolean (`true`/`false`), not `1`/`0` — the handler checks for
`JSON_BOOL` and will silently ignore a number.

### Delete a task
```bash
curl -X DELETE http://localhost:8080/api/tasks/1

# Response: {"deleted":true}
```

### Try an invalid id
```bash
curl -i http://localhost:8080/api/tasks/abc

# HTTP/1.1 400 Bad Request
# {"error":"invalid task id"}
```

### Check health and metrics
```bash
curl http://localhost:8080/healthz
# Response: {"uptime_seconds":42,"status":"ok"}

curl http://localhost:8080/metrics
```

The health endpoint is `/healthz`, not `/health`.

### Test with verbose output
```bash
curl -v http://localhost:8080/api/tasks
# Shows the full response headers. The CORS middleware ignores requests with no
# Origin header, so send one to see it act:
#   curl -v -H 'Origin: http://example.com' http://localhost:8080/api/tasks
# With allowed_origins = NULL (wildcard) that adds exactly one header:
#   Access-Control-Allow-Origin: *
```

## Summary

You've now built a complete REST API with the Modern C Web Library. This tutorial covered:

- **CRUD operations** — all five standard REST operations
- **JSON handling** — parsing request bodies and building responses with `json_value_t`
- **Route parameters** — dynamic URL segments with `:id`
- **HTTP status codes** — 200, 201, 400, 404, and the correct 500 constant
  (`HTTP_INTERNAL_ERROR`)
- **Production middleware** — logging, CORS, rate limiting, error handling, metrics
- **Input validation** — `input_validate_integer()` and `input_validate_length()` instead
  of `atoi()` and hand-rolled bounds checks
- **Server tuning** — worker threads, socket timeouts, and a whole-request deadline

### Next Steps

- **Thread safety**: add a mutex around the store before serving real traffic
- **Persistence**: replace the in-memory store with a database (see `include/db_pool.h`)
- **Authentication**: add API-key, basic-auth or JWT middleware
- **HTTPS**: see the experimental TLS 1.3 section in
  [Getting Started](getting-started.md#serving-https-experimental-tls-13) — it is
  unaudited, so put a reverse proxy in front for production
- **Testing**: add unit tests and integration tests
- **Deployment**: see the [Deployment Guide](../DEPLOYMENT.md)

For more examples, see:
- [`examples/rest_api_server.c`](../../examples/rest_api_server.c) — the CRUD API this
  tutorial is modeled on, with the full middleware stack
- [`examples/simple_server.c`](../../examples/simple_server.c) — hand-written logging and
  CORS middleware
- [`docs/api/README.md`](../api/README.md) — the complete API reference
