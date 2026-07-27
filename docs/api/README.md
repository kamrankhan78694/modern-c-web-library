# Modern C Web Library - API Reference

**Version 2.0.0**

This is the API reference for the Modern C Web Library: the public surface declared
in `include/kamran.k`, plus the pooled-database API in `include/db_pool.h`. Every
signature below is copied from those headers — if a signature here and the header
disagree, the header wins and this page is a bug. Behaviour notes are checked against
the implementation in `src/` — where a header comment and the code disagreed, the
header comment was corrected in 2.0.0 rather than propagated here.

Some subsystems have a dedicated document of their own. Where that is the case you
get the signatures here and a link out for the design detail, rather than two copies
that can drift apart.

> **One maturity caveat.** The optional TLS 1.3 layer is **experimental and
> unaudited** — read [TLS / HTTPS](#tls--https) before you consider using it.
> It is off by default and compiled out entirely unless you ask for it.

---

## Table of Contents

1. [Library Version & Identity](#library-version--identity)
2. [HTTP Server](#http-server)
3. [TLS / HTTPS](#tls--https)
4. [Router](#router)
5. [Request/Response Helpers](#requestresponse-helpers)
6. [JSON API](#json-api)
7. [Event Loop](#event-loop)
8. [WebSocket](#websocket)
9. [Body Parser](#body-parser)
10. [Cookie Handling](#cookie-handling)
11. [CORS Middleware](#cors-middleware)
12. [Rate Limiting Middleware](#rate-limiting-middleware)
13. [Static File Middleware](#static-file-middleware)
14. [Session Management](#session-management)
15. [Template Engine](#template-engine)
16. [Authentication Middleware](#authentication-middleware)
17. [Security Headers Middleware](#security-headers-middleware)
18. [Security Utilities](#security-utilities)
19. [Environment Configuration](#environment-configuration)
20. [Database Connection Pool](#database-connection-pool)
21. [Thread Pool & Server Hardening](#thread-pool--server-hardening)
22. [CSRF Middleware](#csrf-middleware)
23. [Logging Middleware](#logging-middleware)
24. [Error Handler Middleware](#error-handler-middleware)
25. [Input Validation](#input-validation)
26. [Health Check](#health-check)
27. [In-Memory Cache](#in-memory-cache)
28. [Metrics Middleware](#metrics-middleware)
29. [Response Compression](#response-compression)
30. [Benchmarking](#benchmarking)
31. [Cloudflare Workers & WebAssembly](#cloudflare-workers--webassembly)
32. [Status Codes](#status-codes)

---

## Library Version & Identity

Compile-time macros and their runtime accessors. Use the encoded number for
`#if` version gates, the string for logging.

```c
#define WEBLIB_VERSION_MAJOR 2
#define WEBLIB_VERSION_MINOR 0
#define WEBLIB_VERSION_PATCH 0
#define WEBLIB_VERSION       "2.0.0"

/* "weblib/2.0.0 (author:kamran)" — sent as the Server header on every response */
#define WEBLIB_VERSION_STRING

/* Encode a triplet for comparison: WEBLIB_VERSION_ENCODE(1, 2, 3) → 10203 */
#define WEBLIB_VERSION_ENCODE(major, minor, patch)
#define WEBLIB_VERSION_NUMBER   /* the current version, encoded */

const char *weblib_version(void);            // "2.0.0"
const char *weblib_kamran_signature(void);   // Server header string
void weblib_version_components(int *major, int *minor, int *patch);
```

`weblib_version_components()` accepts NULL for any component you do not need.

```c
#if WEBLIB_VERSION_NUMBER >= WEBLIB_VERSION_ENCODE(2, 0, 0)
    /* the keyed session API and http_server_enable_tls() are available */
#endif
```

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
Enable/disable async I/O mode. The default is threaded mode.
```c
int http_server_set_async(http_server_t *server, bool enable);
// Returns: 0 on success, -1 on failure
```
Enabling async mode fails (returns -1) if TLS is already enabled on the server —
TLS termination is wired only into the threaded path. See [TLS / HTTPS](#tls--https).

### `http_server_get_event_loop()`
Get the event loop (async mode only).
```c
event_loop_t *http_server_get_event_loop(http_server_t *server);
// Returns: Event loop instance, or NULL if not in async mode
```

---

## TLS / HTTPS

> **EXPERIMENTAL and UNAUDITED.** This is a hand-written, zero-dependency TLS 1.3
> implementation in pure C (5,481 lines under `src/tls/`). It has not had an
> external cryptographic audit. Do not put it in front of anything you care about
> until it has.

One function turns an `http_server_t` into an HTTPS server. Everything else —
routes, middleware, handlers — is unchanged; TLS is terminated underneath HTTP, so
your handlers never see it.

**Design detail lives in [`src/tls/README.md`](../../src/tls/README.md).** That is the
authoritative document for the protocol scope, the security model, and the roadmap.

### `http_server_enable_tls()`
Terminate TLS 1.3 on every connection the server accepts.
```c
int http_server_enable_tls(http_server_t *server,
                           const char *cert_pem, size_t cert_len,
                           const char *key_pem,  size_t key_len);
// Returns: 0 on success, -1 on failure
```
Declared in `include/kamran.k`.

**Parameters**

| Parameter | Meaning |
|---|---|
| `server`   | Server instance, not yet listening |
| `cert_pem` | PEM `CERTIFICATE` block — the server certificate. **A buffer, not a file path.** |
| `cert_len` | Length of `cert_pem` in bytes |
| `key_pem`  | PEM `PRIVATE KEY` block — a PKCS#8 **Ed25519** private key. **A buffer, not a file path.** |
| `key_len`  | Length of `key_pem` in bytes |

The two most common mistakes are passing a filename where a buffer is expected, and
forgetting that the lengths are explicit. You read the files yourself and hand over
the bytes; the server copies what it needs, so you may free your buffers as soon as
the call returns.

**Returns** `0` on success. It returns `-1` — without partially enabling anything —
in each of these cases:

- `server`, `cert_pem` or `key_pem` is NULL, or either length is 0
- the certificate PEM or the private key PEM is malformed, or the key is not Ed25519
- async mode is enabled on this server (TLS is threaded-mode only)
- TLS has already been enabled on this server
- the server is already listening — **this call must come before
  `http_server_listen()`**
- the library was built without `WEBLIB_ENABLE_TLS` (see [Build](#build) below)

That last one is the one that surprises people: the symbol always exists so your
program links either way, but in a non-TLS build the function body is a stub that
does nothing and returns `-1`. If `enable_tls` fails on a machine where you expected
it to work, check how the library was configured before you check your certificate.

### Example

```c
#include "kamran.k"
#include <stdio.h>
#include <stdlib.h>

/* Read a whole file into a NUL-terminated buffer; caller frees. */
static char *read_file(const char *path, size_t *len_out) {
    FILE *f = fopen(path, "rb");
    long sz;
    char *buf;
    size_t got;

    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0 || (sz = ftell(f)) < 0) { fclose(f); return NULL; }
    rewind(f);
    if (!(buf = malloc((size_t)sz + 1))) { fclose(f); return NULL; }
    got = fread(buf, 1, (size_t)sz, f);
    buf[got] = '\0';
    *len_out = got;
    fclose(f);
    return buf;
}

static void handle_root(http_request_t *req, http_response_t *res) {
    (void)req;
    http_response_send_text(res, HTTP_OK, "Hello over pure-C TLS 1.3!\n");
}

int main(void) {
    size_t cert_len = 0, key_len = 0;
    char *cert = read_file("cert.pem", &cert_len);
    char *key  = read_file("key.pem",  &key_len);
    http_server_t *server = http_server_create();
    router_t *router = router_create();

    if (!cert || !key || !server || !router) return 1;

    router_add_route(router, HTTP_GET, "/", handle_root);
    http_server_set_router(server, router);

    /* Buffers + explicit lengths. Must precede http_server_listen(). */
    if (http_server_enable_tls(server, cert, cert_len, key, key_len) != 0) {
        fprintf(stderr, "enable_tls failed — is this a -DWEBLIB_ENABLE_TLS=ON build, "
                        "and are cert/key a valid Ed25519 pair?\n");
        return 1;
    }

    /* The server copied what it needs; the PEM buffers can go now. */
    free(cert);
    free(key);

    if (http_server_listen(server, 8443) < 0) return 1;
    /* ... run until shutdown ... */

    http_server_stop(server);
    http_server_destroy(server);
    router_destroy(router);
    return 0;
}
```

A complete, buildable version of this is [`examples/tls_server.c`](../../examples/tls_server.c)
(routes `/`, `/hello`, `/big`; default port 8443).

### Build

TLS is **off by default**. With `WEBLIB_ENABLE_TLS` off, no file under `src/tls/` is
compiled and the resulting binary is byte-identical to a build of the library without
the TLS work at all — you pay nothing for a feature you did not ask for.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo -DWEBLIB_ENABLE_TLS=ON
cmake --build build --parallel
```

Generate an Ed25519 test certificate — Ed25519 is the only key type this server
accepts:

```bash
openssl genpkey -algorithm ed25519 -out key.pem
openssl req -x509 -new -key key.pem -out cert.pem -days 365 -subj "/CN=localhost"
```

Run the example and talk to it:

```bash
./build/examples/tls_server cert.pem key.pem 8443

printf 'GET / HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n' \
  | openssl s_client -quiet -connect 127.0.0.1:8443 -tls1_3
```

There is a second option, `WEBLIB_TLS_TEST_HOOKS` (also default OFF), which exposes a
deterministic-RNG seam used by one test suite. It must never be on in a build you
deploy.

### Scope

Read this before designing around the TLS layer. These are boundaries, not bugs.

- **One cipher profile, no agility:** `TLS_CHACHA20_POLY1305_SHA256` with X25519 key
  exchange and Ed25519 signatures. No AES-GCM, no RSA, no ECDSA, no TLS 1.2. A client
  that cannot offer all three is refused with `handshake_failure`. Choosing one good
  profile and refusing to negotiate is deliberate — it removes a whole class of
  downgrade bug.
- **Server-side only.** There is no TLS client.
- **Threaded mode only.** `http_server_enable_tls()` returns -1 under async mode, and
  `http_server_set_async()` returns -1 once TLS is on.
- **Native builds only.** Not available in WebAssembly or Cloudflare Workers builds.
- **WebSocket over TLS is refused.** A request that would upgrade to WebSocket on a
  TLS connection gets `503 Service Unavailable` with the body
  `WebSocket over TLS not supported`, rather than plaintext WS frames over the
  encrypted socket. Plain `ws://` on a non-TLS server is unaffected.
- **The handshake is bounded by the total-request deadline** set with
  [`http_server_set_request_timeout()`](#thread-pool--server-hardening) (default 60
  seconds) — a client cannot hold a worker thread by stalling mid-handshake. Note
  this is the *request* timeout, not `http_server_set_timeout()`, which sets the
  per-recv/send socket timeouts.
- **Interoperability:** a real `openssl s_client` TLS 1.3 handshake and HTTPS
  round-trip is verified in CI, including a response larger than 16 KiB (which spans
  multiple TLS records) and keep-alive with two requests on one connection.
  **Browser page-load is not achieved** — Ed25519-only certificates have limited and
  inconsistent browser support, so do not plan on pointing a browser at this.

Deliberately not implemented yet: ChangeCipherSpec emission, the downgrade sentinel,
session resumption / 0-RTT / KeyUpdate, client-certificate authentication, and
SNI-based certificate selection. ALPN negotiates `http/1.1`; HTTP/2 is not offered.

---

## Router

### Handler Types
```c
typedef void (*route_handler_t)(http_request_t *req, http_response_t *res);

/* Return true to continue the chain, false to stop and send what you have. */
typedef bool (*middleware_fn_t)(http_request_t *req, http_response_t *res,
                                void *user_data);
```

A middleware returning `false` short-circuits the request: no later middleware runs
and the route handler is skipped, so set the response before you return. The chain
also stops if a middleware has already sent a response, whatever it returns. The
`user_data` argument is whatever you passed to
[`router_use_middleware_with_data()`](#router), and `NULL` for middleware registered
with plain `router_use_middleware()`.

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
// Returns: 0 on success, -1 on failure
```

### `router_use_middleware_with_data()`
Same, but with a per-instance context pointer.
```c
int router_use_middleware_with_data(router_t *router, middleware_fn_t middleware,
                                    void *user_data);
// Returns: 0 on success, -1 on failure
```
`user_data` is handed back to the middleware as its third argument on every call.
It is how you give a middleware you wrote its own state instead of a file-static
global.

Two things to know before you plan around it. Middleware registered on a router runs
for **every** request that router handles — there is no per-path registration, so you
cannot scope one instance to `/api` and another to everything else. And among the
built-ins, only some read `user_data`: the CORS, Basic/API-key/JWT auth, and logging
middlewares treat it as a pointer to their own config struct (`cors_options_t *`,
`jwt_auth_config_t *`, `log_config_t *`, and so on) and fall back to the configuration
registered by their `*_create()` call when it is `NULL`. Rate limiting is the
exception — its state type is not part of the public header, so it cannot be handed a
per-instance pointer.

### `router_route()`
Dispatch a request through the middleware chain and into the matching handler.
```c
int router_route(router_t *router, http_request_t *req, http_response_t *res);
// Returns:  0  a route matched and ran, or a middleware stopped the chain
//           1  no route matched — a 404 "Not Found" body is filled in for you
//          -1  router, req, res, or req->path was NULL; nothing was written
```
The HTTP server calls this for you. You need it directly only when you are driving
the router yourself — in a Cloudflare Worker, or in a test.

> The `@return` comment on this function in `include/kamran.k` used to read "-1 if not
> found", which predates the built-in 404 fallback; it was corrected in 2.0.0 to match
> the three values above. If you are reading an older header, the safe reading is
> "non-zero means no route handler ran".

---

## Request/Response Helpers

### Request
```c
const char *http_request_get_header(http_request_t *req, const char *key);
const char *http_request_get_param(http_request_t *req, const char *key);
int http_request_set_param(http_request_t *req, const char *key, const char *value);
// set_param returns 0 on success, -1 on failure; the router calls it during matching
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
json_value_t *json_null_create(void);
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

## Event Loop

The non-blocking I/O core behind async mode. You can use it directly for your own
descriptors, or let `http_server_set_async()` create and drive one for you.

### Types
```c
typedef enum {
    EVENT_READ    = 1 << 0,  /* Descriptor is readable */
    EVENT_WRITE   = 1 << 1,  /* Descriptor is writable */
    EVENT_ERROR   = 1 << 2,  /* Error condition on descriptor */
    EVENT_TIMEOUT = 1 << 3   /* Timeout fired */
} event_type_t;

typedef void (*event_callback_t)(int fd, int events, void *user_data);
```

### Lifecycle
```c
event_loop_t *event_loop_create(void);
int  event_loop_run(event_loop_t *loop);    // Blocks; 0 on normal exit, -1 on error
void event_loop_stop(event_loop_t *loop);
void event_loop_destroy(event_loop_t *loop);
```

### Descriptors
```c
int event_loop_add_fd(event_loop_t *loop, int fd, int events,
                      event_callback_t callback, void *user_data);
int event_loop_modify_fd(event_loop_t *loop, int fd, int events);
int event_loop_remove_fd(event_loop_t *loop, int fd);
// All return 0 on success, -1 on failure
```

### Timers
```c
int event_loop_add_timeout(event_loop_t *loop, int timeout_ms,
                           event_callback_t callback, void *user_data);
// Returns: timer ID on success, -1 on failure

int event_loop_cancel_timeout(event_loop_t *loop, int timer_id);
// Returns: 0 on success, -1 on failure

int event_loop_get_timer_count(event_loop_t *loop);  // Active timers, -1 on failure
int event_loop_get_max_timers(void);                 // Compile-time capacity
```

Timer capacity is a fixed compile-time limit. If you are registering timers
dynamically, compare `event_loop_get_timer_count()` against
`event_loop_get_max_timers()` rather than assuming the add will succeed.

---

## WebSocket

RFC 6455 WebSocket, usable from a route handler once the client asks to upgrade.
**For the full guide — handshake flow, framing, ping/pong, and worked examples — see
[`docs/WEBSOCKET.md`](../WEBSOCKET.md).** The signatures are collected here.

> WebSocket upgrades over a TLS connection are refused with `503`. See
> [TLS / HTTPS](#tls--https).

### Types
```c
typedef enum { WS_MESSAGE_TEXT, WS_MESSAGE_BINARY } ws_message_type_t;

typedef enum {
    WS_CLOSE_NORMAL = 1000, WS_CLOSE_GOING_AWAY = 1001, WS_CLOSE_PROTOCOL_ERROR = 1002,
    WS_CLOSE_UNSUPPORTED = 1003, WS_CLOSE_NO_STATUS = 1005, WS_CLOSE_ABNORMAL = 1006,
    WS_CLOSE_INVALID_DATA = 1007, WS_CLOSE_POLICY = 1008, WS_CLOSE_TOO_LARGE = 1009,
    WS_CLOSE_EXTENSION = 1010, WS_CLOSE_UNEXPECTED = 1011, WS_CLOSE_TLS_FAILED = 1015
} ws_close_code_t;

typedef void (*websocket_message_cb_t)(websocket_connection_t *conn,
                                       ws_message_type_t type,
                                       const void *data, size_t len);
typedef void (*websocket_close_cb_t)(websocket_connection_t *conn, uint16_t code);
typedef void (*websocket_error_cb_t)(websocket_connection_t *conn, const char *error);
typedef void (*websocket_connect_cb_t)(websocket_connection_t *conn, void *user_data);
```

### Handshake & Lifecycle
```c
bool websocket_handle_upgrade(http_request_t *req, http_response_t *res);
websocket_connection_t *websocket_connection_create(int fd);
void websocket_connection_destroy(websocket_connection_t *conn);
```

### Sending
```c
int websocket_send(websocket_connection_t *conn, ws_message_type_t type,
                   const void *data, size_t len);
int websocket_send_text(websocket_connection_t *conn, const char *text);
int websocket_send_binary(websocket_connection_t *conn, const void *data, size_t len);
int websocket_send_ping(websocket_connection_t *conn, const void *data, size_t len);
int websocket_send_pong(websocket_connection_t *conn, const void *data, size_t len);
int websocket_close(websocket_connection_t *conn, uint16_t code, const char *reason);
// All return 0 on success, -1 on failure
```

### Receiving
```c
int websocket_process_data(websocket_connection_t *conn,
                           const uint8_t *data, size_t len);
// Parses frames and invokes the registered callbacks. 0 on success, -1 on failure
```

### Callbacks & State
```c
void websocket_set_message_callback(websocket_connection_t *conn, websocket_message_cb_t cb);
void websocket_set_close_callback(websocket_connection_t *conn, websocket_close_cb_t cb);
void websocket_set_error_callback(websocket_connection_t *conn, websocket_error_cb_t cb);
void websocket_set_user_data(websocket_connection_t *conn, void *user_data);

void *websocket_get_user_data(websocket_connection_t *conn);
bool  websocket_is_open(websocket_connection_t *conn);
int   websocket_get_fd(websocket_connection_t *conn);   // -1 if conn is NULL

websocket_message_cb_t websocket_get_message_callback(websocket_connection_t *conn);
websocket_close_cb_t   websocket_get_close_callback(websocket_connection_t *conn);
websocket_error_cb_t   websocket_get_error_callback(websocket_connection_t *conn);
```

### Async WebSocket Manager
Non-blocking WebSocket I/O driven by an event loop. Sends are queued and flushed when
the socket becomes writable, so a slow peer never blocks the loop.
```c
typedef struct async_ws_manager async_ws_manager_t;

async_ws_manager_t *async_ws_manager_create(event_loop_t *loop);
void async_ws_manager_destroy(async_ws_manager_t *mgr);

void async_ws_manager_set_callbacks(async_ws_manager_t *mgr,
                                    websocket_message_cb_t on_msg,
                                    websocket_close_cb_t on_close,
                                    websocket_error_cb_t on_error);

int async_ws_manager_add(async_ws_manager_t *mgr, websocket_connection_t *ws);
int async_ws_manager_remove(async_ws_manager_t *mgr, websocket_connection_t *ws);
int async_ws_send(async_ws_manager_t *mgr, websocket_connection_t *ws,
                  ws_message_type_t type, const void *data, size_t len);
size_t async_ws_manager_count(async_ws_manager_t *mgr);
```
`async_ws_manager_destroy()` closes the managed connections but does **not** destroy
the underlying `websocket_connection_t` objects — those remain yours to free.

---

## Body Parser

Parses `application/x-www-form-urlencoded` and `multipart/form-data` request bodies.
The accessors parse on first use, so calling `http_request_parse_body()` yourself is
optional.

### Types
```c
struct http_uploaded_file {
    char *field_name;       /* Form field name */
    char *filename;         /* Original filename (sanitized) */
    char *content_type;     /* MIME type */
    uint8_t *data;          /* File data */
    size_t size;            /* File data size */
    struct http_uploaded_file *next;
};

struct http_form_field {
    char *name;
    char *value;
    struct http_form_field *next;
};
```

### Functions
```c
int http_request_parse_body(http_request_t *req);   // 0 on success, -1 on failure
const char *http_request_get_form_field(http_request_t *req, const char *name);
http_uploaded_file_t *http_request_get_file(http_request_t *req, const char *field_name);
void body_parser_data_free(body_parser_data_t *data);
```

`filename` is sanitized on the way in, but it is still attacker-supplied text. Never
concatenate it into a path you write to without validating it yourself.

---

## Cookie Handling

### Options
```c
typedef struct cookie_options {
    const char *domain;     /* Domain attribute */
    const char *path;       /* Path attribute (default: "/") */
    int max_age;            /* Max-Age in seconds (-1 = session cookie) */
    bool secure;            /* Secure flag */
    bool http_only;         /* HttpOnly flag */
    const char *same_site;  /* "Strict", "Lax", or "None" */
} cookie_options_t;
```

### Functions
```c
const char *http_request_get_cookie(http_request_t *req, const char *name);
void http_response_set_cookie(http_response_t *res, const char *name,
                              const char *value, const cookie_options_t *options);
void http_response_delete_cookie(http_response_t *res, const char *name);
```

Pass `NULL` for `options` to take the defaults. For anything holding a session or a
token, set `http_only` and `secure` and pick a `same_site` value explicitly —
`delete_cookie` works by setting `Max-Age=0`.

---

## CORS Middleware

### Configuration
```c
typedef struct cors_options {
    const char **allowed_origins;  /* NULL-terminated array, or NULL for "*" */
    const char *allowed_methods;   /* Comma-separated methods */
    const char *allowed_headers;   /* Comma-separated headers */
    const char *expose_headers;    /* Comma-separated headers to expose */
    bool allow_credentials;        /* Allow credentials */
    int max_age;                   /* Preflight cache duration in seconds */
} cors_options_t;
```

### Functions
```c
middleware_fn_t cors_middleware_create(const cors_options_t *options);
void cors_middleware_destroy(void);
```

Passing `NULL` returns `NULL` — no middleware is created, so always pass a real
`cors_options_t`. Leaving `allowed_origins` NULL *inside* that struct selects the
wildcard `*` origin: convenient in development and wrong in production, so name your
origins explicitly before you ship. `allow_credentials` combined with a wildcard
origin is refused outright by `cors_middleware_create()` (it returns NULL — CWE-942),
and browsers reject it too.

---

## Rate Limiting Middleware

### Configuration
```c
typedef struct ratelimit_config {
    int requests_per_window;  /* Max requests per window */
    int window_seconds;       /* Window length in seconds */
    int burst_size;           /* Token bucket capacity */
} ratelimit_config_t;
```

### Functions
```c
middleware_fn_t ratelimit_middleware_create(const ratelimit_config_t *config);
void ratelimit_middleware_destroy(void);
```

Requests are bucketed by client IP into a token bucket. A client that runs out of
tokens gets `HTTP_TOO_MANY_REQUESTS` (429) with a `Retry-After` header, and the
middleware chain stops there. If the client IP cannot be determined the request is
allowed through — the limiter fails open, so it is a courtesy control, not a
security boundary.

There is one rate limiter per process. `ratelimit_middleware_create()` returns only
the middleware function; the limiter state itself is internal and has no public
handle, so calling it a second time **replaces** the first limiter rather than adding
an independent one. Configure it once.

---

## Static File Middleware

### Configuration
```c
typedef struct static_file_config {
    const char *root_dir;    /* Root directory for static files */
    const char *index_file;  /* Default index file (default: "index.html") */
    int cache_max_age;       /* Cache-Control max-age in seconds (default: 3600) */
    bool enable_etag;        /* Enable ETag support (default: true) */
} static_file_config_t;
```

### Functions
```c
middleware_fn_t static_file_middleware_create(const static_file_config_t *config);
void static_file_middleware_destroy(void);
```

Requests are confined to `root_dir`. Point it at a directory that contains only
things you are happy to serve to the public.

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

Keyed on `(store, session_id)` — **not** on a `session_t *` handle.

```c
int session_set_data(session_store_t *store, const char *session_id,
                     const char *key, const char *value);
// Returns: 0 on success; -1 if the session is missing/expired, or on allocation failure
// The value is copied.

char *session_get_data(session_store_t *store, const char *session_id,
                       const char *key);
// Returns: a NEWLY ALLOCATED copy of the value — CALLER MUST free() IT —
//          or NULL if the session or key is not found

int session_remove_data(session_store_t *store, const char *session_id,
                        const char *key);
// Returns: 0 if the key was removed, -1 if the session or key was not found
```

Why the store and the ID rather than a handle? Because these three re-resolve the
session under the store's lock on every call, so no raw session pointer ever escapes
that lock. And `session_get_data()` hands back a copy rather than a pointer into the
store's own memory, which closes the use-after-free window a borrowed pointer would
otherwise open. It costs you a `free()`; it buys you a class of bug you cannot hit.

### Session Utilities
```c
const char *session_get_id(session_t *session);
bool session_is_expired(session_t *session);
int session_cleanup_expired(session_store_t *store);  // Returns: number reclaimed
session_t *session_from_request(session_store_t *store, http_request_t *req);
void session_set_cookie(http_response_t *res, const char *session_id,
                        int max_age, const char *path);
void session_store_set_idle_timeout(session_store_t *store, int seconds);
```

**Treat `session_t *` as a short-lived existence check, nothing more.** A handle from
`session_get()` or `session_from_request()` is good for an immediate,
single-threaded `!= NULL` test — or an immediate `session_get_id()` /
`session_is_expired()` — and that is all. Do not store it and do not read data
through it: another thread can destroy or expire the session, and its fixed slot may
be reused by a different session, so a retained handle can silently alias the wrong
one. For data, use the keyed functions above.

`session_store_set_idle_timeout()` controls how session-cookie sessions
(`max_age == 0`) get reclaimed. Those have no absolute expiry, so without an idle
timeout they would accumulate and permanently consume slots in the store's fixed
pool. The default reclaims one that has been untouched for 1800 seconds; pass a
different value to tune it, or `<= 0` to disable idle reclamation entirely. Sessions
created with `max_age > 0` are unaffected — they keep their absolute deadline.

The session cookie is named `MCWL_SESSION`.

### Example
```c
/* One store for the process; create it in main() before the server starts. */
static session_store_t *store;

// Login handler
void handle_login(http_request_t *req, http_response_t *res) {
    char *sid;

    (void)req;
    sid = session_create(store, 3600);         // caller frees the ID
    if (!sid) {
        http_response_send_text(res, HTTP_INTERNAL_ERROR, "Session failed");
        return;
    }
    session_set_data(store, sid, "user_id", "42");
    session_set_cookie(res, sid, 3600, "/");
    free(sid);
    http_response_send_text(res, HTTP_OK, "Logged in");
}

// Protected route
void handle_profile(http_request_t *req, http_response_t *res) {
    const char *sid = http_request_get_cookie(req, "MCWL_SESSION");
    char *user_id;

    if (!sid || !session_get(store, sid)) {          // existence check only
        http_response_send_text(res, HTTP_UNAUTHORIZED, "Login required");
        return;
    }

    user_id = session_get_data(store, sid, "user_id");  // owned copy
    if (user_id) {
        // ... use user_id ...
        free(user_id);
    }
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

### Escaping

Substitution is HTML-escaped by default, so the safe thing is what happens if you do
nothing:

| Syntax | Behaviour |
|---|---|
| `{{ name }}`   | Value is HTML-escaped (`& < > " '`) — **safe default** |
| `{{{ name }}}` | Value is emitted raw — **trusted HTML only** |

Unknown variables render as empty. Only reach for the triple-brace form with values
you fully control; binding untrusted input with `{{{ }}}` puts the XSS back.

Escaping covers HTML text and quoted attribute values. It is **not** sufficient for
unquoted attributes, `javascript:` / `data:` URLs, or the body of a `<script>` or
`<style>` element — do not drop template output into those contexts without
additional, context-appropriate encoding.

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
router_use_middleware(router, mw);
// ...
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
router_use_middleware(router, mw);
// ...
apikey_auth_middleware_destroy();
```

### JWT Auth (HMAC-SHA256)
```c
jwt_auth_config_t config = {
    .secret = "my-secret-key",
    .secret_len = 13,
    .header_name = NULL,  // Defaults to "Authorization"
    .require_exp = false  // Set true to reject tokens that carry no "exp" claim
};

middleware_fn_t mw = jwt_auth_middleware_create(&config);
router_use_middleware(router, mw);
// ...
jwt_auth_middleware_destroy();
```

The verifier accepts **only** `alg: "HS256"` (the header's `alg` field is parsed and
compared exactly — not substring-matched) and validates the time claims when present:
an expired `exp` or a not-yet-valid `nbf` is rejected. `exp` is OPTIONAL by default
(RFC 7519); set `require_exp = true` so a token lacking `exp` is refused, preventing a
leaked exp-less token from replaying indefinitely.

---

## Security Headers Middleware

Sets the standard defensive response headers on every response, so you do not have to
remember them per-route.

| Header | Purpose |
|---|---|
| `Content-Security-Policy`   | Restricts resource loading (XSS mitigation) |
| `X-Content-Type-Options`    | Prevents MIME-type sniffing |
| `X-Frame-Options`           | Clickjacking protection |
| `Referrer-Policy`           | Controls referrer leakage |
| `Permissions-Policy`        | Disables dangerous browser features |
| `Strict-Transport-Security` | Forces HTTPS — opt in via `enable_hsts` |
| `X-XSS-Protection: 0`       | Disables the legacy XSS filter; CSP is the real defence |

### Configuration
```c
typedef struct security_headers_config {
    const char *content_security_policy; /* NULL → "default-src 'self'" */
    const char *frame_options;           /* NULL → "DENY" */
    const char *referrer_policy;         /* NULL → "strict-origin-when-cross-origin" */
    const char *permissions_policy;      /* NULL → "geolocation=(), camera=(), microphone=()" */
    bool enable_hsts;                    /* Set Strict-Transport-Security */
    int  hsts_max_age;                   /* Seconds; 0 → 31536000 (1 year) */
    bool hsts_include_subdomains;        /* Add includeSubDomains */
} security_headers_config_t;
```

### Functions
```c
middleware_fn_t security_headers_middleware_create(
        const security_headers_config_t *config);
void security_headers_middleware_destroy(void);
```

### Example
```c
security_headers_config_t cfg = {0};
cfg.enable_hsts = true;
cfg.content_security_policy = "default-src 'self'; script-src 'self'";

middleware_fn_t sec = security_headers_middleware_create(&cfg);
router_use_middleware(router, sec);
// ... at shutdown:
security_headers_middleware_destroy();
```

Passing `NULL` gives you the safe defaults above. Turn on HSTS only once you are
genuinely serving over HTTPS everywhere — it is sticky in browsers, and a premature
`includeSubDomains` can take a subdomain offline for the length of `max_age`.
`security_headers_middleware_destroy()` is a no-op if the middleware was never
created.

---

## Security Utilities

Three primitives worth using instead of rolling your own. All are safe to call from
any thread.

```c
void secure_zero(void *ptr, size_t len);
// Wipes memory with zeros behind a compiler barrier, so the write cannot be
// optimised away. NULL-safe (no-op). Scrub secrets before freeing.

bool secure_compare(const void *a, const void *b, size_t len);
// Constant-time comparison: always reads all len bytes, so response time
// leaks nothing about where the first difference was.
// Returns false if either pointer is NULL.

int secure_random_bytes(void *buf, size_t len);
// Cryptographically secure bytes from /dev/urandom (POSIX) or
// BCryptGenRandom (Windows). buf must be non-NULL and len > 0.
// Returns: 0 on success, -1 on failure — always check it.
```

Use `secure_compare()` for anything an attacker submits and you compare against a
secret: tokens, MACs, password hashes. A plain `memcmp()` returns early on the first
differing byte, and that timing difference is measurable over enough requests.

---

## Environment Configuration

Typed accessors for environment variables — the mechanism GitHub Secrets, Docker,
Kubernetes, and CI systems all use. You choose the variable names; the library just
saves you the parsing and the fallback handling.

```c
const char *env_config_get(const char *key, const char *default_value);
int         env_config_get_int(const char *key, int default_value);
bool        env_config_get_bool(const char *key, bool default_value);
uint16_t    env_config_get_port(const char *key, uint16_t default_value);
const char *env_config_require(const char *key);   // NULL if unset or empty
bool        env_config_is_set(const char *key);
```

Each getter falls back to `default_value` when the variable is unset, empty, or
unparseable. Booleans accept `1`/`true`/`yes`/`on` and `0`/`false`/`no`/`off`,
case-insensitively. `env_config_get_port()` additionally rejects anything outside
0–65535. `env_config_require()` is the one to use for values that must be present —
it returns `NULL` so you can fail loudly at startup rather than half-configured.

```c
const char *db   = env_config_get("DB_URL", "postgres://localhost/mydb");
int         port = env_config_get_int("PORT", 8080);
bool        debug = env_config_get_bool("DEBUG", false);
const char *key  = env_config_require("API_KEY");   // NULL if missing
```

### Secure secret handling

For API keys, passwords, and tokens, prefer the secure-value API. It copies the
secret into a dedicated buffer, wipes that buffer on free, and gives you a redacted
form that is safe to log.

```c
typedef struct env_secure_value env_secure_value_t;

env_secure_value_t *env_config_get_secure(const char *key);
// Returns: opaque handle, or NULL if the variable is missing/empty

const char *env_secure_value_get(const env_secure_value_t *sv);  // NULL-safe
size_t      env_secure_value_len(const env_secure_value_t *sv);  // NULL-safe → 0
void        env_secure_value_free(env_secure_value_t *sv);       // wipes, then frees

char *env_config_redact(const char *value);
// Heap-allocated masked copy for logs; caller frees.
// Values of 4 chars or fewer become "****"; longer ones keep the first and
// last character (e.g. "s**********z"). NULL or empty in → NULL out.
```

```c
env_secure_value_t *key = env_config_get_secure("API_KEY");
if (!key) { fprintf(stderr, "API_KEY required\n"); exit(1); }
use_secret(env_secure_value_get(key));
env_secure_value_free(key);   // secret wiped from memory
```

This narrows the window in which plaintext secrets sit in process memory, which is
what you want if a core dump or a crash reporter ever gets written to disk.

### `http_server_apply_env()`
Apply the standard `WEBLIB_*` variables to a server in one call.
```c
int http_server_apply_env(http_server_t *server);
// Returns: 0 on success, -1 if server is NULL
```
Reads `WEBLIB_READ_TIMEOUT`, `WEBLIB_WRITE_TIMEOUT`, `WEBLIB_REQUEST_TIMEOUT`,
`WEBLIB_THREAD_COUNT`, `WEBLIB_MAX_CONNECTIONS`, and `WEBLIB_ASYNC_MODE`. Variables
that are unset are skipped rather than reset, so anything you configured in code
survives — which means you can call this after your own setup and let the environment
override only what it actually specifies.

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
void *handle = db_connection_get_handle(conn);
// ... use handle ...
db_pool_release(pool, conn);

db_pool_destroy(pool);
```

### Full API
```c
db_pool_config_t db_pool_config_default(db_type_t db_type, const char *connection_string);
db_pool_t *db_pool_create(const db_pool_config_t *config);
void db_pool_destroy(db_pool_t *pool);

db_connection_t *db_pool_acquire(db_pool_t *pool);
int db_pool_release(db_pool_t *pool, db_connection_t *conn);
int db_pool_get_stats(db_pool_t *pool, db_pool_stats_t *stats);
int db_pool_close_idle(db_pool_t *pool);

int   db_connection_execute(db_connection_t *conn, const char *query);
void *db_connection_get_handle(db_connection_t *conn);
bool  db_connection_is_valid(db_connection_t *conn);
```

`db_type_t` is one of `DB_TYPE_GENERIC`, `DB_TYPE_SQLITE`, `DB_TYPE_POSTGRESQL`,
`DB_TYPE_MYSQL`, or `DB_TYPE_CUSTOM`. For `DB_TYPE_CUSTOM` you supply the
`connect_fn` / `disconnect_fn` / `ping_fn` / `execute_fn` callbacks on the config.
`connection_string` is borrowed — the pool copies it and never writes to your buffer.

---

## Thread Pool & Server Hardening

### Timeouts
```c
int http_server_set_timeout(http_server_t *server, int read_sec, int write_sec);
// Per-recv socket timeouts. Default 30/30; 0 disables. Must precede listen().
// Returns: 0 on success, -1 on failure (NULL server or negative values)

int http_server_get_read_timeout(http_server_t *server);   // -1 if server is NULL
int http_server_get_write_timeout(http_server_t *server);  // -1 if server is NULL

int http_server_set_request_timeout(http_server_t *server, int sec);
// Total wall-clock deadline for reading one complete request.
// Default 60; 0 disables. Returns: 0 on success, -1 on failure

int http_server_get_request_timeout(http_server_t *server);  // -1 if server is NULL
```

The two timeouts do different jobs, and you want both. `set_timeout()` bounds a
single `recv()` — but a drip-feeding client resets it with every byte, so on its own
it does not stop a slow-loris. `set_request_timeout()` bounds the *whole* request and
does. The deadline is evaluated per request, so you can change it at runtime and it
takes effect on subsequent requests. A completely silent client is bounded even when
the read timeout is 0, because the server caps the effective `recv()` timeout at this
deadline; worst case, a connection is held for roughly the deadline plus one recv
interval. Threaded mode only. This is also the deadline that bounds the
[TLS handshake](#tls--https).

### Concurrency
```c
int http_server_set_thread_count(http_server_t *server, int count);
// Worker threads for threaded mode. Default 16, clamped to [1, 256].
// Must precede listen(). Returns: 0 on success, -1 on failure

int http_server_set_max_connections(http_server_t *server, int max_conn);
// Maximum simultaneous connections (must be >= 1)
// Returns: 0 on success, -1 on failure

int http_server_get_active_connections(http_server_t *server);
// Returns: current active connection count, or -1 on failure
```

### Lifecycle
```c
typedef enum {
    HTTP_SERVER_STOPPED  = 0,
    HTTP_SERVER_RUNNING  = 1,
    HTTP_SERVER_DRAINING = 2
} http_server_state_t;

int http_server_shutdown(http_server_t *server, int timeout_sec);
// Graceful: stop accepting, drain in-flight requests. 0 = drain immediately.
// Returns: 0 on success, -1 on failure

int http_server_get_state(http_server_t *server);
```

Server state transitions: `STOPPED → RUNNING → DRAINING → STOPPED`

---

## CSRF Middleware

```c
typedef struct csrf_config {
    const char *cookie_name; /* Cookie carrying the token (default: "csrf_token") */
    const char *header_name; /* Header the client must echo (default: "X-CSRF-Token") */
    int token_length;        /* Token bytes BEFORE hex-encoding (default: 16, min 8, max 32) */
} csrf_config_t;

middleware_fn_t csrf_middleware_create(const csrf_config_t *config);
void csrf_middleware_destroy(void);
```

The double-submit cookie pattern. On the first request, when no cookie is present, a
random token is generated and written to a cookie on the response. For state-changing
methods (POST, PUT, PATCH, DELETE) the token in the cookie must also appear in the
`X-CSRF-Token` request header — or whatever you set `header_name` to. The comparison
is constant-time, so it does not leak a partial match through response timing.

Safe methods (GET, HEAD, OPTIONS) pass through unconditionally.

Note that `token_length` counts raw bytes *before* hex encoding, so the default of 16
produces a 32-character token. Pass `NULL` for the config to take all defaults.

---

## Logging Middleware

```c
typedef enum {
    LOG_LEVEL_DEBUG = 0,
    LOG_LEVEL_INFO  = 1,
    LOG_LEVEL_WARN  = 2,
    LOG_LEVEL_ERROR = 3
} log_level_t;

typedef struct log_config {
    log_level_t level;  /* Minimum level to emit (default: LOG_LEVEL_INFO) */
    FILE *output;       /* Output stream; NULL defaults to stderr */
} log_config_t;

middleware_fn_t log_middleware_create(const log_config_t *config);
void log_middleware_destroy(void);
```

One line per request. Output format: `[YYYY-MM-DD HH:MM:SS] LEVEL  METHOD /path`.
Pass `NULL` for the config to take the defaults.

---

## Error Handler Middleware

```c
typedef void (*error_handler_fn_t)(http_request_t *req, http_response_t *res,
                                   http_status_t status);

typedef struct error_handler_config {
    error_handler_fn_t handler;  /* Custom handler; NULL → built-in JSON error body */
} error_handler_config_t;

middleware_fn_t error_handler_middleware_create(const error_handler_config_t *config);
void error_handler_middleware_destroy(void);
void error_handler_apply(http_request_t *req, http_response_t *res);
```

When the response status is 4xx or 5xx and no body has been set, a standard JSON
error body is filled in — `{"error":"Not Found","status":404}`. Pass `NULL` for the
config to get that built-in behaviour, or supply your own `handler` to shape the body
yourself.

The middleware covers errors produced *before* the route handler runs, such as a
status set by an earlier middleware. For an error your own handler produces, call
`error_handler_apply()` directly — `req` may be `NULL`, and the call is a no-op when
the status is below 400.

---

## Input Validation

```c
bool input_validate_length(const char *str, size_t min_len, size_t max_len);
// True if the length is within [min_len, max_len], inclusive

bool input_validate_charset(const char *str, const char *allowed_chars);
// True if every character of str appears in the allowed_chars set

bool input_validate_integer(const char *str, long long min_val, long long max_val,
                            long long *out_val);
// Parses a decimal integer and range-checks it. Rejects leading/trailing
// whitespace and any non-numeric character. If out_val is non-NULL and
// validation passes, it receives the parsed value.

bool input_validate_email(const char *str);
// Non-empty local part, '@', non-empty domain containing at least one '.'.
// Format only — no DNS lookup, no full RFC 5321 validation.

bool input_is_alphanumeric(const char *str);
// True if every character is [A-Za-z0-9]. An empty string is false.

char *input_sanitize_html(const char *str);
// Escapes & " ' < > to named HTML entities.
// Returns: newly allocated string (caller must free), or NULL on error
```

`input_validate_integer()` gives you the parsed value through `out_val`, so you
validate and convert in one step rather than re-parsing with `atoi()` afterwards —
which is where the range check usually gets quietly lost.

---

## Health Check

```c
int health_check_register(router_t *router);  // Registers GET /healthz
// Returns: 0 on success, -1 on failure

void health_check_handler(http_request_t *req, http_response_t *res);
// The handler itself, if you would rather register it on your own path
```

Response: `{"status":"ok","uptime_seconds":N}` — shaped for load-balancer probes,
Kubernetes liveness/readiness checks, and monitoring dashboards.

---

## In-Memory Cache

```c
typedef struct cache cache_t;

cache_t *cache_create(size_t max_entries);
void cache_destroy(cache_t *cache);

int cache_set(cache_t *cache, const char *key, const char *value, int ttl_seconds);
// Both key and value are copied internally. Returns: 0 on success, -1 on failure

const char *cache_get(cache_t *cache, const char *key);
// Returns: a NEWLY ALLOCATED copy of the value, or NULL if missing or expired.
// CALLER OWNS the result and must free it: free((void *)val);

int cache_delete(cache_t *cache, const char *key);  // 0 if deleted, -1 if not found
void cache_clear(cache_t *cache);
size_t cache_count(cache_t *cache);                 // Current number of entries
```

Values are NUL-terminated strings, not arbitrary binary blobs. LRU eviction kicks in
once `max_entries` is reached, and a `ttl_seconds` of 0 means no expiry. Expired
entries are removed lazily, on the `cache_get()` that finds them.

---

## Metrics Middleware

```c
middleware_fn_t metrics_middleware_create(void);
void metrics_middleware_destroy(void);

int metrics_register(router_t *router);   // Registers GET /metrics
// Returns: 0 on success, -1 on failure

void metrics_handler(http_request_t *req, http_response_t *res);
// The handler itself, if you would rather register it on your own path

void metrics_record_status(int status_code);
// Call after sending a response to fold it into the 2xx/3xx/4xx/5xx counters
```

`GET /metrics` returns JSON: total request count, a per-method breakdown, status code
ranges (2xx/3xx/4xx/5xx), and uptime.

---

## Response Compression

```c
void http_response_send_compressed(http_response_t *res, http_status_t status,
                                   const char *body, size_t body_len,
                                   const char *content_type,
                                   const char *accept_encoding);
// body_len of 0 means "use strlen". accept_encoding may be NULL.

const char *compression_negotiate(const char *accept_encoding);
// Returns: "gzip" if the client accepts gzip, NULL otherwise

bool compression_should_compress(const char *content_type, size_t content_length);
// True for text types, application/json, application/javascript and similar.
// False for images, audio, video, and payloads under 256 bytes.

uint32_t crc32_compute(const uint8_t *data, size_t len);  // CRC-32 per RFC 1952
```

Pure C gzip (RFC 1952) with DEFLATE (RFC 1951) — no zlib, no external dependency.

Compression is **not** applied automatically. Send a response through
`http_response_send_compressed()` and it decides for you: if the client's
`Accept-Encoding` offers gzip *and* the content type and size are worth compressing,
the body is compressed; otherwise it falls through to a plain
`http_response_send_text()`. `compression_negotiate()` and
`compression_should_compress()` are the two halves of that decision, exposed in case
you want to make it yourself.

```c
void handle_page(http_request_t *req, http_response_t *res) {
    const char *html = "<html><body>...</body></html>";
    const char *ae = http_request_get_header(req, "Accept-Encoding");

    /* body_len 0 → strlen(html). ae may be NULL; the call handles that. */
    http_response_send_compressed(res, HTTP_OK, html, 0, "text/html", ae);
}
```

---

## Benchmarking

### Statistics
```c
typedef struct {
    uint64_t total_requests;     /* Total requests executed */
    uint64_t successful;         /* 2xx responses */
    uint64_t failed;             /* Non-2xx or connection error */
    double elapsed_seconds;      /* Total wall-clock time */
    double requests_per_second;  /* Throughput */
    double avg_latency_us;       /* Average latency, microseconds */
    double min_latency_us;
    double max_latency_us;
    double p50_latency_us;       /* Median */
    double p95_latency_us;
    double p99_latency_us;
} benchmark_stats_t;
```

### Functions
```c
int benchmark_run(uint16_t port, const char *path,
                  uint64_t num_requests, benchmark_stats_t *stats);
// Sends num_requests SEQUENTIAL GET requests to http://127.0.0.1:<port><path>
// and collects latency samples. stats must not be NULL.
// Returns: 0 on success, -1 on failure

void benchmark_print(FILE *fp, const benchmark_stats_t *stats);
// Print the results to a stream, e.g. stdout

uint64_t benchmark_timestamp_us(void);
// High-resolution timestamp from CLOCK_MONOTONIC, microseconds since an
// arbitrary epoch. Useful for timing your own code.
```

### Example
```c
benchmark_stats_t stats;
if (benchmark_run(8080, "/", 10000, &stats) == 0) {
    benchmark_print(stdout, &stats);
}
```

Requests are sent **sequentially**, against loopback only. That makes the latency
percentiles a clean measure of per-request server cost, but it means these numbers
are not a concurrency or saturation benchmark — reach for a dedicated load generator
when that is the question you are asking.

---

## Cloudflare Workers & WebAssembly

The library also builds for WebAssembly and for Cloudflare Workers, where the router,
JSON, template, and validation code runs unchanged and the Worker runtime supplies
the I/O.

**The Worker API has its own full reference: [`docs/WORKER_API.md`](../WORKER_API.md)**
— request/response objects, environment bindings, the fetch handler, and the KV, R2,
D1, and Queues APIs, with `wrangler.toml` mapping. The summary below is orientation
only; go there for the signatures.

### Bindings
```c
typedef enum {
    WORKER_BINDING_KV,       /* KV Namespace */
    WORKER_BINDING_R2,       /* R2 Object Storage */
    WORKER_BINDING_D1,       /* D1 SQL Database */
    WORKER_BINDING_QUEUE,    /* Queue (producer/consumer) */
    WORKER_BINDING_SECRET,   /* Secret / env variable */
    WORKER_BINDING_SERVICE   /* Service binding (Worker-to-Worker) */
} worker_binding_type_t;
```

### Entry point
```c
typedef worker_response_t *(*worker_fetch_handler_t)(worker_request_t *req,
                                                     worker_env_t *env);

void worker_set_fetch_handler(worker_fetch_handler_t handler);
void worker_set_router(router_t *router);

WASM_EXPORT
worker_response_t *worker_handle_fetch(worker_request_t *req, worker_env_t *env);
```

Set a custom fetch handler and it is called for every request. `worker_set_router()`
is accepted but **not yet used for dispatch**: with only a router set,
`worker_handle_fetch()` returns a 200 placeholder (`X-Worker-Routed: configured`,
body `Router configured; native worker simulation does not perform route matching`)
without matching any route; with neither handler nor router it returns 503.

The KV, R2, D1, and Queue handles are backed by in-memory implementations in **every**
build — native, test, and WASM/Workers alike — not by Cloudflare's services. Reaching
the real bindings would need a JS glue layer, and none ships here: `examples/worker.js`
accepts `env` but never passes it into WASM, and no `wrangler.toml` ships. That is what
lets you exercise Worker code from the ordinary native test suite — but it is not a
local-only convenience: a deployed Worker gets the same in-memory arrays, so their fixed
capacities are real limits. See [Cloudflare Workers API](../WORKER_API.md).

### WebAssembly runtime
```c
WASM_EXPORT const char *wasm_weblib_version(void);
WASM_EXPORT const char *wasm_weblib_capabilities(void);
WASM_EXPORT bool wasm_weblib_has_capability(const char *name);
```

`wasm_weblib_capabilities()` returns a versioned identifier with a parenthesised
capability list, so JavaScript can feature-detect rather than assume. Thin
`WASM_EXPORT` wrappers are provided for the JSON API (`wasm_json_*`), the router
(`wasm_router_*`), input validation (`wasm_validate_*`), and the template engine
(`wasm_template_*`).

```c
WASM_EXPORT void wasm_free(void *ptr);
```

Strings returned across the WASM boundary — from `wasm_json_stringify()`,
`wasm_template_render()` and friends — must be released with `wasm_free()` from the
JavaScript side. Forgetting this leaks linear memory that nothing else will reclaim.

> TLS is **not** available in WebAssembly or Cloudflare Workers builds. Those
> platforms terminate TLS for you.

---

## Status Codes

### Methods
```c
typedef enum {
    HTTP_GET, HTTP_POST, HTTP_PUT, HTTP_DELETE,
    HTTP_PATCH, HTTP_HEAD, HTTP_OPTIONS
} http_method_t;
```

### Statuses
```c
HTTP_SWITCHING_PROTOCOLS     = 101
HTTP_OK                      = 200
HTTP_CREATED                 = 201
HTTP_ACCEPTED                = 202
HTTP_NO_CONTENT              = 204
HTTP_NOT_MODIFIED            = 304
HTTP_BAD_REQUEST             = 400
HTTP_UNAUTHORIZED            = 401
HTTP_FORBIDDEN               = 403
HTTP_NOT_FOUND               = 404
HTTP_METHOD_NOT_ALLOWED      = 405
HTTP_REQUEST_TIMEOUT         = 408
HTTP_PAYLOAD_TOO_LARGE       = 413
HTTP_URI_TOO_LONG            = 414
HTTP_TOO_MANY_REQUESTS       = 429
HTTP_HEADER_FIELDS_TOO_LARGE = 431
HTTP_INTERNAL_ERROR          = 500
HTTP_NOT_IMPLEMENTED         = 501
HTTP_BAD_GATEWAY             = 502
HTTP_SERVICE_UNAVAILABLE     = 503
```

---

*Modern C Web Library v2.0.0 — Pure C, Zero Dependencies*
