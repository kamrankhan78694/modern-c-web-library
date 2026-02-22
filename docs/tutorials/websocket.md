# Real-Time Applications with WebSocket

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

1. **HTTP Upgrade Request** — Client sends an HTTP request with `Upgrade: websocket` header
2. **Server Handshake** — Server validates the request and responds with upgrade headers
3. **Protocol Switch** — Connection switches from HTTP to WebSocket protocol
4. **Persistent Connection** — Both parties can now send/receive messages freely
5. **Graceful Closure** — Either party can initiate connection closure with status codes

The library handles the handshake automatically through `websocket_handle_upgrade()`, which:
- Validates the upgrade request headers
- Computes the Sec-WebSocket-Accept header
- Sends the 101 Switching Protocols response
- Returns the socket file descriptor for WebSocket communication

After the upgrade, you create a `websocket_connection_t` object to manage the connection, set callbacks for handling messages, and integrate with your event loop.

## Basic Echo Server

Let's build a simple WebSocket echo server that sends back any message it receives. This demonstrates the core WebSocket APIs.

```c
#include "http_server.h"
#include "websocket.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Global connection for this simple example
websocket_connection_t* ws_conn = NULL;

// Message callback - receives text/binary messages
void on_message(websocket_connection_t* conn, const char* data, 
                size_t length, bool is_binary) {
    printf("Received %s message (%zu bytes): %.*s\n",
           is_binary ? "binary" : "text", length, (int)length, data);
    
    // Echo the message back
    if (is_binary) {
        websocket_send_binary(conn, data, length);
    } else {
        websocket_send_text(conn, data);
    }
}

// Close callback - connection closed
void on_close(websocket_connection_t* conn, uint16_t code, const char* reason) {
    printf("Connection closed: code=%u, reason=%s\n", code, reason ? reason : "");
    ws_conn = NULL;
}

// HTTP handler for WebSocket upgrade
http_response_t* ws_handler(http_request_t* req) {
    http_response_t* res = http_response_create();
    
    // Perform WebSocket handshake
    int socket_fd = websocket_handle_upgrade(req, res);
    if (socket_fd < 0) {
        http_response_set_status(res, 400);
        http_response_set_body(res, "Bad Request - WebSocket upgrade failed", 39);
        return res;
    }
    
    // Create WebSocket connection from the socket
    ws_conn = websocket_connection_create(socket_fd);
    if (!ws_conn) {
        close(socket_fd);
        http_response_set_status(res, 500);
        http_response_set_body(res, "Internal Server Error", 21);
        return res;
    }
    
    // Set callbacks
    websocket_set_message_callback(ws_conn, on_message);
    websocket_set_close_callback(ws_conn, on_close);
    
    printf("WebSocket connection established on fd %d\n", socket_fd);
    
    // The response is already sent by websocket_handle_upgrade
    // Return NULL to indicate response was handled
    return NULL;
}

int main() {
    http_server_t* server = http_server_create("0.0.0.0", 8080);
    
    // Register WebSocket endpoint
    http_server_add_route(server, "/ws", ws_handler);
    
    printf("WebSocket echo server running on ws://localhost:8080/ws\n");
    
    // Start server
    http_server_start(server);
    
    // Simple event loop for processing WebSocket data
    char buffer[4096];
    while (1) {
        if (ws_conn && websocket_is_open(ws_conn)) {
            // Read data from socket (in real app, use select/epoll)
            int fd = ws_conn->socket_fd;
            int n = recv(fd, buffer, sizeof(buffer), MSG_DONTWAIT);
            
            if (n > 0) {
                websocket_process_data(ws_conn, buffer, n);
            } else if (n == 0) {
                // Connection closed
                websocket_connection_destroy(ws_conn);
                ws_conn = NULL;
            }
        }
        usleep(1000); // 1ms sleep to avoid busy loop
    }
    
    http_server_destroy(server);
    return 0;
}
```

This basic example shows the essential WebSocket operations. For production use, integrate with an event loop (see Async Mode section).

## Sending Messages

The library provides simple APIs for sending text and binary messages:

### Text Messages

```c
// Send a text message (UTF-8 string)
const char* message = "Hello, WebSocket!";
websocket_send_text(ws_conn, message);

// Send formatted text
char buffer[256];
snprintf(buffer, sizeof(buffer), "User %s joined at %ld", username, time(NULL));
websocket_send_text(ws_conn, buffer);
```

### Binary Messages

