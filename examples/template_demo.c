#include "weblib.h"
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>

/* Global server instance for signal handling */
static http_server_t *g_server = NULL;
static volatile sig_atomic_t shutdown_requested = 0;

/* Signal handler for graceful shutdown */
void signal_handler(int signum) {
    (void)signum;
    shutdown_requested = 1;
}

/* Logging middleware */
bool logging_middleware(http_request_t *req, http_response_t *res) {
    (void)res;
    printf("[%s] %s\n", 
           req->method == HTTP_GET ? "GET" : 
           req->method == HTTP_POST ? "POST" : "OTHER",
           req->path);
    return true;
}

/* Homepage with template */
void handle_home(http_request_t *req, http_response_t *res) {
    (void)req;
    
    template_context_t *ctx = template_context_create();
    template_context_set(ctx, "title", "Modern C Web Library");
    template_context_set(ctx, "heading", "Welcome to Template Engine Demo");
    template_context_set(ctx, "description", "This page is rendered using the template engine!");
    template_context_set(ctx, "version", "1.0.0");
    
    const char *template = 
        "<!DOCTYPE html>\n"
        "<html>\n"
        "<head>\n"
        "    <title>{{ title }}</title>\n"
        "    <style>\n"
        "        body { font-family: Arial, sans-serif; max-width: 800px; margin: 50px auto; padding: 20px; }\n"
        "        h1 { color: #333; }\n"
        "        .info { background: #f0f0f0; padding: 15px; border-radius: 5px; }\n"
        "        .footer { margin-top: 50px; text-align: center; color: #666; }\n"
        "    </style>\n"
        "</head>\n"
        "<body>\n"
        "    <h1>{{ heading }}</h1>\n"
        "    <div class=\"info\">\n"
        "        <p>{{ description }}</p>\n"
        "        <p><strong>Version:</strong> {{ version }}</p>\n"
        "    </div>\n"
        "    <h2>Features</h2>\n"
        "    <ul>\n"
        "        <li>Simple variable substitution with {{ variable }} syntax</li>\n"
        "        <li>Efficient rendering with dynamic buffers</li>\n"
        "        <li>Easy integration with HTTP responses</li>\n"
        "        <li>File-based template loading support</li>\n"
        "    </ul>\n"
        "    <h2>API Endpoints</h2>\n"
        "    <ul>\n"
        "        <li><a href=\"/\">/ - Home Page (this page)</a></li>\n"
        "        <li><a href=\"/user\">/ user - User Profile Template</a></li>\n"
        "        <li><a href=\"/about\">/ about - About Page Template</a></li>\n"
        "    </ul>\n"
        "    <div class=\"footer\">\n"
        "        <p>Powered by Modern C Web Library Template Engine</p>\n"
        "    </div>\n"
        "</body>\n"
        "</html>";
    
    http_response_send_template(res, HTTP_OK, template, ctx);
    template_context_destroy(ctx);
}

/* User profile page */
void handle_user(http_request_t *req, http_response_t *res) {
    (void)req;
    
    template_context_t *ctx = template_context_create();
    template_context_set(ctx, "username", "Alice Johnson");
    template_context_set(ctx, "role", "Senior Developer");
    template_context_set(ctx, "email", "alice@example.com");
    template_context_set(ctx, "bio", "Passionate about building high-performance web applications in C.");
    
    const char *template = 
        "<!DOCTYPE html>\n"
        "<html>\n"
        "<head>\n"
        "    <title>User Profile - {{ username }}</title>\n"
        "    <style>\n"
        "        body { font-family: Arial, sans-serif; max-width: 800px; margin: 50px auto; padding: 20px; }\n"
        "        .profile { background: #e8f4f8; padding: 20px; border-radius: 10px; }\n"
        "        h1 { color: #2c3e50; }\n"
        "        .field { margin: 10px 0; }\n"
        "        .label { font-weight: bold; color: #34495e; }\n"
        "        a { color: #3498db; text-decoration: none; }\n"
        "    </style>\n"
        "</head>\n"
        "<body>\n"
        "    <h1>User Profile</h1>\n"
        "    <div class=\"profile\">\n"
        "        <div class=\"field\">\n"
        "            <span class=\"label\">Name:</span> {{ username }}\n"
        "        </div>\n"
        "        <div class=\"field\">\n"
        "            <span class=\"label\">Role:</span> {{ role }}\n"
        "        </div>\n"
        "        <div class=\"field\">\n"
        "            <span class=\"label\">Email:</span> {{ email }}\n"
        "        </div>\n"
        "        <div class=\"field\">\n"
        "            <span class=\"label\">Bio:</span> {{ bio }}\n"
        "        </div>\n"
        "    </div>\n"
        "    <p><a href=\"/\">← Back to Home</a></p>\n"
        "</body>\n"
        "</html>";
    
    http_response_send_template(res, HTTP_OK, template, ctx);
    template_context_destroy(ctx);
}

