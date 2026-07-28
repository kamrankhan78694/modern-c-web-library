# Real-Time Applications with WebSocket

**Version 2.0.1**

## Introduction

WebSockets provide a full-duplex communication channel over a single TCP connection, enabling real-time, bidirectional data exchange between clients and servers. Unlike traditional HTTP request-response cycles, WebSockets maintain a persistent connection that allows both the client and server to send messages at any time.

The Modern C Web Library implements WebSocket support based on **RFC 6455**, the WebSocket Protocol standard. This enables you to build:

- **Chat applications** — real-time messaging between users
- **Live dashboards** — streaming metrics and updates
- **Collaborative tools** — multi-user editing and synchronization
- **Gaming servers** — low-latency game state synchronization
- **IoT data streams** — continuous sensor data transmission
- **Notification systems** — instant alerts and updates

WebSockets start as regular HTTP connections and then "upgrade" to the WebSocket protocol through a handshake process. Once upgraded, the connection remains open for efficient, low-overhead communication.

## How WebSocket Works in the Library

The WebSocket workflow in the Modern C Web Library follows these steps:

1. **HTTP Upgrade Request** — client sends an HTTP request with an `Upgrade: websocket` header
2. **Server Handshake** — your route handler calls `websocket_handle_upgrade()`, which
   validates the request and fills in the `101 Switching Protocols` response
3. **Protocol Switch** — the server sends that response and takes the connection over
4. **Persistent Connection** — both sides can now send and receive frames freely
5. **Graceful Closure** — either side can close with an RFC 6455 status code

The important thing to understand is **how little you have to do**. You do not create the
connection object, you do not send the 101 response, and you do not run a read loop. Your
route handler does exactly two things:

1. Call `websocket_handle_upgrade(req, res)`. It checks `Upgrade`, `Connection`,
   `Sec-WebSocket-Key` and `Sec-WebSocket-Version`, computes `Sec-WebSocket-Accept`, sets
   the response status to 101 with the right headers, and returns `bool` — `true` if the
   request was a valid upgrade. It does **not** return a file descriptor.
2. Point `req->user_data` at a callbacks struct.

When your handler returns and the server sees status 101, it sends the response, creates
the `websocket_connection_t` for you, installs your callbacks, and runs the frame loop
until the connection closes.

The callbacks struct is not a named public type — you declare it yourself with exactly
these four members, in this order:

```c
typedef struct {
    websocket_message_cb_t on_message;
    websocket_close_cb_t   on_close;
    websocket_error_cb_t   on_error;
    void                  *user_data;
} websocket_callbacks_t;
```

It must outlive the request, so make it `static` (or heap-allocate it). The three
callback signatures, from `kamran.k`:

```c
void on_message(websocket_connection_t *conn, ws_message_type_t type,
                const void *data, size_t len);
void on_close(websocket_connection_t *conn, uint16_t code);      /* no reason argument */
void on_error(websocket_connection_t *conn, const char *error);
```

`ws_message_type_t` is `WS_MESSAGE_TEXT` or `WS_MESSAGE_BINARY`.

## Basic Echo Server

Here is a complete echo server. This is `examples/websocket_echo_server.c` with the HTML
page trimmed out; you can build and run that example directly.