```c
// Send binary data (images, files, custom protocols)
uint8_t binary_data[] = {0x01, 0x02, 0x03, 0x04};
websocket_send_binary(ws_conn, binary_data, sizeof(binary_data));

// Send a struct
typedef struct {
    uint32_t user_id;
    float x, y, z;
} position_update_t;

position_update_t update = {123, 10.5f, 20.3f, 5.0f};
websocket_send_binary(ws_conn, &update, sizeof(update));
```

Both functions return `true` on success or `false` if the connection is closed or an error occurs.

## Handling Different Message Types

The message callback receives both text and binary messages. Use the `is_binary` parameter to distinguish:

```c
void on_message(websocket_connection_t* conn, const char* data, 
                size_t length, bool is_binary) {
    if (is_binary) {
        // Handle binary data
        printf("Binary message received: %zu bytes\n", length);
        
        // Example: Parse binary protocol
        if (length >= 4) {
            uint32_t msg_type = *(uint32_t*)data;
            switch (msg_type) {
                case 0x01: handle_position_update(data, length); break;
                case 0x02: handle_chat_message(data, length); break;
                default: printf("Unknown binary message type\n");
            }
        }
    } else {
        // Handle text data (UTF-8 string)
        printf("Text message: %.*s\n", (int)length, data);
        
        // Example: Parse JSON (if using a JSON library)
        // json_t* json = json_parse(data, length);
        // process_json_message(json);
    }
}
```

**Note:** Text messages are always valid UTF-8 strings. Binary messages can contain any byte sequence.

## Ping/Pong for Keep-Alive

WebSocket ping/pong frames are used for keep-alive and connection health checks:

```c
// Send a ping frame
const char* ping_data = "keepalive";
websocket_send_ping(ws_conn, ping_data, strlen(ping_data));

// The library automatically responds to incoming ping frames with pong
// You can also send custom pong frames if needed
```

Ping/pong frames help detect broken connections and prevent timeout disconnections by proxies or firewalls. The library handles incoming pings automatically by sending pong responses.

For periodic keep-alive:

```c
// In your event loop or timer callback
void send_keepalive(void* user_data) {
    websocket_connection_t* conn = (websocket_connection_t*)user_data;
    if (websocket_is_open(conn)) {
        websocket_send_ping(conn, NULL, 0); // Empty ping
    }
}

// Schedule this to run every 30 seconds
```

## Closing Connections

Close WebSocket connections gracefully with status codes:

```c
// Normal closure
websocket_close(ws_conn, 1000, "Normal closure");

// Going away (e.g., server shutting down)
websocket_close(ws_conn, 1001, "Server shutting down");

// Protocol error
websocket_close(ws_conn, 1002, "Protocol error");

// Invalid data received
websocket_close(ws_conn, 1003, "Invalid data type");

// No status code (empty close frame)
websocket_close(ws_conn, 1005, NULL);
```

Common WebSocket close codes (RFC 6455):
- **1000** — Normal closure
- **1001** — Going away (endpoint leaving)
- **1002** — Protocol error
- **1003** — Unsupported data
- **1008** — Policy violation
- **1009** — Message too big
- **1011** — Internal server error

The close callback is invoked when a close frame is received or sent:

```c
void on_close(websocket_connection_t* conn, uint16_t code, const char* reason) {
    printf("Connection closed: %u - %s\n", code, reason ? reason : "");
    
    // Cleanup resources
    websocket_connection_destroy(conn);
}
```

## Async Mode - Production WebSocket Servers

For production environments handling hundreds or thousands of concurrent WebSocket connections, use the library's async mode with an event loop:

