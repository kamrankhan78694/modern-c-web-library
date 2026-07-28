# Getting Started with Modern C Web Library

**Version 2.1.0**

Welcome to the Modern C Web Library! This tutorial walks you from an empty directory
to a running HTTP server, a JSON API, middleware, and — at the end — an experimental
HTTPS server that speaks TLS 1.3 without linking OpenSSL.

## Introduction

The library is a pure C web framework for building HTTP and WebSocket servers. It links
no external libraries: everything (routing, JSON, WebSocket framing, and the optional
TLS 1.3 layer) is written from scratch in this repository.

What you get:
- **Pure C** — no third-party dependencies, only the C standard library and OS sockets
- **One header** — everything public lives in `include/kamran.k`
- **Cross-platform** — Linux and macOS are built and tested in CI on every push; a Windows
  build path exists (it links `ws2_32`/`bcrypt` instead of pthreads) but CI does not
  currently exercise it. There are also WebAssembly (Emscripten) and Cloudflare Workers
  build targets
- **Batteries included** — routing with parameters, middleware, JSON, sessions, templates,
  cookies, compression, health checks, metrics
- **Optional experimental TLS 1.3** — off by default, native builds only (see
  [Serving HTTPS](#serving-https-experimental-tls-13) below)

## Prerequisites

Before you begin, make sure you have:

- **C Compiler** — GCC 7+, Clang 5+, or MSVC 2017+ (the build asks for C11)
- **CMake** — the project itself asks for 3.10 or higher, but the `cmake -S . -B build`
  form used throughout this tutorial needs **3.13 or higher**
- **Git** — for cloning the repository
- **Basic C knowledge** — you should be comfortable with pointers and manual cleanup
- **OpenSSL command-line tool** — only for the optional HTTPS section, to make a test
  certificate and to act as a TLS client

## Installation

### Step 1: Clone the Repository

```bash
git clone https://github.com/kamrankhan78694/modern-c-web-library.git
cd modern-c-web-library
```

### Step 2: Build the Library

```bash
cmake -S . -B build
cmake --build build --parallel
```

This produces:

- `build/libweblib.a` — the static library
- `build/libweblib_shared.so` (`.dylib` on macOS) — the shared library
- `build/examples/` — the example programs (`simple_server`, `rest_api_server`,
  `websocket_echo_server`, and friends)
- `build/tests/` — the test binaries

### Step 3: Run the Test Suite

```bash
(cd build && ctest --output-on-failure)
```

That is the form CI runs, and it works with any CMake. If you have CMake 3.20 or newer you
can stay put and use `ctest --test-dir build --output-on-failure` instead.

A default build registers six ctest suites — `WebLibTests`, `KamranHeaderTests`,
`AsyncWebSocketTests`, `StressTests`, `WorkerTests`, `WasmTests` — and they should all
pass. Enabling the experimental TLS layer brings the total to thirteen; see
[Serving HTTPS](#serving-https-experimental-tls-13).

### Step 4: Install (Optional)

```bash
sudo cmake --install build
```

This copies the libraries to `lib/`, the headers (`kamran.k` and `db_pool.h`) to
`include/`, and the non-TLS example servers to `bin/`.

Note: the install step does **not** export a CMake package config, so `find_package(weblib)`
will not find anything. Link against the library directly, as shown in
[Compile and Run](#compile-and-run) below.

## Your First Server

Everything public lives in a single header, `kamran.k`. There is no `mcwl/` include
directory and no per-module headers — one `#include "kamran.k"` gives you the whole API.

Create a new file called `hello_server.c`:

```c
#include "kamran.k"
#include <stdio.h>
#include <signal.h>
#include <unistd.h>

static http_server_t *g_server = NULL;
static volatile sig_atomic_t shutdown_requested = 0;

static void handle_sigint(int sig) {
    (void)sig;
    shutdown_requested = 1;   /* keep the handler async-signal-safe */
}

static void hello_handler(http_request_t *req, http_response_t *res) {
    (void)req;  /* unused */
    http_response_send_text(res, HTTP_OK, "Hello, World!");
}

int main(void) {
    router_t *router;

    signal(SIGINT, handle_sigint);

    /* Create server and router */
    g_server = http_server_create();
    router = router_create();
    if (!g_server || !router) {
        fprintf(stderr, "Failed to allocate server/router\n");
        return 1;
    }

    /* Add a route. The method is an http_method_t enum value, not a string. */
    router_add_route(router, HTTP_GET, "/", hello_handler);

    /* Attach the router to the server */
    http_server_set_router(g_server, router);

    /* Start listening. This returns as soon as the listener is up. */
    if (http_server_listen(g_server, 8080) < 0) {
        fprintf(stderr, "Failed to listen on port 8080\n");
        router_destroy(router);
        http_server_destroy(g_server);
        return 1;
    }
    printf("Server running on http://localhost:8080/ (Ctrl+C to stop)\n");

    while (!shutdown_requested) {
        sleep(1);
    }

    /* Cleanup */
    http_server_stop(g_server);
    http_server_destroy(g_server);
    router_destroy(router);
    return 0;
}
```

Two things that trip people up:

- **`http_server_listen()` does not block in the default threaded mode.** It binds,
  listens, starts the accept thread, and returns `0` (or `-1` on failure). Your `main`
  has to stay alive on its own — hence the `while (!shutdown_requested) sleep(1);` loop.
  Every threaded-mode example in [`examples/`](../../examples/) uses this shape. (In async mode it is
  the other way round: the call runs the event loop on your thread and only returns once
  the server is stopped.)
- **Routes take an enum, not a string.** `HTTP_GET`, `HTTP_POST`, `HTTP_PUT`,
  `HTTP_DELETE`, `HTTP_PATCH`, `HTTP_HEAD`, `HTTP_OPTIONS`.

### Compile and Run

Point the compiler at the repository's `include/` directory and link `libweblib.a`:

```bash
cc -std=c11 -I /path/to/modern-c-web-library/include \
   hello_server.c -o hello_server \
   -L /path/to/modern-c-web-library/build -lweblib -lpthread
./hello_server
```

On Windows, link `ws2_32` and `bcrypt` instead of `pthread`.

Visit `http://localhost:8080` in your browser or use curl:

```bash
curl http://localhost:8080
# Output: Hello, World!
```

## Adding Routes

Let's add more routes, including one with a path parameter. A path segment starting with
`:` becomes a named parameter you read back with `http_request_get_param()`.

Keep the `main()` shape from above and swap in these handlers and registrations:

```c
static void home_handler(http_request_t *req, http_response_t *res) {
    (void)req;
    http_response_send_text(res, HTTP_OK, "Welcome!");
}

static void about_handler(http_request_t *req, http_response_t *res) {
    (void)req;
    http_response_send_text(res, HTTP_OK, "Modern C Web Library v" WEBLIB_VERSION);
}

static void user_handler(http_request_t *req, http_response_t *res) {
    const char *user_id = http_request_get_param(req, "id");

    if (user_id) {
        char message[256];
        snprintf(message, sizeof(message), "User ID: %s", user_id);
        http_response_send_text(res, HTTP_OK, message);
    } else {
        http_response_send_text(res, HTTP_BAD_REQUEST, "Missing user ID");
    }
}

/* ... inside main(), after router_create(): */
router_add_route(router, HTTP_GET, "/", home_handler);
router_add_route(router, HTTP_GET, "/about", about_handler);
router_add_route(router, HTTP_GET, "/users/:id", user_handler);
```

`WEBLIB_VERSION` is a macro in `kamran.k` holding the library version as a string
(`"2.0.0"` today), so the `/about` route can never drift from the library it was built
against. If you need it at runtime instead, call `weblib_version()`.

### Test the Routes

```bash
curl http://localhost:8080/
# Output: Welcome!

curl http://localhost:8080/about
# Output: Modern C Web Library v2.0.0

curl http://localhost:8080/users/42
# Output: User ID: 42
```

## JSON Responses

The library ships its own JSON parser and builder — no external library. Every JSON
node is a `json_value_t *`, and freeing the root frees the whole tree:

```c
static void api_status_handler(http_request_t *req, http_response_t *res) {
    json_value_t *json;

    (void)req;

    /* Build a JSON object */
    json = json_object_create();
    json_object_set(json, "status", json_string_create("ok"));
    json_object_set(json, "version", json_string_create(weblib_version()));
    json_object_set(json, "uptime", json_number_create(12345));

    /* Send it */
    http_response_send_json(res, HTTP_OK, json);

    /* Free the root; children are freed with it */
    json_value_free(json);
}

static void api_user_handler(http_request_t *req, http_response_t *res) {
    const char *user_id = http_request_get_param(req, "id");
    json_value_t *json = json_object_create();

    json_object_set(json, "id", json_string_create(user_id ? user_id : "unknown"));
    json_object_set(json, "name", json_string_create("John Doe"));
    json_object_set(json, "email", json_string_create("john@example.com"));
    json_object_set(json, "active", json_bool_create(true));

    http_response_send_json(res, HTTP_OK, json);
    json_value_free(json);
}

/* ... inside main(): */
router_add_route(router, HTTP_GET, "/api/status", api_status_handler);
router_add_route(router, HTTP_GET, "/api/users/:id", api_user_handler);
```

The constructors are `json_object_create()`, `json_array_create()`,
`json_string_create()`, `json_number_create()` (a `double`), `json_bool_create()` and
`json_null_create()`. Note the boolean one is `json_bool_create`, not `json_boolean_create`.

To read JSON out of a request body, use `json_parse(req->body)` and then
`json_object_get()` / `json_array_get()`; the [REST API tutorial](rest-api.md) does this
in full.

### Test JSON Endpoints

```bash
curl http://localhost:8080/api/status
# Output: {"uptime":12345,"version":"2.0.0","status":"ok"}

curl http://localhost:8080/api/users/123
# Output: {"active":true,"email":"john@example.com","name":"John Doe","id":"123"}
```

Key order is worth knowing about: `json_object_set()` pushes each new key onto the front
of the object's list, so the serialized output lists keys in reverse insertion order.
JSON objects are unordered by definition and every client parses this fine, but do not
write a test that string-compares against a hand-typed object.

## Adding Middleware

Middleware runs before your route handlers — good for logging, authentication, CORS, or
request validation. A middleware function has this signature:

```c
bool my_middleware(http_request_t *req, http_response_t *res, void *user_data);
```

Return `true` to continue down the chain, `false` to stop. Middleware runs in the order
you register it, and the router also stops if a middleware has already sent a response.

Middleware is registered **on the router and applies to every route**. There is no
per-route middleware registration function, so a middleware that should only guard some
paths has to check `req->path` itself.

```c
#include "kamran.k"
#include <stdio.h>
#include <string.h>
#include <time.h>

/* Logging middleware. There are no getters for the method and path — read them
 * straight off the request struct. */
static bool logging_middleware(http_request_t *req, http_response_t *res, void *user_data) {
    static const char *names[] = { "GET", "POST", "PUT", "DELETE", "PATCH", "HEAD", "OPTIONS" };
    time_t now = time(NULL);
    char stamp[32];

    (void)res;
    (void)user_data;

    strftime(stamp, sizeof(stamp), "%Y-%m-%dT%H:%M:%S", gmtime(&now));
    printf("[%s] %s %s\n", stamp, names[req->method], req->path);

    return true;  /* continue to the next middleware/handler */
}

/* Authentication middleware guarding one path prefix. */
static bool auth_middleware(http_request_t *req, http_response_t *res, void *user_data) {
    const char *auth_header;

    (void)user_data;

    if (strncmp(req->path, "/protected", 10) != 0) {
        return true;  /* not our business */
    }

    auth_header = http_request_get_header(req, "Authorization");
    if (!auth_header || strcmp(auth_header, "Bearer secret-token") != 0) {
        http_response_send_text(res, HTTP_UNAUTHORIZED, "Unauthorized");
        return false;  /* stop; the response is already written */
    }

    return true;
}

static void protected_handler(http_request_t *req, http_response_t *res) {
    (void)req;
    http_response_send_text(res, HTTP_OK, "This is a protected resource!");
}

static void public_handler(http_request_t *req, http_response_t *res) {
    (void)req;
    http_response_send_text(res, HTTP_OK, "This is a public resource!");
}

/* ... inside main(), after router_create(): */
router_use_middleware(router, logging_middleware);
router_use_middleware(router, auth_middleware);

router_add_route(router, HTTP_GET, "/public", public_handler);
router_add_route(router, HTTP_GET, "/protected", protected_handler);
```

If a middleware needs configuration, register it with
`router_use_middleware_with_data(router, fn, &config)` — the pointer comes back as the
`user_data` argument, so you can run several instances of the same middleware with
different settings.

### Built-in Middleware

You do not have to write the common ones yourself. `kamran.k` exposes factories that
return a `middleware_fn_t`, each with a matching teardown call:

```c
cors_options_t cors_cfg = {0};
cors_cfg.allowed_origins = NULL;             /* NULL means allow all origins */
cors_cfg.allowed_methods = "GET,POST,PUT,DELETE,OPTIONS";
cors_cfg.allowed_headers = "Content-Type,Authorization";
cors_cfg.max_age = 86400;

middleware_fn_t cors = cors_middleware_create(&cors_cfg);
if (cors) {
    router_use_middleware(router, cors);
}

/* ... on shutdown, after router_destroy(): */
cors_middleware_destroy();
```

The same shape works for `log_middleware_create()`, `ratelimit_middleware_create()`,
`error_handler_middleware_create()`, `metrics_middleware_create()`,
`csrf_middleware_create()`, `security_headers_middleware_create()` and the auth
middlewares. `examples/rest_api_server.c` wires up a full stack this way.

Two ready-made endpoints are one call each: `health_check_register(router)` adds
`GET /healthz`, and `metrics_register(router)` adds `GET /metrics`.

`metrics_register()` also installs the response hook that populates the counters and
allocates the counter state, so it is complete on its own — you do not need
`metrics_middleware_create()` as well. Because it allocates, pair it with
`metrics_middleware_destroy()` at shutdown.

### Test Middleware

```bash
# Public route - no authentication needed
curl http://localhost:8080/public
# Output: This is a public resource!

# Protected route - without authentication
curl http://localhost:8080/protected
# Output: Unauthorized

# Protected route - with authentication
curl -H "Authorization: Bearer secret-token" http://localhost:8080/protected
# Output: This is a protected resource!
```

## Running and Testing

### Build Your Application

```bash
cc -std=c11 -I /path/to/modern-c-web-library/include \
   myapp.c -o myapp \
   -L /path/to/modern-c-web-library/build -lweblib -lpthread
```

### Run the Server

```bash
./myapp
```

### Test with curl

```bash
# Basic GET request
curl http://localhost:8080/

# GET with route parameters
curl http://localhost:8080/users/42

# POST request with JSON
curl -X POST http://localhost:8080/api/users \
  -H "Content-Type: application/json" \
  -d '{"name":"Alice","email":"alice@example.com"}'

# Custom headers
curl -H "Authorization: Bearer secret-token" http://localhost:8080/protected
```

### Tuning the Server

There is no global debug switch. What you can set, all before `http_server_listen()`:

```c
http_server_set_timeout(g_server, 30, 30);        /* per-recv/send socket timeouts, seconds */
http_server_set_request_timeout(g_server, 60);    /* whole-request deadline (slow-loris guard) */
http_server_set_thread_count(g_server, 16);       /* worker threads, clamped to [1, 256] */
http_server_set_max_connections(g_server, 1000);  /* simultaneous connection cap (default 128) */
```

`http_server_apply_env(g_server)` applies the same knobs from the environment
(`WEBLIB_READ_TIMEOUT`, `WEBLIB_WRITE_TIMEOUT`, `WEBLIB_THREAD_COUNT`,
`WEBLIB_MAX_CONNECTIONS`, `WEBLIB_ASYNC_MODE`); variables you have not set are left alone.

For request logging, register `log_middleware_create()` with a `log_config_t` — set
`.level` to `LOG_LEVEL_DEBUG` while developing and point `.output` at `stderr`.

## Serving HTTPS (experimental TLS 1.3)

The library can terminate HTTPS itself, using a TLS 1.3 server written from scratch in
this repository — about 5,500 lines under `src/tls/`, with no OpenSSL, no mbedTLS, and no
new link dependency.

**Read this before you go further.** The TLS layer is **experimental and unaudited**. The
cryptographic primitives and, more importantly, the handshake state machine are
hand-written and have never been reviewed by an outside cryptographer. It passes RFC
known-answer tests for every primitive, a deterministic fuzzer over the untrusted-input
path, and a real `openssl s_client` interoperability test in CI — but none of that is a
security audit, and a TLS bug is usually invisible until it is exploited. Concretely: use
it to learn, to test locally, and on traffic you would be willing to send in the clear.
Do **not** put it in front of real users, real credentials, or real user data without an
external cryptographic audit. For production today, terminate TLS in a reverse proxy
(nginx, Caddy, a load balancer) and let this server speak plain HTTP behind it.

The profile is deliberately narrow — one cipher suite
(`TLS_CHACHA20_POLY1305_SHA256`), one key exchange group (X25519), one signature
algorithm (Ed25519). There is no cipher agility at all: a client that cannot offer all
three is refused with `handshake_failure`. That is a design choice, not an oversight —
these three are constant-time by construction and need no big-integer arithmetic, which
is the only responsible basis for hand-rolled crypto.

### Step 1: Build with TLS enabled

TLS is **off by default**. With the option off, no file under `src/tls/` is compiled and
the build is byte-identical to a build with no TLS code in the tree at all. Turn it on
with a fresh configure:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo -DWEBLIB_ENABLE_TLS=ON
cmake --build build --parallel
cd build && ctest --output-on-failure
```

On top of the six suites a default build registers, this adds `TlsTests`,
`TlsCryptoTests`, `TlsParseTests`, `TlsTransportTests`, `TlsFuzzTests` and
`TlsInteropOpenssl`. Add `-DWEBLIB_TLS_TEST_HOOKS=ON` as well and you also get
`TlsHttpTests`, for thirteen suites in total — all passing:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo \
      -DWEBLIB_ENABLE_TLS=ON -DWEBLIB_TLS_TEST_HOOKS=ON
```

`WEBLIB_TLS_TEST_HOOKS` exposes a deterministic-RNG seam so the end-to-end HTTPS test can
replay a fixed handshake. It is off by default and must never be on in a build you deploy
— a predictable RNG would destroy TLS entirely.

`TlsInteropOpenssl` is the one to watch: it runs the real `openssl s_client` against the
example server and checks a full TLS 1.3 handshake, a >16 KiB response spanning multiple
TLS records, and two requests over one keep-alive connection. It skips itself gracefully
if your `openssl` is too old for `-tls1_3` or lacks Ed25519.

### Step 2: Generate an Ed25519 test certificate

Ed25519 is the only key type this server accepts, so the usual RSA one-liner will not
work. Generate the key and a self-signed certificate:

```bash
openssl genpkey -algorithm ed25519 -out key.pem
openssl req -x509 -new -key key.pem -out cert.pem -days 365 -subj "/CN=localhost"
```

The private key must be a PEM `PRIVATE KEY` block (PKCS#8), which is what `genpkey`
produces. Keep both files out of version control.

### Step 3: Enable TLS on the server

`http_server_enable_tls()` takes PEM **buffers with explicit lengths** — not file paths.
Reading the files is your job, which is what the `read_file()` helper below does:

```c
#include "kamran.k"
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>

static http_server_t *g_server = NULL;
static volatile sig_atomic_t shutdown_requested = 0;

static void handle_sigint(int sig) {
    (void)sig;
    shutdown_requested = 1;
}

static void hello_handler(http_request_t *req, http_response_t *res) {
    (void)req;
    http_response_send_text(res, HTTP_OK, "Hello over pure-C TLS 1.3!\n");
}

/* Read a whole file into a NUL-terminated buffer; caller frees. */
static char *read_file(const char *path, size_t *len_out) {
    FILE *f = fopen(path, "rb");
    long sz;
    char *buf;
    size_t got;

    if (!f) {
        return NULL;
    }
    if (fseek(f, 0, SEEK_END) != 0 || (sz = ftell(f)) < 0) {
        fclose(f);
        return NULL;
    }
    rewind(f);
    buf = (char *)malloc((size_t)sz + 1);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    got = fread(buf, 1, (size_t)sz, f);
    buf[got] = '\0';
    *len_out = got;
    fclose(f);
    return buf;
}

int main(void) {
    char *cert, *key;
    size_t cert_len = 0, key_len = 0;
    router_t *router;

    signal(SIGINT, handle_sigint);

    cert = read_file("cert.pem", &cert_len);
    key = read_file("key.pem", &key_len);
    if (!cert || !key) {
        fprintf(stderr, "Could not read cert.pem / key.pem\n");
        free(cert);
        free(key);
        return 1;
    }

    g_server = http_server_create();
    router = router_create();
    if (!g_server || !router) {
        fprintf(stderr, "Failed to allocate server/router\n");
        free(cert);
        free(key);
        return 1;
    }

    router_add_route(router, HTTP_GET, "/", hello_handler);
    http_server_set_router(g_server, router);

    /* PEM buffers plus their lengths. Must be called before http_server_listen().
     * Returns -1 on NULL/empty arguments, malformed PEM, async mode enabled, TLS
     * already enabled, or a server that is already listening. In a build without
     * -DWEBLIB_ENABLE_TLS the function still exists and always returns -1. */
    if (http_server_enable_tls(g_server, cert, cert_len, key, key_len) != 0) {
        fprintf(stderr, "http_server_enable_tls failed - is this a -DWEBLIB_ENABLE_TLS=ON "
                        "build, and are cert.pem/key.pem a valid Ed25519 pair?\n");
        free(cert);
        free(key);
        router_destroy(router);
        http_server_destroy(g_server);
        return 1;
    }

    /* enable_tls copied what it needs; the PEM buffers can go now. */
    free(cert);
    free(key);

    if (http_server_listen(g_server, 8443) < 0) {
        fprintf(stderr, "Failed to listen on port 8443\n");
        router_destroy(router);
        http_server_destroy(g_server);
        return 1;
    }
    printf("HTTPS (EXPERIMENTAL) on https://localhost:8443/ (Ctrl+C to stop)\n");

    while (!shutdown_requested) {
        sleep(1);
    }

    http_server_stop(g_server);
    http_server_destroy(g_server);
    router_destroy(router);
    return 0;
}
```

The exact signature, declared in `include/kamran.k`:

```c
int http_server_enable_tls(http_server_t *server,
                           const char *cert_pem, size_t cert_len,
                           const char *key_pem,  size_t key_len);
```

Five arguments. Any three-argument, path-taking version you may have seen elsewhere does
not exist. Always check the return value — a `-1` you ignore leaves you serving plain
HTTP on a port everyone will connect to with `https://`.

The handshake runs on accept, before any HTTP is spoken, and it is bounded by the same
wall-clock deadline as reading a request (`http_server_set_request_timeout()`, 60 seconds
by default), so a slow-drip client cannot pin a worker thread during the handshake.

### Step 4: Verify it

You can build and run the shipped example instead of writing your own — it serves `/`,
`/hello` and `/big`, and defaults to port 8443:

```bash
./build/examples/tls_server cert.pem key.pem 8443
```

Then, from another terminal, do a real TLS 1.3 request with `openssl s_client`:

```bash
printf 'GET / HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n' \
  | openssl s_client -quiet -connect 127.0.0.1:8443 -tls1_3
```

You should see a couple of verification lines (`depth=0 CN=...` and `verify error:num=18:self-signed certificate`, both expected for a self-signed cert), then a normal HTTP response body. `-quiet` suppresses OpenSSL's session and certificate output; to inspect the certificate the server actually sent, replace `-quiet` with `-ign_eof` — note that simply deleting `-quiet` is not enough, because `-quiet` implies `-ign_eof`, and without it s_client closes the connection at stdin EOF before the response arrives. The `/big` route returns a 40,000-byte body, which is a useful check that responses larger than one TLS record's 16 KiB plaintext limit are fragmented correctly.

### What you cannot do yet

- **No browser page-load.** This is not a handshake bug — the certificate profile is
  Ed25519-only, and Ed25519 *server certificates* have limited and inconsistent support
  across browsers and versions. Do not plan on opening this in Chrome, Firefox or Safari.
- **No async mode.** `http_server_enable_tls()` returns -1 if you have called
  `http_server_set_async(server, true)`. TLS works in the default threaded mode only.
- **No WebSockets over TLS.** If a client sends an upgrade request on a TLS connection,
  the server refuses it with HTTP 503 and the message `WebSocket over TLS not supported`,
  rather than speaking plaintext WebSocket frames inside the TLS session. See the
  [WebSocket tutorial](websocket.md) for the details.
- **Native builds only.** WebAssembly and Cloudflare Workers builds ignore the option
  entirely — on those targets the browser or the edge terminates TLS for you.
- **No client-certificate authentication, no SNI-based certificate selection, no session
  resumption, no 0-RTT, no KeyUpdate.** One certificate, one full handshake per
  connection.
- **No TLS 1.2, no AES-GCM, no RSA or ECDSA.** One profile, no negotiation.

### Where to read more

[`src/tls/README.md`](../../src/tls/README.md) is the authoritative document for this
layer: what is implemented, the exact security scope, the interoperability matrix, and
the deliberate omissions (such as not emitting a middlebox-compatibility
ChangeCipherSpec). [`examples/tls_server.c`](../../examples/tls_server.c) is the working
program the CI interop test drives.

## Next Steps

You now have a server that routes, returns JSON, runs middleware, and — if you followed
the last section — speaks TLS 1.3. Where to go next:

### Tutorials
- **[Building a REST API](rest-api.md)** — a full CRUD API with JSON and a production middleware stack
- **[WebSocket Applications](websocket.md)** — real-time, bidirectional connections

### Documentation
- **[API Reference](../api/README.md)** — the full public API
- **[Examples](../../examples/)** — every pattern in this tutorial as a compiling program
- **[Deployment Guide](../DEPLOYMENT.md)** — running this in production
- **[TLS layer notes](../../src/tls/README.md)** — scope, security limits, and interop status of the experimental TLS 1.3 code

### Community
- **GitHub Issues** — report bugs or request features
- **Discussions** — ask questions and share what you built
- **[CONTRIBUTING.md](../../CONTRIBUTING.md)** — how to send a change
