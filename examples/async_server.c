#include "weblib.h"
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>

/* Global server instance for signal handling */
static http_server_t *g_server = NULL;
static event_loop_t *g_event_loop = NULL;
static volatile sig_atomic_t shutdown_requested = 0;

/* Signal handler for graceful shutdown */
void signal_handler(int signum) {
    (void)signum;
    shutdown_requested = 1;
    if (g_event_loop) {
        event_loop_stop(g_event_loop);
    }
}

/* Route handlers */
void handle_root(http_request_t *req, http_response_t *res) {
    (void)req;
    http_response_send_text(res, HTTP_OK, "Welcome to Async HTTP Server!");
}

void handle_hello(http_request_t *req, http_response_t *res) {
    (void)req;
    http_response_send_text(res, HTTP_OK, "Hello from Async I/O!");
}

void handle_json(http_request_t *req, http_response_t *res) {
    (void)req;
    
    /* Create JSON response */
    json_value_t *json = json_object_create();
    json_object_set(json, "message", json_string_create("Async I/O is working!"));
    json_object_set(json, "mode", json_string_create("async"));
    json_object_set(json, "status", json_string_create("success"));
    
    http_response_send_json(res, HTTP_OK, json);
    json_value_free(json);
}

void handle_info(http_request_t *req, http_response_t *res) {
    (void)req;
    
    json_value_t *json = json_object_create();
    json_object_set(json, "server", json_string_create("Modern C Web Library"));
    json_object_set(json, "version", json_string_create("1.0.0"));
    json_object_set(json, "io_mode", json_string_create("async"));
    json_object_set(json, "event_loop", json_string_create("enabled"));
    
    /* Event loop backend is platform-dependent and auto-selected */
    #ifdef __linux__
    json_object_set(json, "platform", json_string_create("Linux"));
    #elif defined(__APPLE__)
    json_object_set(json, "platform", json_string_create("macOS"));
    #elif defined(__FreeBSD__)
    json_object_set(json, "platform", json_string_create("FreeBSD"));
    #else
    json_object_set(json, "platform", json_string_create("Other"));
    #endif
    
    http_response_send_json(res, HTTP_OK, json);
    json_value_free(json);
}

/* Middleware example - logging */
bool logging_middleware(http_request_t *req, http_response_t *res) {
    (void)res;
    
    const char *method_str = "UNKNOWN";
    switch (req->method) {
        case HTTP_GET: method_str = "GET"; break;
        case HTTP_POST: method_str = "POST"; break;
        case HTTP_PUT: method_str = "PUT"; break;
        case HTTP_DELETE: method_str = "DELETE"; break;
        case HTTP_PATCH: method_str = "PATCH"; break;
        case HTTP_HEAD: method_str = "HEAD"; break;
        case HTTP_OPTIONS: method_str = "OPTIONS"; break;
    }
    
    printf("[ASYNC] [%s] %s\n", method_str, req->path);
    
    return true; /* Continue to next middleware/handler */
}

int main(int argc, char *argv[]) {
    uint16_t port = 8080;
    
    /* Parse command line arguments */
    if (argc > 1) {
        port = (uint16_t)atoi(argv[1]);
        if (port == 0) {
            fprintf(stderr, "Invalid port number\n");
            return 1;
        }
    }
    
    /* Setup signal handlers */
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    /* Create server */
    g_server = http_server_create();
    if (!g_server) {
        fprintf(stderr, "Failed to create server\n");
        return 1;
    }
    
    /* Enable async I/O mode */
    printf("Enabling async I/O mode...\n");
    if (http_server_set_async(g_server, true) < 0) {
        fprintf(stderr, "Failed to enable async mode\n");
        http_server_destroy(g_server);
        return 1;
    }
    
    /* Get event loop reference */
    g_event_loop = http_server_get_event_loop(g_server);
    if (!g_event_loop) {
        fprintf(stderr, "Failed to get event loop\n");
        http_server_destroy(g_server);
        return 1;
    }
    
    /* Create router */
    router_t *router = router_create();
    if (!router) {
        fprintf(stderr, "Failed to create router\n");
        http_server_destroy(g_server);
        return 1;
    }
    
    /* Add middleware */
    router_use_middleware(router, logging_middleware);
    
    /* Add routes */
    router_add_route(router, HTTP_GET, "/", handle_root);
    router_add_route(router, HTTP_GET, "/hello", handle_hello);
    router_add_route(router, HTTP_GET, "/api/json", handle_json);
    router_add_route(router, HTTP_GET, "/info", handle_info);
    
    /* Set router on server */
    http_server_set_router(g_server, router);
    
    /* Start server */
    printf("\n");
    printf("===========================================\n");
    printf("  Async HTTP Server - Modern C Web Library\n");
    printf("===========================================\n");
    printf("\n");
    printf("Starting async HTTP server on port %d...\n", port);
    printf("\n");
    printf("Platform: ");
#ifdef __linux__
    printf("Linux (using epoll or poll)\n");
#elif defined(__APPLE__)
    printf("macOS (using kqueue)\n");
#elif defined(__FreeBSD__)
    printf("FreeBSD (using kqueue)\n");
#else
    printf("Other (using poll)\n");
#endif
    printf("\n");
    printf("Available endpoints:\n");
    printf("  GET  /              - Welcome message\n");
    printf("  GET  /hello         - Hello World\n");
    printf("  GET  /api/json      - JSON response example\n");
    printf("  GET  /info          - Server information\n");
    printf("\n");
    printf("Press Ctrl+C to stop the server.\n");
    printf("===========================================\n\n");
    
    /* Start listening (this will run the event loop) */
    if (http_server_listen(g_server, port) < 0) {
        fprintf(stderr, "Failed to start server\n");
        router_destroy(router);
        http_server_destroy(g_server);
        return 1;
    }
    
    /* Server stopped */
    printf("\nShutting down...\n");
    
    /* Cleanup */
    router_destroy(router);
    http_server_destroy(g_server);
    
    printf("Server stopped successfully\n");
    
    return 0;
}
