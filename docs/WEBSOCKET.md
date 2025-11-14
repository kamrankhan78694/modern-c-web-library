# WebSocket Support Guide

The Modern C Web Library provides a complete, RFC 6455-compliant WebSocket implementation written entirely in pure C with no external dependencies.

## Features

- ✅ **RFC 6455 Compliant**: Full implementation of the WebSocket protocol
- ✅ **Message Types**: Support for text and binary messages
- ✅ **Fragmentation**: Automatic handling of fragmented messages
- ✅ **Control Frames**: Ping, pong, and close frames
- ✅ **Security**: Proper masking/unmasking and handshake validation
- ✅ **Pure C**: No external dependencies, all implemented from scratch
- ✅ **Cross-Platform**: Works on Linux, macOS, and Windows

## Quick Start

### 1. WebSocket Handshake

The WebSocket connection begins with an HTTP upgrade request:

```c
#include "weblib.h"

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

## Complete Example

See `examples/websocket_echo_server.c` for a complete WebSocket server implementation that includes:

- HTTP server with WebSocket upgrade endpoint
- Interactive browser-based test client
- Echo functionality (messages are sent back to sender)
- Connection management
- Graceful shutdown

Run the example:

```bash
cd build
./examples/websocket_echo_server 8080
```

Then open your browser to `http://localhost:8080` to use the interactive test client.

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

## Integration with Event Loop

For production use with many concurrent connections, integrate WebSocket with the event loop:

```c
/* Set up event loop */
event_loop_t *loop = event_loop_create();

/* Add WebSocket fd to event loop */
event_loop_add_fd(loop, socket_fd, EVENT_READ, websocket_event_handler, conn);

/* Event handler */
void websocket_event_handler(int fd, int events, void *user_data) {
    websocket_connection_t *conn = (websocket_connection_t *)user_data;
    
    if (events & EVENT_READ) {
        uint8_t buffer[4096];
        ssize_t n = recv(fd, buffer, sizeof(buffer), 0);
        
        if (n > 0) {
            websocket_process_data(conn, buffer, n);
        } else if (n == 0) {
            /* Connection closed */
            event_loop_remove_fd(loop, fd);
            websocket_connection_destroy(conn);
        }
    }
}

/* Run event loop */
event_loop_run(loop);
```

## Security Considerations

1. **Frame Masking**: Client-to-server frames MUST be masked (RFC requirement)
2. **Payload Size**: The library enforces maximum frame size (65536 bytes)
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
- Server-to-client frame masking is optional (RFC allows unmasked)
- No built-in heartbeat/timeout mechanism

Planned improvements:

- [ ] Per-message compression (permessage-deflate extension)
- [ ] UTF-8 validation for text frames
- [ ] Built-in connection timeout and heartbeat
- [ ] WebSocket over TLS (when TLS support is added)
- [ ] Subprotocol negotiation

## Testing

Run WebSocket tests:

```bash
cd build
make test
```

The test suite includes:
- Frame encoding/decoding
- Connection lifecycle
- Handshake validation
- Message fragmentation

## Troubleshooting

### Handshake Fails

**Symptoms:** Client receives 400 Bad Request or connection refused

**Solutions:**
- Verify all required headers are present (Upgrade, Connection, Sec-WebSocket-Key, Sec-WebSocket-Version)
- Check that WebSocket version is 13
- Ensure route is registered correctly

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
