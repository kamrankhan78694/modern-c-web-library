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

/* Route handlers */
void handle_root(http_request_t *req, http_response_t *res) {
    (void)req;
    http_response_send_text(res, HTTP_OK, "Welcome to Modern C Web Library!");
}

void handle_hello(http_request_t *req, http_response_t *res) {
    (void)req;
    http_response_send_text(res, HTTP_OK, "Hello, World!");
}

void handle_json(http_request_t *req, http_response_t *res) {
    (void)req;
    
    /* Create JSON response */
    json_value_t *json = json_object_create();
    json_object_set(json, "message", json_string_create("Hello from JSON API"));
    json_object_set(json, "version", json_string_create("1.0.0"));
    json_object_set(json, "status", json_string_create("success"));
    
    http_response_send_json(res, HTTP_OK, json);
    json_value_free(json);
}

void handle_user(http_request_t *req, http_response_t *res) {
    /* This demonstrates route parameters like /users/:id */
    const char *user_id = http_request_get_param(req, "id");
    
    json_value_t *json = json_object_create();
    json_object_set(json, "id", json_string_create(user_id ? user_id : "unknown"));
    json_object_set(json, "name", json_string_create("John Doe"));
    json_object_set(json, "email", json_string_create("john@example.com"));
    
    http_response_send_json(res, HTTP_OK, json);
    json_value_free(json);
}

void handle_post_data(http_request_t *req, http_response_t *res) {
    /* Echo back received data */
    if (req->body && req->body_length > 0) {
        json_value_t *json = json_object_create();
        json_object_set(json, "received", json_string_create("true"));
        json_object_set(json, "length", json_number_create((double)req->body_length));
        
        http_response_send_json(res, HTTP_CREATED, json);
        json_value_free(json);
    } else {
        http_response_send_text(res, HTTP_BAD_REQUEST, "No data received");
    }
}

void handle_form_submit(http_request_t *req, http_response_t *res) {
    /* Handle URL-encoded form data */
    const char *username = http_request_get_form_field(req, "username");
    const char *email = http_request_get_form_field(req, "email");
    const char *message = http_request_get_form_field(req, "message");
    
    json_value_t *json = json_object_create();
    json_object_set(json, "status", json_string_create("success"));
    json_object_set(json, "username", json_string_create(username ? username : "not provided"));
    json_object_set(json, "email", json_string_create(email ? email : "not provided"));
    json_object_set(json, "message", json_string_create(message ? message : "not provided"));
    
    http_response_send_json(res, HTTP_OK, json);
    json_value_free(json);
}

void handle_file_upload(http_request_t *req, http_response_t *res) {
    /* Handle multipart file upload */
    const char *description = http_request_get_form_field(req, "description");
    
    const char *filename = NULL;
    const char *content_type = NULL;
    size_t file_size = 0;
    const char *file_data = http_request_get_file(req, "file", &filename, &content_type, &file_size);
    
    json_value_t *json = json_object_create();
    
    if (file_data) {
        json_object_set(json, "status", json_string_create("success"));
        json_object_set(json, "filename", json_string_create(filename ? filename : "unknown"));
        json_object_set(json, "content_type", json_string_create(content_type ? content_type : "unknown"));
        json_object_set(json, "size", json_number_create((double)file_size));
        json_object_set(json, "description", json_string_create(description ? description : "none"));
        
        http_response_send_json(res, HTTP_OK, json);
    } else {
        json_object_set(json, "error", json_string_create("No file uploaded"));
        json_object_set(json, "status", json_string_create("error"));
        
        http_response_send_json(res, HTTP_BAD_REQUEST, json);
    }
    
    json_value_free(json);
}

void handle_not_found(http_request_t *req, http_response_t *res) {
    (void)req;
    
    json_value_t *json = json_object_create();
    json_object_set(json, "error", json_string_create("Not Found"));
    json_object_set(json, "status", json_number_create(404));
    
    http_response_send_json(res, HTTP_NOT_FOUND, json);
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
    
    printf("[%s] %s\n", method_str, req->path);
    
    return true; /* Continue to next middleware/handler */
}

/* Middleware example - CORS headers */
bool cors_middleware(http_request_t *req, http_response_t *res) {
    (void)req;
    
    http_response_set_header(res, "Access-Control-Allow-Origin", "*");
    http_response_set_header(res, "Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, PATCH, OPTIONS");
    http_response_set_header(res, "Access-Control-Allow-Headers", "Content-Type, Authorization");
    
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
    
    /* Create router */
    router_t *router = router_create();
    if (!router) {
        fprintf(stderr, "Failed to create router\n");
        http_server_destroy(g_server);
        return 1;
    }
    
    /* Add middlewares */
    router_use_middleware(router, logging_middleware);
    router_use_middleware(router, cors_middleware);
    
    /* Add routes */
    router_add_route(router, HTTP_GET, "/", handle_root);
    router_add_route(router, HTTP_GET, "/hello", handle_hello);
    router_add_route(router, HTTP_GET, "/api/json", handle_json);
    router_add_route(router, HTTP_GET, "/users/:id", handle_user);
    router_add_route(router, HTTP_POST, "/api/data", handle_post_data);
    router_add_route(router, HTTP_POST, "/api/form", handle_form_submit);
    router_add_route(router, HTTP_POST, "/api/upload", handle_file_upload);
    router_add_route(router, HTTP_GET, "/404", handle_not_found);
    
    /* Set router on server */
    http_server_set_router(g_server, router);
    
    /* Start server */
    printf("Starting HTTP server on port %d...\n", port);
    printf("Available endpoints:\n");
    printf("  GET  /              - Welcome message\n");
    printf("  GET  /hello         - Hello World\n");
    printf("  GET  /api/json      - JSON response example\n");
    printf("  GET  /users/:id     - User info with route parameters\n");
    printf("  POST /api/data      - Echo posted data\n");
    printf("  POST /api/form      - Handle URL-encoded form data\n");
    printf("  POST /api/upload    - Handle multipart file upload\n");
    printf("\nPress Ctrl+C to stop the server.\n\n");
    
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
    
    /* Shutdown initiated */
    printf("\nShutting down...\n");
    http_server_stop(g_server);
    
    /* Cleanup */
    router_destroy(router);
    http_server_destroy(g_server);
    
    printf("Server stopped successfully\n");
    
    return 0;
}
