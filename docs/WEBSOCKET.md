# WebSocket Support Guide

**Modern C Web Library v2.0.0** — RFC 6455 Compliant Implementation

The Modern C Web Library provides a complete, RFC 6455-compliant WebSocket implementation written entirely in pure C with no external dependencies.

## Features

- ✅ **RFC 6455 Compliant**: Full implementation of the WebSocket protocol
- ✅ **Message Types**: Support for text and binary messages
- ✅ **Fragmentation**: Automatic handling of fragmented messages
- ✅ **Control Frames**: Ping, pong, and close frames
- ✅ **Security**: Proper masking/unmasking and handshake validation
- ✅ **Pure C**: No external dependencies, all implemented from scratch
- ✅ **POSIX**: Built and tested on Linux and macOS (the WebSocket layer uses POSIX sockets directly, so Windows is not supported)

## Quick Start

### 1. WebSocket Handshake

The WebSocket connection begins with an HTTP upgrade request:

> **Plaintext `ws://` only.** If you have enabled the experimental TLS layer with
> `http_server_enable_tls()`, an upgrade on that connection is refused with
> `503 Service Unavailable` instead of being completed. See
> [Limitations & Future Work](#limitations--future-work).

```c
#include "kamran.k"

void handle_websocket(http_request_t *req, http_response_t *res) {
    /* Perform WebSocket handshake */
    if (!websocket_handle_upgrade(req, res)) {
        /* Handshake failed - response already sent with error */
        return;
    }
    
    /* Handshake successful - connection is now upgraded to WebSocket */
    printf("WebSocket connection established!\n");
}

/* Register the route */
router_add_route(router, HTTP_GET, "/ws", handle_websocket);
```

### 2. Creating a WebSocket Connection

> **If you are using this library's `http_server`, skip to
> [Integration with the Event Loop](#integration-with-the-event-loop).** In both
> threaded and async mode the server creates the `websocket_connection_t` for
> you, wires up your callbacks, runs the read loop, and destroys the connection
> at the end — you only hand it a callbacks struct via `req->user_data` from the
> route handler. Steps 2–6 below describe the manual path, for when you own the
> socket yourself.

After a successful handshake, create a WebSocket connection object:

```c
/* Assuming you have the socket file descriptor from the HTTP connection */
websocket_connection_t *conn = websocket_connection_create(socket_fd);
if (!conn) {
    fprintf(stderr, "Failed to create WebSocket connection\n");
    return;
}
```

### 3. Setting Up Callbacks

Configure callbacks to handle WebSocket events:

```c
/* Message callback - receives text and binary messages */
void on_message(websocket_connection_t *conn, ws_message_type_t type, 
                const void *data, size_t len) {
    if (type == WS_MESSAGE_TEXT) {
        printf("Text message: %.*s\n", (int)len, (const char *)data);
    } else {
        printf("Binary message: %zu bytes\n", len);
    }
}

/* Close callback - called when connection closes */
void on_close(websocket_connection_t *conn, uint16_t code) {
    printf("Connection closed with code %u\n", code);
}

/* Error callback - called on protocol errors */
void on_error(websocket_connection_t *conn, const char *error) {
    fprintf(stderr, "WebSocket error: %s\n", error);
}

/* Set the callbacks */
websocket_set_message_callback(conn, on_message);
websocket_set_close_callback(conn, on_close);
websocket_set_error_callback(conn, on_error);
```

### 4. Sending Messages

Send text or binary messages to the client:

```c
/* Send text message */
const char *text = "Hello, WebSocket!";
websocket_send_text(conn, text);

/* Send binary message */
uint8_t data[] = {0x01, 0x02, 0x03, 0x04};
websocket_send_binary(conn, data, sizeof(data));

/* Send ping (for keep-alive) */
websocket_send_ping(conn, "ping", 4);

/* Close connection gracefully */
websocket_close(conn, WS_CLOSE_NORMAL, "Goodbye");
```

### 5. Processing Incoming Data

When data is received on the socket, pass it to the WebSocket connection:

```c
uint8_t buffer[4096];
ssize_t bytes_read = recv(socket_fd, buffer, sizeof(buffer), 0);

if (bytes_read > 0) {
    /* Process the WebSocket frames */
    if (websocket_process_data(conn, buffer, bytes_read) < 0) {
        fprintf(stderr, "Error processing WebSocket data\n");
    }
}
```

### 6. Cleanup

Always destroy the WebSocket connection when done:

```c
websocket_connection_destroy(conn);
```

## Complete Examples

Two examples ship with the library, one per server mode.

`examples/websocket_echo_server.c` — **threaded mode** (the default). One thread
per connection, blocking I/O. It includes:

- HTTP server with a WebSocket upgrade endpoint
- Interactive browser-based test client served from `/`
- Echo functionality (messages are sent back to sender)
- Connection management
- Graceful shutdown

```bash
cd build
./examples/websocket_echo_server 8080
```

Then open your browser to `http://localhost:8080` to use the interactive test client.

`examples/async_websocket_echo_server.c` — **async mode**
(`http_server_set_async(server, true)`). A single event loop drives every
connection, so a WebSocket costs an fd rather than a thread. It has no browser
client page; connect with `ws://host:8081/ws`.

```bash
cd build
./examples/async_websocket_echo_server 8081
```

See [Integration with the Event Loop](#integration-with-the-event-loop) for how
to choose between them.

## API Reference

### Connection Management

#### `websocket_handle_upgrade()`

```c
bool websocket_handle_upgrade(http_request_t *req, http_response_t *res);
```

Performs the WebSocket handshake. Call this from an HTTP route handler.

**Parameters:**
- `req`: HTTP request object
- `res`: HTTP response object

**Returns:** `true` if handshake succeeded, `false` otherwise

#### `websocket_connection_create()`

```c
websocket_connection_t *websocket_connection_create(int fd);
```

Creates a WebSocket connection from a file descriptor.

**Parameters:**
- `fd`: Socket file descriptor

**Returns:** WebSocket connection or NULL on failure

#### `websocket_connection_destroy()`

```c
void websocket_connection_destroy(websocket_connection_t *conn);
```

Destroys a WebSocket connection and frees resources.

### Sending Messages

#### `websocket_send()`

```c
int websocket_send(websocket_connection_t *conn, ws_message_type_t type, const void *data, size_t len);
```

Sends a message of an explicitly chosen type. Useful when you are echoing back
whatever type you received, as in `examples/async_websocket_echo_server.c`.

**Returns:** 0 on success, -1 on failure

#### `websocket_send_text()`

```c
int websocket_send_text(websocket_connection_t *conn, const char *text);
```

Sends a text message (UTF-8 encoded).

**Returns:** 0 on success, -1 on failure

#### `websocket_send_binary()`

```c
int websocket_send_binary(websocket_connection_t *conn, const void *data, size_t len);
```

Sends a binary message.

**Returns:** 0 on success, -1 on failure

#### `websocket_send_ping()`

```c
int websocket_send_ping(websocket_connection_t *conn, const void *data, size_t len);
```

Sends a ping frame (for connection keep-alive).

**Returns:** 0 on success, -1 on failure

#### `websocket_send_pong()`

```c
int websocket_send_pong(websocket_connection_t *conn, const void *data, size_t len);
```

Sends a pong frame (typically in response to a ping).

**Returns:** 0 on success, -1 on failure

#### `websocket_close()`

```c
int websocket_close(websocket_connection_t *conn, uint16_t code, const char *reason);
```

Closes the WebSocket connection gracefully.

**Parameters:**
- `code`: Close code (see `ws_close_code_t` enum)
- `reason`: Optional close reason string

**Returns:** 0 on success, -1 on failure

### Receiving Messages

#### `websocket_process_data()`

```c
int websocket_process_data(websocket_connection_t *conn, const uint8_t *data, size_t len);
```

Processes incoming WebSocket data. This function:
- Parses WebSocket frames
- Handles fragmentation
- Invokes appropriate callbacks
- Responds to control frames

**Parameters:**
- `data`: Received data buffer
- `len`: Length of data

**Returns:** 0 on success, -1 on failure

### Callbacks

#### `websocket_set_message_callback()`

```c
void websocket_set_message_callback(websocket_connection_t *conn, 
                                    websocket_message_cb_t callback);
```

Sets the callback for incoming messages.

**Callback signature:**
```c
typedef void (*websocket_message_cb_t)(websocket_connection_t *conn, 
                                       ws_message_type_t type, 
                                       const void *data, 
                                       size_t len);
```

#### `websocket_set_close_callback()`

```c
void websocket_set_close_callback(websocket_connection_t *conn, 
                                  websocket_close_cb_t callback);
```

Sets the callback for connection close events.

**Callback signature:**
```c
typedef void (*websocket_close_cb_t)(websocket_connection_t *conn, 
                                     uint16_t code);
```

#### `websocket_set_error_callback()`

```c
void websocket_set_error_callback(websocket_connection_t *conn, 
                                  websocket_error_cb_t callback);
```

Sets the callback for protocol errors.

**Callback signature:**
```c
typedef void (*websocket_error_cb_t)(websocket_connection_t *conn, 
                                     const char *error);
```

### Utilities

#### `websocket_is_open()`

```c
bool websocket_is_open(websocket_connection_t *conn);
```

Checks if the WebSocket connection is open.

**Returns:** `true` if open, `false` otherwise

#### `websocket_get_fd()`

```c
int websocket_get_fd(websocket_connection_t *conn);
```

Returns the underlying socket file descriptor, or -1 if `conn` is NULL.

#### `websocket_set_user_data()` / `websocket_get_user_data()`

```c
void websocket_set_user_data(websocket_connection_t *conn, void *user_data);
void *websocket_get_user_data(websocket_connection_t *conn);
```

Attach custom data to a WebSocket connection.

## Message Types

```c
typedef enum {
    WS_MESSAGE_TEXT,    /* UTF-8 text message */
    WS_MESSAGE_BINARY   /* Binary message */
} ws_message_type_t;
```

## Close Codes

Standard WebSocket close codes (RFC 6455):

```c
typedef enum {
    WS_CLOSE_NORMAL = 1000,           /* Normal closure */
    WS_CLOSE_GOING_AWAY = 1001,       /* Endpoint is going away */
    WS_CLOSE_PROTOCOL_ERROR = 1002,   /* Protocol error */
    WS_CLOSE_UNSUPPORTED = 1003,      /* Unsupported data type */
    WS_CLOSE_NO_STATUS = 1005,        /* No status code received */
    WS_CLOSE_ABNORMAL = 1006,         /* Abnormal closure */
    WS_CLOSE_INVALID_DATA = 1007,     /* Invalid frame payload data */
    WS_CLOSE_POLICY = 1008,           /* Policy violation */
    WS_CLOSE_TOO_LARGE = 1009,        /* Message too large */
    WS_CLOSE_EXTENSION = 1010,        /* Extension negotiation failure */
    WS_CLOSE_UNEXPECTED = 1011,       /* Unexpected condition */
    WS_CLOSE_TLS_FAILED = 1015        /* TLS handshake failure */
} ws_close_code_t;
```

## Protocol Details

### Handshake Process

1. Client sends HTTP GET request with upgrade headers:
   ```
   GET /ws HTTP/1.1
   Host: localhost:8080
   Upgrade: websocket
   Connection: Upgrade
   Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==
   Sec-WebSocket-Version: 13
   ```

2. Server validates and responds with:
   ```
   HTTP/1.1 101 Switching Protocols
   Upgrade: websocket
   Connection: Upgrade
   Sec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=
   ```

3. The `Sec-WebSocket-Accept` key is computed as:
   ```
   Base64(SHA1(Sec-WebSocket-Key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"))
   ```

### Frame Format

WebSocket frames consist of:
- **FIN bit**: Indicates final fragment
- **Opcode**: Frame type (text, binary, close, ping, pong)
- **Mask bit**: Indicates if payload is masked (required for client→server)
- **Payload length**: 7, 7+16, or 7+64 bits
- **Masking key**: 4 bytes (if masked)
- **Payload data**: The actual message

### Fragmentation

Large messages can be split into multiple frames:
1. First frame: Opcode indicates message type, FIN=0
2. Continuation frames: Opcode=0 (continuation), FIN=0
3. Final frame: Opcode=0 (continuation), FIN=1

The library automatically reassembles fragmented messages before invoking the message callback.

## Integration with the Event Loop

If you are handling many concurrent connections, you do not want a thread per
WebSocket. There are three ways to get WebSockets onto an event loop, in
descending order of how much the library does for you. Start at the top and only
move down when you need what the next tier gives you.

### Tier 1 — let `http_server` drive it (recommended)

Turn on async mode and the server runs the whole thing: it accepts, parses the
upgrade request, sends the `101`, then switches the connection to a WebSocket
read handler on its own event loop. You write route handlers, not I/O code.

```c
static void on_message(websocket_connection_t *ws, ws_message_type_t type,
                       const void *data, size_t len) {
    websocket_send(ws, type, data, len);   /* echo */
}
static void on_close(websocket_connection_t *ws, uint16_t code) { (void)ws; (void)code; }
static void on_error(websocket_connection_t *ws, const char *err) { (void)ws; (void)err; }

static void handle_ws(http_request_t *req, http_response_t *res) {
    if (!websocket_handle_upgrade(req, res)) {
        return;                     /* error response already sent */
    }
    /* The server reads these four fields, in this order, out of
     * req->user_data after the 101 flushes. */
    typedef struct {
        websocket_message_cb_t on_message;
        websocket_close_cb_t   on_close;
        websocket_error_cb_t   on_error;
        void                  *user_data;
    } websocket_callbacks_t;
    static websocket_callbacks_t callbacks = {
        on_message, on_close, on_error, NULL
    };
    req->user_data = &callbacks;
}

http_server_t *server = http_server_create();
http_server_set_async(server, true);          /* before http_server_listen() */

router_t *router = router_create();
router_add_route(router, HTTP_GET, "/ws", handle_ws);
http_server_set_router(server, router);

http_server_listen(server, 8081);
```

The callbacks struct must outlive the request — declare it `static`, or point
`req->user_data` at something you own for the life of the connection.

What the server does for you on the async path: creates the
`websocket_connection_t`, installs your callbacks, tears down the HTTP
request/response/parser, registers its own read handler, and destroys the
connection when the peer closes or a frame fails to parse. It also gives the
live WebSocket an **idle deadline** of `read_timeout_sec`, refreshed on every
inbound frame, so a client that upgrades and then goes silent is reaped instead
of holding an fd and a connection-cap slot forever.

Threaded mode (the default) reads the same callbacks struct from the same place,
so the route handler above is unchanged if you drop the
`http_server_set_async()` call. What differs is what happens after the `101`:
threaded mode hands the WebSocket its own thread and a blocking read loop —
simpler, but it costs a thread per connection, which is the reason to prefer
async once you have many of them.

Worked example: `examples/async_websocket_echo_server.c`.

### Tier 2 — `async_ws_manager_*` for sockets you own

Use this when the WebSocket did not come from this library's `http_server` — for
example you terminated the handshake yourself, or you are multiplexing
WebSockets alongside other work on your own `event_loop_t`.

```c
event_loop_t *loop = event_loop_create();
async_ws_manager_t *mgr = async_ws_manager_create(loop);

async_ws_manager_set_callbacks(mgr, on_message, on_close, on_error);

/* Hand over an already-upgraded connection. This makes the socket
 * non-blocking and registers it with the event loop for reads. */
async_ws_manager_add(mgr, ws);

/* Queues the frame and drains it when the socket is writable —
 * never blocks, even if the peer has stopped reading. */
async_ws_send(mgr, ws, WS_MESSAGE_TEXT, "hello", 5);

event_loop_run(loop);

/* Teardown */
async_ws_manager_remove(mgr, ws);   /* unregisters, frees the write queue */
async_ws_manager_destroy(mgr);
websocket_connection_destroy(ws);   /* still yours to free — see below */
```

Two contracts to hold onto, because both are real properties of the current
implementation:

1. **You own the `websocket_connection_t`.** Neither
   `async_ws_manager_remove()` nor `async_ws_manager_destroy()` destroys it;
   they only unregister the fd and free the manager's own bookkeeping. Call
   `websocket_connection_destroy()` yourself.
2. **There is no idle reaping here.** The manager has no timers — the idle
   deadline described in Tier 1 belongs to `http_server`, not to
   `async_ws_manager_*`. If you need to reap silent clients on this path, track
   last-activity time in your own callbacks.

A manager holds at most 1024 connections. `async_ws_manager_count()` reports how
many are currently registered.

### Tier 3 — raw `event_loop_add_fd()`

Only if you need something neither tier above gives you. You are then
responsible for the read loop, the close path, and all lifetime management:

```c
static event_loop_t *g_loop;

static void websocket_event_handler(int fd, int events, void *user_data) {
    websocket_connection_t *conn = (websocket_connection_t *)user_data;

    if (events & EVENT_READ) {
        uint8_t buffer[4096];
        ssize_t n = recv(fd, buffer, sizeof(buffer), 0);

        if (n > 0) {
            websocket_process_data(conn, buffer, (size_t)n);
        } else if (n == 0) {
            /* Peer closed */
            event_loop_remove_fd(g_loop, fd);
            websocket_connection_destroy(conn);
        }
    }
}

g_loop = event_loop_create();
event_loop_add_fd(g_loop, socket_fd, EVENT_READ, websocket_event_handler, conn);
event_loop_run(g_loop);
```

Note that `websocket_process_data()` may invoke your close callback, which is a
common place to free the connection — so do not touch `conn` after it returns
without knowing whether your own callback already destroyed it.

## Security Considerations

1. **Frame Masking**: Client-to-server frames MUST be masked (RFC 6455 §5.1). The
   library enforces this — an unmasked client frame is closed with code 1002.
   Server-to-client frames are never masked, which is what the RFC requires of a
   server.
2. **Payload Size**: The library caps both a single frame and a fully reassembled fragmented message at `WS_MAX_MESSAGE_SIZE` (default 16 MiB, defined in `src/websocket.c`). An oversized frame is rejected from its header alone, before any payload is buffered, and the connection is closed with code 1009 (Message Too Big). The 64 KiB `WS_FRAME_MAX_SIZE` figure is only the initial receive-buffer allocation, which grows on demand. To change the cap, rebuild the library with `-DWS_MAX_MESSAGE_SIZE=<bytes>` in your compiler flags (e.g. `cmake -DCMAKE_C_FLAGS=-DWS_MAX_MESSAGE_SIZE=1048576`) — it is a compile-time constant, not a CMake option.
3. **UTF-8 Validation**: Text frames should contain valid UTF-8 (not yet enforced)
4. **Origin Validation**: Check the `Origin` header during handshake if needed
5. **Rate Limiting**: Implement rate limiting for message frequency
6. **Authentication**: Perform authentication before WebSocket upgrade

## Performance Tips

1. **Buffer Management**: Reuse buffers to minimize allocations
2. **Event Loop**: Use async I/O for multiple connections
3. **Fragmentation**: Consider message size to avoid excessive fragmentation
4. **Keep-Alive**: Use ping/pong frames to detect stale connections
5. **Compression**: Consider implementing per-message compression extension (permessage-deflate)

## Limitations & Future Work

Current limitations:

- No WebSocket extensions (compression, multiplexing)
- No UTF-8 validation for text frames
- No subprotocol negotiation (`Sec-WebSocket-Protocol` is not echoed back)
- No automatic heartbeat: the library never sends pings on its own. Call
  `websocket_send_ping()` from your own timer if you need application-level
  keepalive.
- No WebSocket-level ping/pong liveness check. Idle connections are still
  bounded, though: in async mode a live WebSocket carries an idle deadline of
  `read_timeout_sec` that is refreshed on every inbound frame, and in threaded
  mode the accepted socket keeps the `SO_RCVTIMEO` applied at accept (the lesser
  of `read_timeout_sec` and `request_timeout_sec`), so a silent client's
  blocking `recv()` times out and the connection is torn down.
- **No `wss` (WebSocket over TLS).** The library ships an experimental,
  unaudited TLS 1.3 server (`http_server_enable_tls()`, opt-in via the
  `WEBLIB_ENABLE_TLS` CMake option, OFF by default, native and threaded mode
  only). `wss` is *not* wired to it: a route that returns
  `101 Switching Protocols` on a TLS connection is refused with
  `503 Service Unavailable` and the body `WebSocket over TLS not supported`
  (`src/http_server.c`). To serve `wss` today, terminate TLS at a reverse proxy
  and forward plaintext WebSocket to this server. See `src/tls/README.md` for
  the TLS status and caveats.

Planned improvements:

- [ ] Per-message compression (permessage-deflate extension)
- [ ] UTF-8 validation for text frames
- [ ] Built-in ping/pong heartbeat (automatic keepalive pings)
- [ ] WebSocket over TLS (`wss`) — see the note under "Current limitations" above
- [ ] Subprotocol negotiation

## Testing

Run the whole suite:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build --parallel
cd build && ctest --output-on-failure
```

A default build runs six ctest suites. Two of them cover WebSockets:
`WebLibTests` (frame encoding/decoding, connection lifecycle, handshake
validation, message fragmentation) and `AsyncWebSocketTests` (the
`async_ws_manager_*` path). Run just those with:

```bash
cd build && ctest -R 'WebLibTests|AsyncWebSocketTests' --output-on-failure
```

## Troubleshooting

### Handshake Fails

**Symptoms:** Client receives 400 Bad Request or connection refused

**Solutions:**
- Verify all required headers are present (Upgrade, Connection, Sec-WebSocket-Key, Sec-WebSocket-Version)
- Check that WebSocket version is 13
- Ensure route is registered correctly

### Upgrade Returns 503 "WebSocket over TLS not supported"

**Symptoms:** the route runs and the handshake validates, but the client gets
`503 Service Unavailable` instead of `101 Switching Protocols`.

**Cause:** you called `http_server_enable_tls()` on this server. `wss` is not
implemented, and the server refuses the upgrade rather than writing plaintext
WebSocket frames onto the TLS connection.

**Solution:** serve WebSockets from a plaintext listener, or terminate TLS at a
reverse proxy and forward plaintext WebSocket traffic to this server.

### Messages Not Received

**Symptoms:** `on_message` callback not invoked

**Solutions:**
- Verify `websocket_process_data()` is called with received data
- Check that callbacks are set before data arrives
- Ensure data is being read from the socket correctly

### Connection Closes Unexpectedly

**Symptoms:** Connection closes without explicit close frame

**Solutions:**
- Check for socket errors
- Verify client is sending properly masked frames
- Look for protocol violations (logged via error callback)
- Implement ping/pong for connection health checks

## Resources

- [RFC 6455 - The WebSocket Protocol](https://tools.ietf.org/html/rfc6455)
- [MDN WebSocket API](https://developer.mozilla.org/en-US/docs/Web/API/WebSocket)
- [WebSocket Extensions](https://www.iana.org/assignments/websocket/websocket.xml)

## Contributing

Found a bug or want to add WebSocket extensions? See [CONTRIBUTING.md](../CONTRIBUTING.md) for contribution guidelines.
