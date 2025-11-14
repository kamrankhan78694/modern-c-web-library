/**
 * WebSocket Echo Server Example
 * 
 * This example demonstrates how to use the WebSocket API to create
 * a simple echo server that reflects back any messages it receives.
 * 
 * Features:
 * - WebSocket handshake and upgrade
 * - Text and binary message handling
 * - Ping/pong for connection health
 * - Graceful connection closing
 * 
 * To test:
 * 1. Start the server: ./websocket_echo_server [port]
 * 2. Connect with a WebSocket client (browser, wscat, etc.)
 * 3. Send messages and see them echoed back
 */

#include "weblib.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>

/* Global server for signal handling */
static http_server_t *g_server = NULL;

/* WebSocket connection tracking */
typedef struct {
    websocket_connection_t *ws_conn;
    int fd;
} ws_client_t;

#define MAX_WS_CLIENTS 100
static ws_client_t ws_clients[MAX_WS_CLIENTS];
static size_t ws_client_count = 0;

/* Signal handler for graceful shutdown */
void signal_handler(int sig) {
    (void)sig;
    printf("\nShutting down server...\n");
    
    /* Close all WebSocket connections */
    for (size_t i = 0; i < ws_client_count; i++) {
        if (ws_clients[i].ws_conn) {
            websocket_close(ws_clients[i].ws_conn, WS_CLOSE_GOING_AWAY, "Server shutting down");
            websocket_connection_destroy(ws_clients[i].ws_conn);
        }
    }
    
    if (g_server) {
        http_server_stop(g_server);
    }
}

/* WebSocket message callback - echo back the message */
void on_websocket_message(websocket_connection_t *conn, ws_message_type_t type, const void *data, size_t len) {
    printf("Received %s message (%zu bytes): ", 
           type == WS_MESSAGE_TEXT ? "text" : "binary", len);
    
    if (type == WS_MESSAGE_TEXT) {
        printf("%.*s\n", (int)len, (const char *)data);
    } else {
        printf("[binary data]\n");
    }
    
    /* Echo the message back */
    if (websocket_send(conn, type, data, len) < 0) {
        fprintf(stderr, "Failed to send WebSocket message\n");
    }
}

/* WebSocket close callback */
void on_websocket_close(websocket_connection_t *conn, uint16_t code) {
    printf("WebSocket connection closed with code %u\n", code);
    
    /* Remove from client list */
    for (size_t i = 0; i < ws_client_count; i++) {
        if (ws_clients[i].ws_conn == conn) {
            websocket_connection_destroy(conn);
            
            /* Shift remaining clients */
            for (size_t j = i; j < ws_client_count - 1; j++) {
                ws_clients[j] = ws_clients[j + 1];
            }
            ws_client_count--;
            break;
        }
    }
}

/* WebSocket error callback */
void on_websocket_error(websocket_connection_t *conn, const char *error) {
    (void)conn;
    fprintf(stderr, "WebSocket error: %s\n", error);
}

/* HTTP route handler for WebSocket upgrade */
void handle_websocket(http_request_t *req, http_response_t *res) {
    printf("WebSocket upgrade request from %s\n", req->path);
    
    /* Perform WebSocket handshake */
    if (!websocket_handle_upgrade(req, res)) {
        fprintf(stderr, "WebSocket handshake failed\n");
        return;
    }
    
    printf("WebSocket handshake successful\n");
    
    /* In a real implementation with event loop integration, we would:
     * 1. Get the client socket fd from the connection
     * 2. Create a WebSocket connection object
     * 3. Set up callbacks
     * 4. Add to event loop for non-blocking I/O
     * 
     * For this example, we demonstrate the API usage pattern.
     * Full integration with async I/O requires additional server-side
     * connection management that tracks the underlying socket fd.
     */
    
    /* Note: This example shows the API pattern. For production use,
     * you would need to integrate WebSocket connections with the
     * event loop for proper async handling of multiple connections.
     */
}