```c
#include "http_server.h"
#include "websocket.h"
#include <sys/epoll.h>

#define MAX_CONNECTIONS 1000
websocket_connection_t* connections[MAX_CONNECTIONS] = {0};
int epoll_fd;

void on_ws_message(websocket_connection_t* conn, const char* data,
                   size_t length, bool is_binary) {
    // Broadcast to all connected clients
    for (int i = 0; i < MAX_CONNECTIONS; i++) {
        if (connections[i] && websocket_is_open(connections[i])) {
            websocket_send_text(connections[i], data);
        }
    }
}

void on_ws_close(websocket_connection_t* conn, uint16_t code, const char* reason) {
    // Remove from connections array
    for (int i = 0; i < MAX_CONNECTIONS; i++) {
        if (connections[i] == conn) {
            connections[i] = NULL;
            break;
        }
    }
    websocket_connection_destroy(conn);
}

http_response_t* ws_upgrade_handler(http_request_t* req) {
    http_response_t* res = http_response_create();
    int socket_fd = websocket_handle_upgrade(req, res);
    
    if (socket_fd < 0) {
        http_response_set_status(res, 400);
        return res;
    }
    
    websocket_connection_t* conn = websocket_connection_create(socket_fd);
    websocket_set_message_callback(conn, on_ws_message);
    websocket_set_close_callback(conn, on_ws_close);
    
    // Add to epoll for async I/O
    struct epoll_event ev;
    ev.events = EPOLLIN | EPOLLET; // Edge-triggered
    ev.data.ptr = conn;
    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, socket_fd, &ev);
    
    // Store connection
    for (int i = 0; i < MAX_CONNECTIONS; i++) {
        if (!connections[i]) {
            connections[i] = conn;
            break;
        }
    }
    
    return NULL; // Response already sent
}

int main() {
    http_server_t* server = http_server_create("0.0.0.0", 8080);
    http_server_set_async(server, true); // Enable async mode
    http_server_add_route(server, "/ws", ws_upgrade_handler);
    
    epoll_fd = epoll_create1(0);
    http_server_start(server);
    
    // Event loop
    struct epoll_event events[MAX_CONNECTIONS];
    char buffer[4096];
    
    while (1) {
        int nfds = epoll_wait(epoll_fd, events, MAX_CONNECTIONS, -1);
        
        for (int i = 0; i < nfds; i++) {
            websocket_connection_t* conn = events[i].data.ptr;
            int n = recv(conn->socket_fd, buffer, sizeof(buffer), 0);
            
            if (n > 0) {
                websocket_process_data(conn, buffer, n);
            } else {
                websocket_close(conn, 1001, "Connection lost");
            }
        }
    }
    
    return 0;
}
```

This async approach efficiently handles many concurrent connections using epoll (Linux) or similar mechanisms on other platforms.

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

Save this as `websocket_client.html` and open it in your browser after starting the server.

## Building and Testing

### Compile the Echo Server

```bash
# Using CMake
mkdir build && cd build
cmake ..
make websocket_echo_server

# Or compile directly
gcc -o ws_echo examples/websocket_echo_server.c \
    src/http_server.c src/websocket.c \
    -I include -lpthread
```

### Run the Server

```bash
./build/websocket_echo_server
# Output: WebSocket echo server running on ws://localhost:8080/ws
```

### Test with Browser Client

1. Open `websocket_client.html` in your browser
2. Type messages and see them echoed back
3. Open multiple browser tabs to test concurrent connections
4. Check the server console for connection logs

### Test with Command-Line Tools

```bash
# Using websocat (WebSocket client)
websocat ws://localhost:8080/ws

# Using wscat (npm install -g wscat)
wscat -c ws://localhost:8080/ws

# Type messages and see echoes
```

### Example Server Output

```
WebSocket echo server running on ws://localhost:8080/ws
WebSocket connection established on fd 5
Received text message (13 bytes): Hello, World!
Received text message (25 bytes): Testing binary protocol
Connection closed: code=1000, reason=Normal closure
```

## Summary and Next Steps

You've learned how to:

- ✅ Upgrade HTTP connections to WebSocket protocol
- ✅ Create and manage WebSocket connections
- ✅ Send and receive text/binary messages
- ✅ Handle ping/pong for keep-alive
- ✅ Close connections gracefully with status codes
- ✅ Build async WebSocket servers for high concurrency
- ✅ Create browser clients to interact with WebSocket servers

### Next Steps

**Explore More Features:**
- Build a multi-room chat server with broadcasting
- Implement custom binary protocols for gaming
- Add authentication and authorization to WebSocket endpoints
- Integrate with databases for persistent message history
- Use compression (permessage-deflate extension)

**Production Considerations:**
- Implement connection limits and rate limiting
- Add TLS/SSL for secure WebSocket (wss://)
- Monitor connection health with periodic pings
- Handle backpressure for slow clients
- Implement message queuing for offline users

**References:**
- See `examples/websocket_echo_server.c` for a complete working example
- Read `docs/api/websocket.md` for full API documentation
- Check RFC 6455 for WebSocket protocol details

WebSockets enable powerful real-time applications. Experiment with the examples, build your own projects, and leverage the Modern C Web Library's efficient WebSocket implementation for production use!
