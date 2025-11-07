#include "weblib.h"
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <string.h>
#include <time.h>

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
    http_response_send_text(res, HTTP_OK, "Welcome to Secure Modern C Web Library (HTTPS)!");
}

void handle_secure_info(http_request_t *req, http_response_t *res) {
    (void)req;
    
    json_value_t *json = json_object_create();
    json_object_set(json, "message", json_string_create("This is a secure HTTPS connection"));
    json_object_set(json, "protocol", json_string_create("HTTPS"));
    json_object_set(json, "encrypted", json_bool_create(true));
    
    http_response_send_json(res, HTTP_OK, json);
    json_value_free(json);
}

void handle_api_data(http_request_t *req, http_response_t *res) {
    (void)req;
    
    json_value_t *json = json_object_create();
    json_object_set(json, "status", json_string_create("success"));
    json_object_set(json, "timestamp", json_number_create((double)time(NULL)));
    json_object_set(json, "secure", json_bool_create(true));
    
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
    
    printf("[HTTPS] [%s] %s\n", method_str, req->path);
    
    return true;
}

int main(int argc, char *argv[]) {
    uint16_t port = 8443;  /* Default HTTPS port */
    const char *cert_file = "certs/server.crt";
    const char *key_file = "certs/server.key";
    
    /* Parse command line arguments */
    if (argc > 1) {
        port = (uint16_t)atoi(argv[1]);
        if (port == 0) {
            fprintf(stderr, "Invalid port number\n");
            return 1;
        }
    }
    if (argc > 2) {
        cert_file = argv[2];
    }
    if (argc > 3) {
        key_file = argv[3];
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
    
    /* Configure SSL */
    ssl_config_t ssl_config = {
        .cert_file = cert_file,
        .key_file = key_file,
        .ca_file = NULL,
        .verify_peer = false,
        .min_tls_version = 0  /* Use OpenSSL defaults */
    };
    
    if (http_server_enable_ssl(g_server, &ssl_config) < 0) {
        fprintf(stderr, "Failed to enable SSL. Make sure certificate files exist.\n");
        fprintf(stderr, "Run './generate_cert.sh' to create test certificates.\n");
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
    router_add_route(router, HTTP_GET, "/secure", handle_secure_info);
    router_add_route(router, HTTP_GET, "/api/data", handle_api_data);
    
    /* Set router on server */
    http_server_set_router(g_server, router);
    
    /* Start server */
    printf("Starting HTTPS server on port %d...\n", port);
    printf("Certificate: %s\n", cert_file);
    printf("Private Key: %s\n", key_file);
    printf("\nAvailable endpoints:\n");
    printf("  GET  https://localhost:%d/           - Welcome message\n", port);
    printf("  GET  https://localhost:%d/secure     - Secure connection info\n", port);
    printf("  GET  https://localhost:%d/api/data   - JSON API endpoint\n", port);
    printf("\nNote: Using self-signed certificate. Browser will show security warning.\n");
    printf("      Use 'curl -k' to bypass certificate verification in curl.\n");
    printf("\nPress Ctrl+C to stop the server.\n\n");
    
    if (http_server_listen(g_server, port) < 0) {
        fprintf(stderr, "Failed to start server\n");
        router_destroy(router);
        http_server_destroy(g_server);
        return 1;
    }
    
    /* Wait for shutdown signal */
    while (!shutdown_requested) {
        sleep(1);
    }
    
    /* Shutdown initiated */
    printf("\nShutting down...\n");
    http_server_stop(g_server);
    
    /* Cleanup */
    router_destroy(router);
    http_server_destroy(g_server);
    ssl_library_cleanup();
    
    printf("Server stopped successfully\n");
    
    return 0;
}
