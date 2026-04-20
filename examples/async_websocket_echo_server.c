/* Async WebSocket Echo Server Example */
#include "kamran.k"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>

static http_server_t *g_server = NULL;
static volatile bool server_running = true;
static const char index_html[] =
    "<!DOCTYPE html>\n"
    "<html>\n"
    "<head>\n"
    "    <title>Async WebSocket Echo</title>\n"
    "    <style>\n"
    "        body { font-family: Arial, sans-serif; max-width: 720px; margin: 40px auto; padding: 0 16px; }\n"
    "        .controls { display: flex; gap: 8px; margin-bottom: 12px; }\n"
    "        input, button { padding: 8px 12px; font-size: 14px; }\n"
    "        input { flex: 1; }\n"
    "        #log { border: 1px solid #ccc; border-radius: 6px; min-height: 220px; padding: 12px; background: #fafafa; overflow-y: auto; }\n"
    "        .status { color: #555; }\n"
    "        .sent { color: #0a7a0a; }\n"
    "        .received { color: #0047ab; }\n"
    "        .error { color: #b00020; }\n"
    "    </style>\n"
    "</head>\n"
    "<body>\n"
    "    <h1>Async WebSocket Echo</h1>\n"
    "    <p>Open a WebSocket connection to <code>/ws</code>, send a message, and watch the server echo it back.</p>\n"
    "    <div class='controls'>\n"
    "        <button id='connect' onclick='connectSocket()'>Connect</button>\n"
    "        <button id='disconnect' onclick='disconnectSocket()' disabled>Disconnect</button>\n"
    "    </div>\n"
    "    <div class='controls'>\n"
    "        <input id='message' type='text' value='Hello from the browser' disabled />\n"
    "        <button id='send' onclick='sendMessage()' disabled>Send</button>\n"
    "    </div>\n"
    "    <div id='log'></div>\n"
    "    <script>\n"
    "        let ws = null;\n"
    "        const logEl = document.getElementById('log');\n"
    "        const connectBtn = document.getElementById('connect');\n"
    "        const disconnectBtn = document.getElementById('disconnect');\n"
    "        const sendBtn = document.getElementById('send');\n"
    "        const messageInput = document.getElementById('message');\n"
    "        function appendLog(message, className) {\n"
    "            const line = document.createElement('div');\n"
    "            line.className = className;\n"
    "            line.textContent = message;\n"
    "            logEl.appendChild(line);\n"
    "            logEl.scrollTop = logEl.scrollHeight;\n"
    "        }\n"
    "        function setConnectedState(connected) {\n"
    "            connectBtn.disabled = connected;\n"
    "            disconnectBtn.disabled = !connected;\n"
    "            sendBtn.disabled = !connected;\n"
    "            messageInput.disabled = !connected;\n"
    "        }\n"
    "        function connectSocket() {\n"
    "            const scheme = window.location.protocol === 'https:' ? 'wss://' : 'ws://';\n"
    "            const url = scheme + window.location.host + '/ws';\n"
    "            appendLog('Connecting to ' + url, 'status');\n"
    "            ws = new WebSocket(url);\n"
    "            ws.onopen = function() {\n"
    "                setConnectedState(true);\n"
    "                appendLog('Connected', 'status');\n"
    "            };\n"
    "            ws.onmessage = function(event) {\n"
    "                appendLog('Received: ' + event.data, 'received');\n"
    "            };\n"
    "            ws.onclose = function(event) {\n"
    "                setConnectedState(false);\n"
    "                appendLog('Closed with code ' + event.code, 'status');\n"
    "                ws = null;\n"
    "            };\n"
    "            ws.onerror = function() {\n"
    "                appendLog('WebSocket error', 'error');\n"
    "            };\n"
    "        }\n"
    "        function disconnectSocket() {\n"
    "            if (ws) {\n"
    "                ws.close(1000, 'Done testing');\n"
    "            }\n"
    "        }\n"
    "        function sendMessage() {\n"
    "            if (ws && messageInput.value.length > 0) {\n"
    "                ws.send(messageInput.value);\n"
    "                appendLog('Sent: ' + messageInput.value, 'sent');\n"
    "                messageInput.focus();\n"
    "            }\n"
    "        }\n"
    "        messageInput.addEventListener('keydown', function(event) {\n"
    "            if (event.key === 'Enter') {\n"
    "                sendMessage();\n"
    "            }\n"
    "        });\n"
    "        appendLog('Ready to connect.', 'status');\n"
    "    </script>\n"
    "</body>\n"
    "</html>\n";

static void signal_handler(int sig) {
    (void)sig;
    server_running = false;
    if (g_server) {
        http_server_stop(g_server);
    }
}

static void on_ws_message(websocket_connection_t *conn, ws_message_type_t type, const void *data, size_t len) {
    if (type == WS_MESSAGE_TEXT) {
        printf("[async-ws] text (%zu): %.*s\n", len, (int)len, (const char *)data);
    } else {
        printf("[async-ws] binary (%zu bytes)\n", len);
    }
    websocket_send(conn, type, data, len); /* echo */
}

static void on_ws_close(websocket_connection_t *conn, uint16_t code) {
    (void)conn;
    printf("[async-ws] closed code=%u\n", code);
}

static void on_ws_error(websocket_connection_t *conn, const char *error) {
    (void)conn;
    fprintf(stderr, "[async-ws] error: %s\n", error);
}

static void handle_ws(http_request_t *req, http_response_t *res) {
    if (!websocket_handle_upgrade(req, res)) {
        fprintf(stderr, "Handshake failed\n");
        return;
    }
    typedef struct {
        websocket_message_cb_t on_message;
        websocket_close_cb_t on_close;
        websocket_error_cb_t on_error;
        void *user_data;
    } websocket_callbacks_t;
    static websocket_callbacks_t callbacks = {
        .on_message = on_ws_message,
        .on_close = on_ws_close,
        .on_error = on_ws_error,
        .user_data = NULL
    };
    req->user_data = &callbacks;
}

static void handle_index(http_request_t *req, http_response_t *res) {
    (void)req;
    http_response_set_header(res, "Content-Type", "text/html");
    http_response_send_text(res, HTTP_OK, index_html);
}

int main(int argc, char *argv[]) {
    uint16_t port = 8081; /* different default to avoid clash */
    if (argc > 1) {
        int p = atoi(argv[1]);
        if (p > 0 && p <= 65535) port = (uint16_t)p;
    }
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    g_server = http_server_create();
    if (!g_server) {
        fprintf(stderr, "Failed to create server\n");
        return 1;
    }
    if (http_server_set_async(g_server, true) < 0) {
        fprintf(stderr, "Failed to enable async mode\n");
        http_server_destroy(g_server);
        return 1;
    }

    router_t *router = router_create();
    if (!router) {
        fprintf(stderr, "Failed to create router\n");
        http_server_destroy(g_server);
        return 1;
    }
    router_add_route(router, HTTP_GET, "/", handle_index);
    router_add_route(router, HTTP_GET, "/ws", handle_ws);
    http_server_set_router(g_server, router);

    printf("Async WebSocket Echo Server listening on %u (ws endpoint /ws)\n", port);
    if (http_server_listen(g_server, port) < 0) {
        fprintf(stderr, "Listen failed\n");
        router_destroy(router);
        http_server_destroy(g_server);
        return 1;
    }

    while (server_running) {
        sleep(1);
    }
    http_server_stop(g_server);
    router_destroy(router);
    http_server_destroy(g_server);
    return 0;
}