```c
#include "kamran.k"
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>

static http_server_t *g_server = NULL;
static volatile sig_atomic_t shutdown_requested = 0;

static void signal_handler(int sig) {
    (void)sig;
    shutdown_requested = 1;
}

/* Message callback — echo whatever arrives, text or binary. */
static void on_ws_message(websocket_connection_t *conn, ws_message_type_t type,
                          const void *data, size_t len) {
    printf("Received %s message (%zu bytes)\n",
           type == WS_MESSAGE_TEXT ? "text" : "binary", len);

    if (websocket_send(conn, type, data, len) < 0) {
        fprintf(stderr, "Failed to send WebSocket message\n");
    }
}

/* Close callback — code only, no reason string. */
static void on_ws_close(websocket_connection_t *conn, uint16_t code) {
    (void)conn;
    printf("WebSocket closed with code %u\n", code);
}

static void on_ws_error(websocket_connection_t *conn, const char *error) {
    (void)conn;
    fprintf(stderr, "WebSocket error: %s\n", error);
}

/* Ordinary HTTP route handler — same two-argument signature as any other route. */
static void handle_websocket(http_request_t *req, http_response_t *res) {
    typedef struct {
        websocket_message_cb_t on_message;
        websocket_close_cb_t   on_close;
        websocket_error_cb_t   on_error;
        void                  *user_data;
    } websocket_callbacks_t;

    /* static, so it is still alive when the server reads it after we return */
    static websocket_callbacks_t callbacks = {
        on_ws_message, on_ws_close, on_ws_error, NULL
    };

    if (!websocket_handle_upgrade(req, res)) {
        /* Not a valid upgrade. For a bad Sec-WebSocket-Key or an unsupported
         * Sec-WebSocket-Version the function has already written a 400 with a
         * specific message; for a plain GET with no Upgrade header it writes
         * nothing at all. res->body is NULL in that second case, so answer it
         * without clobbering the more useful message from the first. */
        fprintf(stderr, "WebSocket handshake failed\n");
        if (!res->body) {
            http_response_send_text(res, HTTP_BAD_REQUEST, "Expected a WebSocket upgrade");
        }
        return;
    }

    req->user_data = &callbacks;
    /* The server now sends the 101, builds the connection, and runs the frame loop. */
}

int main(int argc, char *argv[]) {
    uint16_t port = 8080;
    router_t *router;

    if (argc > 1) {
        int p = atoi(argv[1]);
        if (p > 0 && p <= 65535) {
            port = (uint16_t)p;
        }
    }

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    g_server = http_server_create();
    router = router_create();
    if (!g_server || !router) {
        fprintf(stderr, "Failed to allocate server/router\n");
        return 1;
    }

    router_add_route(router, HTTP_GET, "/ws", handle_websocket);
    http_server_set_router(g_server, router);

    if (http_server_listen(g_server, port) < 0) {
        fprintf(stderr, "Failed to listen on port %u\n", (unsigned)port);
        router_destroy(router);
        http_server_destroy(g_server);
        return 1;
    }
    printf("WebSocket echo server on ws://localhost:%u/ws (Ctrl+C to stop)\n",
           (unsigned)port);

    while (!shutdown_requested) {
        sleep(1);
    }

    http_server_stop(g_server);
    router_destroy(router);
    http_server_destroy(g_server);
    return 0;
}
```