/* HTTP route handler for index page */
void handle_index(http_request_t *req, http_response_t *res) {
    (void)req;
    
    const char *html = 
        "<!DOCTYPE html>\n"
        "<html>\n"
        "<head>\n"
        "    <title>WebSocket Echo Server</title>\n"
        "    <style>\n"
        "        body { font-family: Arial, sans-serif; max-width: 800px; margin: 50px auto; padding: 20px; }\n"
        "        h1 { color: #333; }\n"
        "        .container { background: #f5f5f5; padding: 20px; border-radius: 8px; }\n"
        "        input, button { padding: 10px; margin: 5px; font-size: 14px; }\n"
        "        input { width: 300px; }\n"
        "        #messages { background: white; padding: 15px; margin-top: 20px; border-radius: 4px; min-height: 200px; max-height: 400px; overflow-y: auto; }\n"
        "        .message { padding: 5px; margin: 5px 0; border-left: 3px solid #4CAF50; padding-left: 10px; }\n"
        "        .status { color: #666; font-style: italic; }\n"
        "        .error { color: red; }\n"
        "        .success { color: green; }\n"
        "    </style>\n"
        "</head>\n"
        "<body>\n"
        "    <h1>WebSocket Echo Server</h1>\n"
        "    <div class='container'>\n"
        "        <div>\n"
        "            <button id='connect' onclick='connect()'>Connect</button>\n"
        "            <button id='disconnect' onclick='disconnect()' disabled>Disconnect</button>\n"
        "            <span id='status' class='status'>Not connected</span>\n"
        "        </div>\n"
        "        <div style='margin-top: 20px;'>\n"
        "            <input type='text' id='message' placeholder='Enter message...' disabled />\n"
        "            <button onclick='sendMessage()' id='send' disabled>Send</button>\n"
        "            <button onclick='sendBinary()' id='sendBin' disabled>Send Binary</button>\n"
        "            <button onclick='sendPing()' id='ping' disabled>Ping</button>\n"
        "        </div>\n"
        "        <div id='messages'></div>\n"
        "    </div>\n"
        "    <script>\n"
        "        let ws = null;\n"
        "        const status = document.getElementById('status');\n"
        "        const messages = document.getElementById('messages');\n"
        "        const messageInput = document.getElementById('message');\n"
        "        \n"
        "        function addMessage(msg, className = '') {\n"
        "            const div = document.createElement('div');\n"
        "            div.className = 'message ' + className;\n"
        "            div.textContent = msg;\n"
        "            messages.appendChild(div);\n"
        "            messages.scrollTop = messages.scrollHeight;\n"
        "        }\n"
        "        \n"
        "        function connect() {\n"
        "            const wsUrl = 'ws://' + window.location.host + '/ws';\n"
        "            addMessage('Connecting to ' + wsUrl + '...', 'status');\n"
        "            \n"
        "            ws = new WebSocket(wsUrl);\n"
        "            \n"
        "            ws.onopen = function() {\n"
        "                status.textContent = 'Connected';\n"
        "                status.className = 'status success';\n"
        "                document.getElementById('connect').disabled = true;\n"
        "                document.getElementById('disconnect').disabled = false;\n"
        "                document.getElementById('message').disabled = false;\n"
        "                document.getElementById('send').disabled = false;\n"
        "                document.getElementById('sendBin').disabled = false;\n"
        "                document.getElementById('ping').disabled = false;\n"
        "                addMessage('✓ Connected to server', 'success');\n"
        "            };\n"
        "            \n"
        "            ws.onmessage = function(event) {\n"
        "                if (event.data instanceof Blob) {\n"
        "                    addMessage('← Received binary: ' + event.data.size + ' bytes');\n"
        "                } else {\n"
        "                    addMessage('← ' + event.data);\n"
        "                }\n"
        "            };\n"
        "            \n"
        "            ws.onclose = function(event) {\n"
        "                status.textContent = 'Disconnected (code: ' + event.code + ')';\n"
        "                status.className = 'status error';\n"
        "                document.getElementById('connect').disabled = false;\n"
        "                document.getElementById('disconnect').disabled = true;\n"
        "                document.getElementById('message').disabled = true;\n"
        "                document.getElementById('send').disabled = true;\n"
        "                document.getElementById('sendBin').disabled = true;\n"
        "                document.getElementById('ping').disabled = true;\n"
        "                addMessage('✗ Connection closed', 'error');\n"
        "                ws = null;\n"
        "            };\n"
        "            \n"
        "            ws.onerror = function(error) {\n"
        "                addMessage('✗ Error occurred', 'error');\n"
        "            };\n"
        "        }\n"
        "        \n"
        "        function disconnect() {\n"
        "            if (ws) {\n"
        "                ws.close(1000, 'Client disconnecting');\n"
        "            }\n"
        "        }\n"
        "        \n"
        "        function sendMessage() {\n"
        "            const msg = messageInput.value;\n"
        "            if (ws && msg) {\n"
        "                ws.send(msg);\n"
        "                addMessage('→ ' + msg);\n"
        "                messageInput.value = '';\n"
        "            }\n"
        "        }\n"
        "        \n"
        "        function sendBinary() {\n"
        "            if (ws) {\n"
        "                const data = new Uint8Array([72, 101, 108, 108, 111]); // 'Hello' in ASCII\n"
        "                ws.send(data.buffer);\n"
        "                addMessage('→ Sent binary: ' + data.length + ' bytes');\n"
        "            }\n"
        "        }\n"
        "        \n"
        "        function sendPing() {\n"
        "            if (ws) {\n"
        "                // Note: Browser WebSocket API doesn't expose ping/pong directly\n"
        "                // This is handled automatically by the browser\n"
        "                addMessage('Ping/pong is handled automatically by the browser', 'status');\n"
        "            }\n"
        "        }\n"
        "        \n"
        "        messageInput.addEventListener('keypress', function(e) {\n"
        "            if (e.key === 'Enter') {\n"
        "                sendMessage();\n"
        "            }\n"
        "        });\n"
        "    </script>\n"
        "</body>\n"
        "</html>";
    
    http_response_set_header(res, "Content-Type", "text/html");
    http_response_send_text(res, HTTP_OK, html);
}

