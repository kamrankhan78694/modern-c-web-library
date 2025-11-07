#include "weblib.h"
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>

/* Global server instance for signal handling */
static http_server_t *g_server = NULL;
static volatile sig_atomic_t shutdown_requested = 0;

/* Signal handler for graceful shutdown */
void signal_handler(int signum) {
    (void)signum;
    shutdown_requested = 1;
}

/* API route handlers */
void handle_api_info(http_request_t *req, http_response_t *res) {
    (void)req;
    
    json_value_t *json = json_object_create();
    json_object_set(json, "name", json_string_create("Modern C Web Library"));
    json_object_set(json, "version", json_string_create("1.0.0"));
    json_object_set(json, "features", json_string_create("Static file serving, routing, middleware, JSON"));
    
    http_response_send_json(res, HTTP_OK, json);
    json_value_free(json);
}

void handle_hello(http_request_t *req, http_response_t *res) {
    (void)req;
    http_response_send_text(res, HTTP_OK, "Hello from the API!");
}

void handle_user(http_request_t *req, http_response_t *res) {
    const char *user_id = http_request_get_param(req, "id");
    
    json_value_t *json = json_object_create();
    json_object_set(json, "id", json_string_create(user_id ? user_id : "unknown"));
    json_object_set(json, "name", json_string_create("John Doe"));
    json_object_set(json, "email", json_string_create("john@example.com"));
    
    http_response_send_json(res, HTTP_OK, json);
    json_value_free(json);
}

/* Static file middleware */
bool static_files_middleware(http_request_t *req, http_response_t *res) {
    /* Serve files from ./public directory */
    return static_file_handler(req, res, "examples/public");
}

/* Logging middleware */
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
    
    printf("[%s] %s\n", method_str, req->path);
    return true;
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
    
    /* Create router */
    router_t *router = router_create();
    if (!router) {
        fprintf(stderr, "Failed to create router\n");
        http_server_destroy(g_server);
        return 1;
    }
    
    /* Add middleware */
    router_use_middleware(router, logging_middleware);
    router_use_middleware(router, static_files_middleware);
    
    /* Add API routes */
    router_add_route(router, HTTP_GET, "/api/json", handle_api_info);
    router_add_route(router, HTTP_GET, "/hello", handle_hello);
    router_add_route(router, HTTP_GET, "/users/:id", handle_user);
    
    /* Set router on server */
    http_server_set_router(g_server, router);
    
    /* Start server */
    printf("Starting HTTP server with static file serving on port %d...\n", port);
    printf("\nStatic files are served from: ./examples/public/\n");
    printf("\nAvailable endpoints:\n");
    printf("  GET  /               - Static files (index.html, styles.css, script.js)\n");
    printf("  GET  /api/json       - JSON API info\n");
    printf("  GET  /hello          - Hello from API\n");
    printf("  GET  /users/:id      - User info with route parameters\n");
    printf("\nOpen http://localhost:%d in your browser!\n", port);
    printf("Press Ctrl+C to stop the server.\n\n");
    
    if (http_server_listen(g_server, port) < 0) {
        fprintf(stderr, "Failed to start server\n");
        router_destroy(router);
        http_server_destroy(g_server);
        return 1;
    }
    
    /* Wait for server to stop */
    while (!shutdown_requested) {
        sleep(1);
    }
    
    /* Shutdown */
    printf("\nShutting down...\n");
    http_server_stop(g_server);
    
    /* Cleanup */
    router_destroy(router);
    http_server_destroy(g_server);
    
    printf("Server stopped successfully\n");
    
    return 0;
}