/* About page */
void handle_about(http_request_t *req, http_response_t *res) {
    (void)req;
    
    template_context_t *ctx = template_context_create();
    template_context_set(ctx, "library_name", "Modern C Web Library");
    template_context_set(ctx, "author", "Kamran Khan");
    template_context_set(ctx, "year", "2024");
    template_context_set(ctx, "license", "MIT License");
    
    const char *template = 
        "<!DOCTYPE html>\n"
        "<html>\n"
        "<head>\n"
        "    <title>About - {{ library_name }}</title>\n"
        "    <style>\n"
        "        body { font-family: Arial, sans-serif; max-width: 800px; margin: 50px auto; padding: 20px; }\n"
        "        .about-box { background: #fff3cd; padding: 20px; border-radius: 10px; border: 2px solid #ffc107; }\n"
        "        h1 { color: #856404; }\n"
        "        p { line-height: 1.6; }\n"
        "        a { color: #3498db; text-decoration: none; }\n"
        "    </style>\n"
        "</head>\n"
        "<body>\n"
        "    <h1>About {{ library_name }}</h1>\n"
        "    <div class=\"about-box\">\n"
        "        <p>{{ library_name }} is a modern, AI-assisted C library for building efficient and scalable web backends.</p>\n"
        "        <p><strong>Author:</strong> {{ author }}</p>\n"
        "        <p><strong>Year:</strong> {{ year }}</p>\n"
        "        <p><strong>License:</strong> {{ license }}</p>\n"
        "        <p>This library includes a powerful template engine that allows you to create dynamic HTML pages with ease.</p>\n"
        "    </div>\n"
        "    <h2>Key Features</h2>\n"
        "    <ul>\n"
        "        <li>HTTP Server with routing</li>\n"
        "        <li>JSON parser and serializer</li>\n"
        "        <li>Middleware support</li>\n"
        "        <li>Template engine (you're seeing it in action!)</li>\n"
        "        <li>Cross-platform compatibility</li>\n"
        "    </ul>\n"
        "    <p><a href=\"/\">← Back to Home</a></p>\n"
        "</body>\n"
        "</html>";
    
    http_response_send_template(res, HTTP_OK, template, ctx);
    template_context_destroy(ctx);
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
    
    /* Add middleware */
    router_use_middleware(router, logging_middleware);
    
    /* Add routes */
    router_add_route(router, HTTP_GET, "/", handle_home);
    router_add_route(router, HTTP_GET, "/user", handle_user);
    router_add_route(router, HTTP_GET, "/about", handle_about);
    
    /* Set router and start server */
    http_server_set_router(g_server, router);
    
    printf("Template Engine Demo Server\n");
    printf("============================\n");
    printf("Server starting on http://localhost:%d\n", port);
    printf("Press Ctrl+C to stop\n\n");
    printf("Available endpoints:\n");
    printf("  http://localhost:%d/       - Home page with template\n", port);
    printf("  http://localhost:%d/user   - User profile template\n", port);
    printf("  http://localhost:%d/about  - About page template\n", port);
    printf("\n");
    
    /* Start server (blocking call) */
    if (http_server_listen(g_server, port) != 0) {
        fprintf(stderr, "Failed to start server on port %d\n", port);
        router_destroy(router);
        http_server_destroy(g_server);
        return 1;
    }
    
    /* Cleanup */
    router_destroy(router);
    http_server_destroy(g_server);
    
    printf("\nServer stopped\n");
    return 0;
}
