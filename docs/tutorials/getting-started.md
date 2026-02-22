# Getting Started with Modern C Web Library

Welcome to the Modern C Web Library (MCWL)! This tutorial will guide you through building your first web server using this powerful, zero-dependency C framework.

## Introduction

The Modern C Web Library is a pure C web framework designed for building high-performance HTTP and WebSocket servers. With zero external dependencies and a clean, intuitive API, MCWL makes it easy to create robust web backends in C.

Key features include:
- **Pure C** - No dependencies, just standard C libraries
- **High Performance** - Designed for speed and efficiency
- **Cross-Platform** - Works on Linux, macOS, and Windows
- **Easy to Use** - Clean API inspired by modern web frameworks
- **Built-in Features** - Routing, middleware, JSON parsing, WebSockets, and more

## Prerequisites

Before you begin, make sure you have:

- **C Compiler** - GCC 7+, Clang 5+, or MSVC 2017+
- **CMake** - Version 3.10 or higher
- **Git** - For cloning the repository
- **Basic C knowledge** - Familiarity with C programming

## Installation

### Step 1: Clone the Repository

```bash
git clone https://github.com/kamrankhan78694/modern-c-web-library.git
cd modern-c-web-library
```

### Step 2: Build the Library

```bash
mkdir build
cd build
cmake ..
cmake --build .
```

This will build the library and create the static library file (`libmcwl.a` on Unix systems or `mcwl.lib` on Windows).

### Step 3: Install (Optional)

```bash
sudo cmake --install .
```

This installs the library and headers to your system directories.

## Your First Server

Let's create a minimal "Hello, World!" HTTP server. Create a new file called `hello_server.c`:

```c
#include <mcwl/http_server.h>
#include <mcwl/router.h>
#include <stdio.h>
#include <signal.h>

static http_server_t *server = NULL;

void handle_sigint(int sig) {
    (void)sig;
    if (server) {
        http_server_destroy(server);
        server = NULL;
    }
    exit(0);
}

void hello_handler(http_request_t *req, http_response_t *res) {
    (void)req; // Unused parameter
    http_response_send_text(res, HTTP_OK, "Hello, World!");
}

int main(void) {
    signal(SIGINT, handle_sigint);
    
    // Create server and router
    server = http_server_create();
    router_t *router = router_create();
    
    // Add route
    router_add_route(router, "GET", "/", hello_handler);
    
    // Attach router to server
    http_server_set_router(server, router);
    
    // Start server
    printf("Server running on http://localhost:8080\n");
    http_server_listen(server, 8080);
    
    // Cleanup
    http_server_destroy(server);
    router_destroy(router);
    
    return 0;
}
```

### Compile and Run

```bash
gcc -o hello_server hello_server.c -lmcwl -lpthread
./hello_server
```

Visit `http://localhost:8080` in your browser or use curl:

```bash
curl http://localhost:8080
# Output: Hello, World!
```

## Adding Routes

Let's expand our server with multiple routes and route parameters:

```c
#include <mcwl/http_server.h>
#include <mcwl/router.h>
#include <stdio.h>
#include <signal.h>

static http_server_t *server = NULL;

void handle_sigint(int sig) {
    (void)sig;
    if (server) {
        http_server_destroy(server);
        server = NULL;
    }
    exit(0);
}

void home_handler(http_request_t *req, http_response_t *res) {
    (void)req;
    http_response_send_text(res, HTTP_OK, "Welcome to MCWL!");
}

void about_handler(http_request_t *req, http_response_t *res) {
    (void)req;
    http_response_send_text(res, HTTP_OK, "Modern C Web Library v1.0.0");
}

void user_handler(http_request_t *req, http_response_t *res) {
    const char *user_id = http_request_get_param(req, "id");
    
    if (user_id) {
        char message[256];
        snprintf(message, sizeof(message), "User ID: %s", user_id);
        http_response_send_text(res, HTTP_OK, message);
    } else {
        http_response_send_text(res, HTTP_BAD_REQUEST, "Missing user ID");
    }
}

int main(void) {
    signal(SIGINT, handle_sigint);
    
    server = http_server_create();
    router_t *router = router_create();
    
    // Add multiple routes
    router_add_route(router, "GET", "/", home_handler);
    router_add_route(router, "GET", "/about", about_handler);
    router_add_route(router, "GET", "/users/:id", user_handler);
    
    http_server_set_router(server, router);
    
    printf("Server running on http://localhost:8080\n");
    http_server_listen(server, 8080);
    
    http_server_destroy(server);
    router_destroy(router);
    
    return 0;
}
```

### Test the Routes

```bash
curl http://localhost:8080/
# Output: Welcome to MCWL!

curl http://localhost:8080/about
# Output: Modern C Web Library v1.0.0

curl http://localhost:8080/users/42
# Output: User ID: 42
```

## JSON Responses

MCWL includes a built-in JSON library. Here's how to return JSON responses:

```c
#include <mcwl/http_server.h>
#include <mcwl/router.h>
#include <mcwl/json.h>
#include <stdio.h>
#include <signal.h>

static http_server_t *server = NULL;

void handle_sigint(int sig) {
    (void)sig;
    if (server) {
        http_server_destroy(server);
        server = NULL;
    }
    exit(0);
}

void api_status_handler(http_request_t *req, http_response_t *res) {
    (void)req;
    
    // Create JSON object
    json_value_t *json = json_object_create();
    json_object_set(json, "status", json_string_create("ok"));
    json_object_set(json, "version", json_string_create("1.0.0"));
    json_object_set(json, "uptime", json_number_create(12345));
    
    // Send JSON response
    http_response_send_json(res, HTTP_OK, json);
    
    // Cleanup
    json_value_free(json);
}

void api_user_handler(http_request_t *req, http_response_t *res) {
    const char *user_id = http_request_get_param(req, "id");
    
    json_value_t *json = json_object_create();
    json_object_set(json, "id", json_string_create(user_id ? user_id : "unknown"));
    json_object_set(json, "name", json_string_create("John Doe"));
    json_object_set(json, "email", json_string_create("john@example.com"));
    json_object_set(json, "active", json_boolean_create(true));
    
    http_response_send_json(res, HTTP_OK, json);
    json_value_free(json);
}

int main(void) {
    signal(SIGINT, handle_sigint);
    
    server = http_server_create();
    router_t *router = router_create();
    
    router_add_route(router, "GET", "/api/status", api_status_handler);
    router_add_route(router, "GET", "/api/users/:id", api_user_handler);
    
    http_server_set_router(server, router);
    
    printf("API server running on http://localhost:8080\n");
    http_server_listen(server, 8080);
    
    http_server_destroy(server);
    router_destroy(router);
    
    return 0;
}
```

### Test JSON Endpoints

```bash
curl http://localhost:8080/api/status
# Output: {"status":"ok","version":"1.0.0","uptime":12345}

curl http://localhost:8080/api/users/123
# Output: {"id":"123","name":"John Doe","email":"john@example.com","active":true}
```

## Adding Middleware

Middleware functions run before your route handlers, perfect for logging, authentication, or request validation:

```c
#include <mcwl/http_server.h>
#include <mcwl/router.h>
#include <stdio.h>
#include <time.h>
#include <signal.h>

static http_server_t *server = NULL;

void handle_sigint(int sig) {
    (void)sig;
    if (server) {
        http_server_destroy(server);
        server = NULL;
    }
    exit(0);
}

// Logging middleware
bool logging_middleware(http_request_t *req, http_response_t *res) {
    (void)res; // Unused parameter
    
    time_t now = time(NULL);
    char *timestamp = ctime(&now);
    timestamp[24] = '\0'; // Remove newline
    
    const char *method = http_request_get_method(req);
    const char *path = http_request_get_path(req);
    
    printf("[%s] %s %s\n", timestamp, method, path);
    
    return true; // Continue to next middleware/handler
}

// Authentication middleware (example)
bool auth_middleware(http_request_t *req, http_response_t *res) {
    const char *auth_header = http_request_get_header(req, "Authorization");
    
    if (!auth_header || strcmp(auth_header, "Bearer secret-token") != 0) {
        http_response_send_text(res, HTTP_UNAUTHORIZED, "Unauthorized");
        return false; // Stop processing
    }
    
    return true; // Continue to handler
}

void protected_handler(http_request_t *req, http_response_t *res) {
    (void)req;
    http_response_send_text(res, HTTP_OK, "This is a protected resource!");
}

void public_handler(http_request_t *req, http_response_t *res) {
    (void)req;
    http_response_send_text(res, HTTP_OK, "This is a public resource!");
}

int main(void) {
    signal(SIGINT, handle_sigint);
    
    server = http_server_create();
    router_t *router = router_create();
    
    // Add global middleware
    router_use_middleware(router, logging_middleware);
    
    // Add routes
    router_add_route(router, "GET", "/public", public_handler);
    router_add_route(router, "GET", "/protected", protected_handler);
    
    // Add route-specific middleware (if supported)
    router_add_route_with_middleware(router, "GET", "/protected", protected_handler, auth_middleware);
    
    http_server_set_router(server, router);
    
    printf("Server with middleware running on http://localhost:8080\n");
    http_server_listen(server, 8080);
    
    http_server_destroy(server);
    router_destroy(router);
    
    return 0;
}
```

### Test Middleware

```bash
# Public route - no authentication needed
curl http://localhost:8080/public
# Output: This is a public resource!

# Protected route - without authentication
curl http://localhost:8080/protected
# Output: Unauthorized

# Protected route - with authentication
curl -H "Authorization: Bearer secret-token" http://localhost:8080/protected
# Output: This is a protected resource!
```

## Running and Testing

### Build Your Application

```bash
gcc -o myapp myapp.c -lmcwl -lpthread
```

### Run the Server

```bash
./myapp
```

### Test with curl

```bash
# Basic GET request
curl http://localhost:8080/

# GET with route parameters
curl http://localhost:8080/users/42

# POST request with JSON
curl -X POST http://localhost:8080/api/users \
  -H "Content-Type: application/json" \
  -d '{"name":"Alice","email":"alice@example.com"}'

# Custom headers
curl -H "Authorization: Bearer token123" http://localhost:8080/api/protected
```

### Debug Mode

For development, you can enable debug logging:

```c
http_server_set_debug(server, true);
```

## Next Steps

Congratulations! You've built your first web server with MCWL. Here's what to explore next:

### Tutorials
- **[Building a REST API](rest-api-tutorial.md)** - Create a full CRUD API with database integration
- **[WebSocket Chat Server](websocket-tutorial.md)** - Build real-time applications with WebSockets
- **[Async I/O](async-io-tutorial.md)** - Leverage non-blocking I/O for high concurrency

### Documentation
- **[API Reference](../api-reference.md)** - Complete API documentation
- **[Examples](../../examples/)** - More code examples and patterns
- **[Architecture Guide](../architecture.md)** - Understanding MCWL internals

### Advanced Topics
- **Error Handling** - Proper error handling patterns
- **Performance Tuning** - Optimization techniques
- **Deployment** - Production deployment strategies
- **Testing** - Unit and integration testing with MCWL

### Community
- **GitHub Issues** - Report bugs or request features
- **Discussions** - Ask questions and share your projects
- **Contributing** - Help improve MCWL

Happy coding! 🚀