In the default threaded mode each WebSocket connection occupies one worker thread for its
whole lifetime, so the worker count (`http_server_set_thread_count()`, 16 by default) is
also your concurrent-connection ceiling. For many simultaneous connections, use async
mode — see [Async Mode](#async-mode) below.

## Sending Messages

Three send functions, all returning `int`: **0 on success, -1 on failure**. They do not
return a boolean — `if (websocket_send_text(...))` reads backwards and will silently do
the wrong thing.

### Text Messages

```c
/* Send a text message (UTF-8, NUL-terminated) */
if (websocket_send_text(conn, "Hello, WebSocket!") < 0) {
    fprintf(stderr, "send failed\n");
}

/* Send formatted text */
char buffer[256];
snprintf(buffer, sizeof(buffer), "User %s joined at %ld", username, (long)time(NULL));
websocket_send_text(conn, buffer);
```

### Binary Messages

```c
/* Send binary data (images, files, custom protocols) */
uint8_t binary_data[] = { 0x01, 0x02, 0x03, 0x04 };
websocket_send_binary(conn, binary_data, sizeof(binary_data));

/* Send a struct. Beware: this puts your machine's padding and endianness on the
 * wire. Fine between two builds of the same program, not an interchange format. */
typedef struct {
    uint32_t user_id;
    float x, y, z;
} position_update_t;

position_update_t update = { 123, 10.5f, 20.3f, 5.0f };
websocket_send_binary(conn, &update, sizeof(update));
```

### Choosing the Type at Runtime

When you already have a `ws_message_type_t` in hand — as the message callback does —
`websocket_send()` takes it directly and saves you the branch:

```c
websocket_send(conn, type, data, len);   /* type is WS_MESSAGE_TEXT or WS_MESSAGE_BINARY */
```

Incoming messages are capped at 16 MiB — that is `WS_MAX_MESSAGE_SIZE`, defined in
`src/websocket.c`, so changing it means rebuilding the library with
`-DWS_MAX_MESSAGE_SIZE=...`, not just recompiling your own program. The cap applies both to
a single frame and to a reassembled fragmented message; anything larger closes the
connection with RFC 6455 code 1009 rather than growing the buffer.

## Handling Different Message Types

The message callback receives both kinds. Switch on the `type` parameter:

```c
static void on_ws_message(websocket_connection_t *conn, ws_message_type_t type,
                          const void *data, size_t len) {
    if (type == WS_MESSAGE_BINARY) {
        const uint8_t *bytes = (const uint8_t *)data;

        printf("Binary message received: %zu bytes\n", len);

        /* Example: a one-byte message tag, read without alignment assumptions */
        if (len >= 1) {
            switch (bytes[0]) {
                case 0x01: handle_position_update(bytes, len); break;
                case 0x02: handle_chat_message(bytes, len); break;
                default:   printf("Unknown binary message type\n"); break;
            }
        }
    } else {
        const char *text = (const char *)data;

        printf("Text message: %.*s\n", (int)len, text);

        /* data is NOT NUL-terminated — copy it if you need a C string */
        {
            char *copy = (char *)malloc(len + 1);
            if (copy) {
                memcpy(copy, text, len);
                copy[len] = '\0';
                /* json_value_t *json = json_parse(copy); ... */
                free(copy);
            }
        }
    }
}
```

Two things to be careful about:

- **`data` is not NUL-terminated.** It points into the connection's frame buffer and is
  only valid for the duration of the callback. Use `len`, and copy anything you keep.
- **`data` is not aligned.** Casting it to `uint32_t *` is undefined behavior on
  architectures that care. Read bytes, or `memcpy` into a properly typed local.

Text messages are meant to be UTF-8; binary messages can hold any byte sequence.

## Ping/Pong for Keep-Alive

Ping/pong frames detect broken connections and stop proxies and firewalls from reaping an
idle connection.

**Incoming pings are answered for you.** When a client sends a ping, the library replies
with a pong carrying the same payload before your callbacks ever see it. Incoming pongs
are simply ignored. You do not need to write any of that.

What you may want to do is ping *outward*, to notice a client that has gone away without
closing:

```c
/* Send a ping with a payload */
const char *ping_data = "keepalive";
websocket_send_ping(conn, ping_data, strlen(ping_data));

/* Or an empty one */
websocket_send_ping(conn, NULL, 0);
```

Like the send functions, `websocket_send_ping()` and `websocket_send_pong()` return `0`
on success and `-1` on failure. In practice a `-1` from a ping is your signal that the
connection is gone.

```c
static void send_keepalive(websocket_connection_t *conn) {
    if (websocket_is_open(conn) && websocket_send_ping(conn, NULL, 0) < 0) {
        /* peer is gone; tear the connection down */
    }
}
```

Scheduling that ping is your job. In async mode, `event_loop_add_timeout()` on the
server's event loop is the natural place for it.

## Closing Connections

Close a connection gracefully with a status code. `kamran.k` defines the RFC 6455 codes
as the `ws_close_code_t` enum, so you can use the names instead of the numbers:

```c
websocket_close(conn, WS_CLOSE_NORMAL, "Normal closure");           /* 1000 */
websocket_close(conn, WS_CLOSE_GOING_AWAY, "Server shutting down"); /* 1001 */
websocket_close(conn, WS_CLOSE_PROTOCOL_ERROR, "Protocol error");   /* 1002 */
websocket_close(conn, WS_CLOSE_UNSUPPORTED, "Invalid data type");   /* 1003 */
websocket_close(conn, WS_CLOSE_NORMAL, NULL);                       /* no reason text */
```

The full enum: `WS_CLOSE_NORMAL` (1000), `WS_CLOSE_GOING_AWAY` (1001),
`WS_CLOSE_PROTOCOL_ERROR` (1002), `WS_CLOSE_UNSUPPORTED` (1003), `WS_CLOSE_NO_STATUS`
(1005), `WS_CLOSE_ABNORMAL` (1006), `WS_CLOSE_INVALID_DATA` (1007), `WS_CLOSE_POLICY`
(1008), `WS_CLOSE_TOO_LARGE` (1009), `WS_CLOSE_EXTENSION` (1010), `WS_CLOSE_UNEXPECTED`
(1011), `WS_CLOSE_TLS_FAILED` (1015). Note that 1005 and 1006 are receive-only codes in
RFC 6455 — never send them.

The library closes with 1002 on a protocol violation (an unmasked client frame, an
unexpected continuation, an unknown opcode) and 1009 on a message over the size cap,
without you doing anything.

Your close callback takes the code and nothing else:

```c
static void on_ws_close(websocket_connection_t *conn, uint16_t code) {
    (void)conn;
    printf("Connection closed: %u\n", code);
    /* Do NOT call websocket_connection_destroy() here — the server owns the
     * connection object and destroys it after the frame loop ends. */
}
```

Destroying the connection from inside the callback would leave the server's frame loop
holding a freed pointer. Only call `websocket_connection_destroy()` on a connection you
created yourself with `websocket_connection_create()`.

## Async Mode

Threaded mode dedicates a worker thread to each live WebSocket, which puts a hard ceiling
on concurrency. Async mode instead multiplexes every connection on one event loop, so
hundreds of idle connections cost almost nothing.

You do **not** have to write the event loop. Add one line to the server setup and keep the
exact same handler:

```c
g_server = http_server_create();
if (http_server_set_async(g_server, true) < 0) {   /* before http_server_listen() */
    fprintf(stderr, "Failed to enable async mode\n");
    return 1;
}
```

The server registers the upgraded socket with its own event loop, installs your callbacks
from `req->user_data` exactly as before, and reaps connections that go silent (an idle
deadline it refreshes on every frame, so a client that upgrades and then says nothing
forever cannot hold an fd open).

One behavioral difference to plan for: **in async mode `http_server_listen()` blocks.** It
runs the event loop on the calling thread and only returns after `http_server_stop()`. In
the default threaded mode it returns immediately.

`examples/async_websocket_echo_server.c` is the complete working version (it defaults to
port 8081 so it can run alongside the threaded example):

```bash
./build/examples/async_websocket_echo_server 8081
```

### Broadcasting

There is no built-in connection registry, so keeping one is up to you:

```c
#define MAX_CONNECTIONS 1000
static websocket_connection_t *g_connections[MAX_CONNECTIONS];

static void on_ws_message(websocket_connection_t *conn, ws_message_type_t type,
                          const void *data, size_t len) {
    int i;
    (void)conn;
    for (i = 0; i < MAX_CONNECTIONS; i++) {
        if (g_connections[i] && websocket_is_open(g_connections[i])) {
            websocket_send(g_connections[i], type, data, len);
        }
    }
}
```

In async mode every callback runs on the single event-loop thread, so a plain array like
this needs no locking. In threaded mode each connection is on its own thread and the same
array does — guard it with a mutex.

### Driving Your Own Event Loop

If you are embedding WebSockets in an event loop you already own, the pieces are public:

- `async_ws_manager_create(loop)` binds a manager to an `event_loop_t`
- `async_ws_manager_set_callbacks()` sets defaults for every connection it manages
- `async_ws_manager_add(mgr, ws)` makes the socket non-blocking, registers it with the
  loop, and from then on reads and parses frames for you — it calls
  `websocket_process_data()` internally and dispatches your callbacks
- `async_ws_send(mgr, ws, type, data, len)` queues a message and writes it when the socket
  becomes writable, instead of blocking
- `async_ws_manager_remove()` and `async_ws_manager_count()` handle the bookkeeping

`websocket_process_data(conn, bytes, len)` is only something you call yourself if you are
reading the socket by hand, without a manager. Likewise, call
`websocket_connection_destroy()` only on connections you created yourself with
`websocket_connection_create()`.

## Browser Client Example

Here's a simple HTML page to test your WebSocket server:

```html
<!DOCTYPE html>
<html>
<head>
    <title>WebSocket Chat</title>
    <style>
        body { font-family: Arial, sans-serif; max-width: 600px; margin: 50px auto; }
        #messages { border: 1px solid #ccc; height: 300px; overflow-y: scroll; 
                    padding: 10px; margin-bottom: 10px; }
        #input { width: 80%; padding: 5px; }
        #send { padding: 5px 15px; }
        .message { margin: 5px 0; }
    </style>
</head>
<body>
    <h1>WebSocket Echo Client</h1>
    <div id="messages"></div>
    <input type="text" id="input" placeholder="Type a message..." />
    <button id="send">Send</button>
    <button id="close">Close</button>
    
    <script>
        const messagesDiv = document.getElementById('messages');
        const input = document.getElementById('input');
        const sendBtn = document.getElementById('send');
        const closeBtn = document.getElementById('close');
        
        // Connect to WebSocket server
        const ws = new WebSocket('ws://localhost:8080/ws');
        
        ws.onopen = () => {
            addMessage('✓ Connected to server', 'green');
        };
        
        ws.onmessage = (event) => {
            addMessage('← ' + event.data, 'blue');
        };
        
        ws.onclose = (event) => {
            addMessage(`✗ Disconnected: ${event.code} - ${event.reason}`, 'red');
        };
        
        ws.onerror = (error) => {
            addMessage('✗ Error: ' + error.message, 'red');
        };
        
        sendBtn.onclick = () => {
            if (ws.readyState === WebSocket.OPEN) {
                const msg = input.value;
                ws.send(msg);
                addMessage('→ ' + msg, 'gray');
                input.value = '';
            }
        };
        
        closeBtn.onclick = () => {
            ws.close(1000, 'User closed connection');
        };
        
        input.onkeypress = (e) => {
            if (e.key === 'Enter') sendBtn.click();
        };
        
        function addMessage(text, color) {
            const div = document.createElement('div');
            div.className = 'message';
            div.textContent = text;
            div.style.color = color;
            messagesDiv.appendChild(div);
            messagesDiv.scrollTop = messagesDiv.scrollHeight;
        }
    </script>
</body>
</html>
```

Save this as `websocket_client.html` and open it in your browser after starting the
server. The shipped example goes one better: `websocket_echo_server` serves an equivalent
test page at `/`, so you can just open `http://localhost:8080/`.

## Building and Testing

### Build the Examples

The examples are part of the normal build, so there is nothing special to do:

```bash
cmake -S . -B build
cmake --build build --parallel
```

That gives you `build/examples/websocket_echo_server` (threaded) and
`build/examples/async_websocket_echo_server` (async).

To build your own program against the library:

```bash
cc -std=c11 -I /path/to/modern-c-web-library/include \
   my_ws_server.c -o my_ws_server \
   -L /path/to/modern-c-web-library/build -lweblib -lpthread
```

### Run the Server

```bash
./build/examples/websocket_echo_server
# HTTP server listening on port 8080 (threaded mode, pool=16)
```

Pass a port as the first argument to use something other than 8080.

### Test with a Browser

1. Open `http://localhost:8080/` (or your own `websocket_client.html`)
2. Type messages and watch them echo back
3. Open several tabs to test concurrent connections
4. Watch the server console for the per-message log lines

### Test with Command-Line Tools

```bash
# websocat
websocat ws://localhost:8080/ws

# wscat (npm install -g wscat)
wscat -c ws://localhost:8080/ws
```

### Run the WebSocket Tests

The async WebSocket layer has its own ctest suite:

```bash
(cd build && ctest --output-on-failure -R AsyncWebSocketTests)
```

It is one of the six suites a default build registers (`WebLibTests`,
`KamranHeaderTests`, `AsyncWebSocketTests`, `StressTests`, `WorkerTests`, `WasmTests`).

## WebSockets and TLS (`wss://`)

This is the one limit you need to know about before you design around it.

The library now ships an experimental pure-C TLS 1.3 server, enabled with
`-DWEBLIB_ENABLE_TLS=ON` and `http_server_enable_tls()`. **WebSocket upgrades over a TLS
connection are refused.** If a client sends an upgrade request on a TLS-terminated
connection, the server does not switch protocols — it answers:

```
HTTP/1.1 503 Service Unavailable

WebSocket over TLS not supported
```

and closes the connection. That is a deliberate choice, not a crash: rather than write
plaintext WebSocket frames into a TLS session and produce something subtly broken, the
server refuses cleanly. Wiring `wss://` through the TLS transport is deferred work.

So if you enable TLS on a server that also serves WebSockets, the HTTPS routes will work
and every `wss://` client will get a 503. Your options today:

- **Terminate TLS in a reverse proxy** (nginx, Caddy, a load balancer) and let this server
  speak plain HTTP and `ws://` behind it. This is the recommended setup anyway — the TLS
  layer is unaudited and should not face the public internet without an external
  cryptographic audit.
- **Run two servers**: one with TLS for HTTPS routes, one without for WebSockets. Workable,
  but you are then serving WebSockets unencrypted, which browsers will block from an HTTPS
  page.

Two related constraints, for completeness: TLS is **threaded mode only**
(`http_server_enable_tls()` returns -1 if async mode is enabled), and it is **native-only**
— WebAssembly and Cloudflare Workers builds ignore the option, because the browser or the
edge terminates TLS there.

See the [HTTPS section of Getting Started](getting-started.md#serving-https-experimental-tls-13)
for how to enable TLS, and [`src/tls/README.md`](../../src/tls/README.md) for the full
scope and security caveats.

## Summary and Next Steps

You've learned how to:

- Upgrade an HTTP connection to WebSocket with `websocket_handle_upgrade()` plus a
  callbacks struct on `req->user_data` — the server does the rest
- Send and receive text and binary messages, and read payloads safely (not
  NUL-terminated, not aligned)
- Rely on automatic pong replies, and ping outward to detect dead peers
- Close connections with the `ws_close_code_t` codes, and leave connection destruction to
  the server
- Switch to async mode for high connection counts with a single call
- Test with a browser page or `websocat`/`wscat`

### Next Steps

**Explore more:**
- Build a multi-room chat server with broadcasting
- Design a custom binary protocol
- Add authentication to the upgrade handler — it is an ordinary route handler, so
  middleware runs before it and you can reject the upgrade there
- Persist message history

**Production considerations:**
- Set `http_server_set_max_connections()` and use the rate-limit middleware
- Put a TLS-terminating reverse proxy in front for `wss://` (see the section above)
- Ping periodically to detect half-open connections
- Handle backpressure for slow clients — in async mode, `async_ws_send()` queues instead
  of blocking

**References:**
- [`examples/websocket_echo_server.c`](../../examples/websocket_echo_server.c) — the
  complete threaded example, with a browser test page
- [`examples/async_websocket_echo_server.c`](../../examples/async_websocket_echo_server.c)
  — the same thing in async mode
- [`docs/WEBSOCKET.md`](../WEBSOCKET.md) — the WebSocket reference documentation
- [`docs/api/README.md`](../api/README.md) — the full API reference
- RFC 6455 — the WebSocket protocol itself
