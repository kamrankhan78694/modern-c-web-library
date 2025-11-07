#include "weblib.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>

/* Global server instance for signal handling */
static http_server_t *g_server = NULL;
static session_store_t *g_session_store = NULL;
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

void handle_not_found(http_request_t *req, http_response_t *res) {
    (void)req;
    
    json_value_t *json = json_object_create();
    json_object_set(json, "error", json_string_create("Not Found"));
    json_object_set(json, "status", json_number_create(404));
    
    http_response_send_json(res, HTTP_NOT_FOUND, json);
    json_value_free(json);
}

/* Session demonstration handlers */
void handle_session_login(http_request_t *req, http_response_t *res) {
    (void)req;
    
    /* Create a new session */
    char *session_id = session_create(g_session_store, 3600); /* 1 hour expiry */
    if (!session_id) {
        http_response_send_text(res, HTTP_INTERNAL_ERROR, "Failed to create session");
        return;
    }
    
    /* Get session and set user data */
    session_t *session = session_get(g_session_store, session_id);
    if (session) {
        session_set(session, "user_id", "42");
        session_set(session, "username", "demo_user");
        session_set(session, "email", "demo@example.com");
    }
    
    /* Set session cookie */
    session_set_cookie(res, session_id, 3600, "/");
    
    /* Send response */
    json_value_t *json = json_object_create();
    json_object_set(json, "message", json_string_create("Logged in successfully"));
    json_object_set(json, "session_id", json_string_create(session_id));
    
    http_response_send_json(res, HTTP_OK, json);
    json_value_free(json);
    free(session_id);
}

void handle_session_info(http_request_t *req, http_response_t *res) {
    /* Get session from request */
    session_t *session = session_from_request(g_session_store, req);
    
    if (!session) {
        json_value_t *json = json_object_create();
        json_object_set(json, "error", json_string_create("No active session"));
        json_object_set(json, "message", json_string_create("Please login first"));
        
        http_response_send_json(res, HTTP_UNAUTHORIZED, json);
        json_value_free(json);
        return;
    }
    
    /* Get session data */
    const char *user_id = session_get_data(session, "user_id");
    const char *username = session_get_data(session, "username");
    const char *email = session_get_data(session, "email");
    
    /* Build response */
    json_value_t *json = json_object_create();
    json_object_set(json, "session_id", json_string_create(session_get_id(session)));
    json_object_set(json, "user_id", json_string_create(user_id ? user_id : "N/A"));
    json_object_set(json, "username", json_string_create(username ? username : "N/A"));
    json_object_set(json, "email", json_string_create(email ? email : "N/A"));
    json_object_set(json, "expired", json_bool_create(session_is_expired(session)));
    
    http_response_send_json(res, HTTP_OK, json);
    json_value_free(json);
}

void handle_session_logout(http_request_t *req, http_response_t *res) {
    /* Get session from request */
    session_t *session = session_from_request(g_session_store, req);
    
    if (session) {
        const char *session_id = session_get_id(session);
        char *id_copy = strdup(session_id);
        
        /* Destroy session */
        session_destroy(g_session_store, id_copy);
        
        /* Clear session cookie */
        session_set_cookie(res, id_copy, -1, "/");
        
        free(id_copy);
    }
    
    json_value_t *json = json_object_create();
    json_object_set(json, "message", json_string_create("Logged out successfully"));
    
    http_response_send_json(res, HTTP_OK, json);
    json_value_free(json);
}

void handle_visit_count(http_request_t *req, http_response_t *res) {
    /* Get or create session */
    session_t *session = session_from_request(g_session_store, req);
    char *session_id = NULL;
    
    if (!session) {
        /* Create new session */
        session_id = session_create(g_session_store, 0); /* Session cookie */
        if (session_id) {
            session = session_get(g_session_store, session_id);
            session_set_cookie(res, session_id, 0, "/");
        }
    }
    
    int visit_count = 1;
    
    if (session) {
        /* Get current visit count */
        const char *count_str = session_get_data(session, "visit_count");
        if (count_str) {
            visit_count = atoi(count_str) + 1;
        }
        
        /* Update visit count */
        char count_buffer[32];
        snprintf(count_buffer, sizeof(count_buffer), "%d", visit_count);
        session_set(session, "visit_count", count_buffer);
    }
    
    /* Send response */
    json_value_t *json = json_object_create();
    json_object_set(json, "message", json_string_create("Visit tracked"));
    json_object_set(json, "visit_count", json_number_create((double)visit_count));
    
    http_response_send_json(res, HTTP_OK, json);
    json_value_free(json);
    
    if (session_id) {
        free(session_id);
    }
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
    
    /* Create session store */
    g_session_store = session_store_create();
    if (!g_session_store) {
        fprintf(stderr, "Failed to create session store\n");
        return 1;
    }
    
    /* Create server */
    g_server = http_server_create();
    if (!g_server) {
        fprintf(stderr, "Failed to create server\n");
        session_store_destroy(g_session_store);
        return 1;
    }
    
    /* Create router */
    router_t *router = router_create();
    if (!router) {
        fprintf(stderr, "Failed to create router\n");
        http_server_destroy(g_server);
        session_store_destroy(g_session_store);
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
    router_add_route(router, HTTP_GET, "/404", handle_not_found);
    
    /* Session routes */
    router_add_route(router, HTTP_POST, "/session/login", handle_session_login);
    router_add_route(router, HTTP_GET, "/session/info", handle_session_info);
    router_add_route(router, HTTP_POST, "/session/logout", handle_session_logout);
    router_add_route(router, HTTP_GET, "/session/visits", handle_visit_count);
    
    /* Set router on server */
    http_server_set_router(g_server, router);
    
    /* Start server */
    printf("Starting HTTP server on port %d...\n", port);
    printf("Available endpoints:\n");
    printf("  GET  /                   - Welcome message\n");
    printf("  GET  /hello              - Hello World\n");
    printf("  GET  /api/json           - JSON response example\n");
    printf("  GET  /users/:id          - User info with route parameters\n");
    printf("  POST /api/data           - Echo posted data\n");
    printf("\nSession Management:\n");
    printf("  POST /session/login      - Create a new session and login\n");
    printf("  GET  /session/info       - Get current session information\n");
    printf("  POST /session/logout     - Logout and destroy session\n");
    printf("  GET  /session/visits     - Track visit count with session\n");
    printf("\nPress Ctrl+C to stop the server.\n\n");
    
    if (http_server_listen(g_server, port) < 0) {
        fprintf(stderr, "Failed to start server\n");
        router_destroy(router);
        http_server_destroy(g_server);
        session_store_destroy(g_session_store);
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
    session_store_destroy(g_session_store);
    
    printf("Server stopped successfully\n");
    
    return 0;
}