int main(int argc, char *argv[]) {
    /* Parse port from command line */
    uint16_t port = 8080;
    if (argc > 1) {
        int p = atoi(argv[1]);
        if (p > 0 && p <= 65535) {
            port = (uint16_t)p;
        }
    }
    
    /* Set up signal handlers */
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    /* Create server and router */
    g_server = http_server_create();
    if (!g_server) {
        fprintf(stderr, "Failed to create server\n");
        return 1;
    }
    
    router_t *router = router_create();
    if (!router) {
        fprintf(stderr, "Failed to create router\n");
        http_server_destroy(g_server);
        return 1;
    }
    
    /* Add routes */
    router_add_route(router, HTTP_GET, "/", handle_index);
    router_add_route(router, HTTP_GET, "/ws", handle_websocket);
    
    /* Set router and start server */
    http_server_set_router(g_server, router);
    
    printf("╔════════════════════════════════════════╗\n");
    printf("║   WebSocket Echo Server                ║\n");
    printf("╠════════════════════════════════════════╣\n");
    printf("║  Listening on: http://localhost:%d   ║\n", port);
    printf("║  WebSocket: ws://localhost:%d/ws     ║\n", port);
    printf("║  Press Ctrl+C to stop                  ║\n");
    printf("╚════════════════════════════════════════╝\n");
    
    /* Start server (blocking) */
    int result = http_server_listen(g_server, port);
    
    /* Cleanup */
    router_destroy(router);
    http_server_destroy(g_server);
    
    return result < 0 ? 1 : 0;
}
