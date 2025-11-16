/* Async WebSocket Echo Server Example */
#include "weblib.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>

static http_server_t *g_server = NULL;
static volatile bool server_running = true;

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
    const char *html = "<html><body><h1>Async WebSocket Echo</h1><script>const ws=new WebSocket('ws://";
    char buf[512];
    snprintf(buf, sizeof(buf), "%s" "" "" , html); /* minimal placeholder */
    http_response_set_header(res, "Content-Type", "text/html");
    http_response_send_text(res, HTTP_OK, "Async WebSocket Echo Server. Connect with ws://host:port/ws");
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