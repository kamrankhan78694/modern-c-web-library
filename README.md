# Modern C Web Library

A modern AI-assisted C library for building efficient, scalable, and feature-rich web backends with support for routing, async I/O, middleware, and JSON handling.

## Features

- **HTTP/HTTPS Server**: Multi-threaded HTTP server with async I/O and SSL/TLS support
- **Routing**: Flexible routing with support for route parameters (e.g., `/users/:id`)
- **Middleware**: Chain middleware functions for request processing
- **JSON Support**: Built-in JSON parser and serializer
- **SSL/TLS Support**: Secure HTTPS connections using OpenSSL
- **Cross-Platform**: Works on Linux, macOS, and Windows
- **Modern C Patterns**: Clean, modular API design

## Quick Start

### Building the Library

```bash
# Create build directory
mkdir build && cd build

# Configure with CMake
cmake ..

# Build
make

# Run tests
make test
```

### Running the Example Server

**HTTP Server:**
```bash
# From build directory
./examples/simple_server

# Or specify a custom port
./examples/simple_server 3000
```

**HTTPS Server (with SSL/TLS):**
```bash
# First, generate test certificates
cd examples
./generate_cert.sh
cd ../build

# Run the HTTPS server
./examples/ssl_server

# Or specify custom port and certificate
./examples/ssl_server 8443 ../examples/certs/server.crt ../examples/certs/server.key
```

The HTTP example server will start on port 8080 (or your specified port) with the following endpoints:

- `GET /` - Welcome message
- `GET /hello` - Hello World
- `GET /api/json` - JSON response example
- `GET /users/:id` - User info with route parameters
- `POST /api/data` - Echo posted data

The HTTPS server will start on port 8443 with secure endpoints demonstrating SSL/TLS encryption.

## Usage

### Basic HTTP Server

```c
#include "weblib.h"

void handle_root(http_request_t *req, http_response_t *res) {
    http_response_send_text(res, HTTP_OK, "Hello, World!");
}

int main(void) {
    // Create server and router
    http_server_t *server = http_server_create();
    router_t *router = router_create();
    
    // Add routes
    router_add_route(router, HTTP_GET, "/", handle_root);
    
    // Set router and start server
    http_server_set_router(server, router);
    http_server_listen(server, 8080);
    
    // Cleanup
    router_destroy(router);
    http_server_destroy(server);
    
    return 0;
}
```

### Route Parameters

```c
void handle_user(http_request_t *req, http_response_t *res) {
    const char *user_id = http_request_get_param(req, "id");
    // ... handle user request
}

router_add_route(router, HTTP_GET, "/users/:id", handle_user);
```

### JSON Handling

```c
void handle_api(http_request_t *req, http_response_t *res) {
    // Create JSON object
    json_value_t *json = json_object_create();
    json_object_set(json, "message", json_string_create("Success"));
    json_object_set(json, "status", json_number_create(200));
    
    // Send JSON response
    http_response_send_json(res, HTTP_OK, json);
    json_value_free(json);
}
```

### Middleware

```c
bool logging_middleware(http_request_t *req, http_response_t *res) {
    printf("Request: %s\n", req->path);
    return true; // Continue to next middleware/handler
}

router_use_middleware(router, logging_middleware);
```

### SSL/TLS (HTTPS)

```c
#include "weblib.h"

int main(void) {
    // Create server
    http_server_t *server = http_server_create();
    
    // Configure SSL
    ssl_config_t ssl_config = {
        .cert_file = "path/to/server.crt",
        .key_file = "path/to/server.key",
        .ca_file = NULL,              // Optional: for client verification
        .verify_peer = false,         // Set to true for mutual TLS
        .min_tls_version = 0          // 0 = use OpenSSL defaults
    };
    
    // Enable SSL/TLS
    if (http_server_enable_ssl(server, &ssl_config) < 0) {
        fprintf(stderr, "Failed to enable SSL\n");
        return 1;
    }
    
    // Create router and add routes
    router_t *router = router_create();
    router_add_route(router, HTTP_GET, "/", handle_root);
    
    // Set router and start HTTPS server
    http_server_set_router(server, router);
    http_server_listen(server, 8443);  // HTTPS typically uses port 443 or 8443
    
    // Cleanup
    router_destroy(router);
    http_server_destroy(server);
    ssl_library_cleanup();
    
    return 0;
}
```

**Generating Test Certificates:**

For development and testing, you can generate self-signed certificates:

```bash
# Use the provided script
./examples/generate_cert.sh

# Or manually with OpenSSL
openssl req -x509 -newkey rsa:4096 -keyout server.key -out server.crt \
    -days 365 -nodes -subj "/C=US/ST=State/L=City/O=Org/CN=localhost"
```

**Testing HTTPS Endpoints:**

```bash
# Using curl (skip certificate verification for self-signed certs)
curl -k https://localhost:8443/

# Using curl with verbose output
curl -kv https://localhost:8443/api/data
```

## API Reference

### HTTP Server

- `http_server_t *http_server_create(void)` - Create a new HTTP server
- `int http_server_listen(http_server_t *server, uint16_t port)` - Start listening on port
- `void http_server_stop(http_server_t *server)` - Stop the server
- `void http_server_destroy(http_server_t *server)` - Destroy server and free resources
- `int http_server_enable_ssl(http_server_t *server, const ssl_config_t *config)` - Enable SSL/TLS
- `bool http_server_is_ssl_enabled(http_server_t *server)` - Check if SSL is enabled

### Router

- `router_t *router_create(void)` - Create a new router
- `int router_add_route(router_t *router, http_method_t method, const char *path, route_handler_t handler)` - Add a route
- `int router_use_middleware(router_t *router, middleware_fn_t middleware)` - Add middleware
- `void router_destroy(router_t *router)` - Destroy router

### JSON

- `json_value_t *json_parse(const char *json_str)` - Parse JSON string
- `json_value_t *json_object_create(void)` - Create JSON object
- `void json_object_set(json_value_t *obj, const char *key, json_value_t *value)` - Set object property
- `json_value_t *json_object_get(json_value_t *obj, const char *key)` - Get object property
- `char *json_stringify(json_value_t *value)` - Convert JSON to string
- `void json_value_free(json_value_t *value)` - Free JSON value

## Project Structure

```
modern-c-web-library/
├── include/
│   └── weblib.h           # Public API header
├── src/
│   ├── http_server.c      # HTTP server implementation
│   ├── router.c           # Router implementation
│   ├── json.c             # JSON parser/serializer
│   └── ssl_context.c      # SSL/TLS implementation
├── examples/
│   ├── simple_server.c    # Example HTTP server
│   ├── ssl_server.c       # Example HTTPS server
│   └── generate_cert.sh   # Script to generate test certificates
├── tests/
│   └── test_weblib.c      # Unit tests
├── CMakeLists.txt         # Main CMake configuration
├── README.md              # This file
├── LICENSE                # MIT License
└── .gitignore             # Git ignore rules
```

### SSL/TLS API

- `void ssl_library_init(void)` - Initialize OpenSSL library (call once at startup)
- `void ssl_library_cleanup(void)` - Cleanup OpenSSL library (call once at shutdown)
- `ssl_context_t *ssl_context_create(const ssl_config_t *config)` - Create SSL context
- `void ssl_context_destroy(ssl_context_t *ctx)` - Destroy SSL context
- `const char *ssl_get_error_string(void)` - Get last SSL error string

**SSL Configuration Structure:**
```c
typedef struct {
    const char *cert_file;      // Path to certificate file (PEM format)
    const char *key_file;       // Path to private key file (PEM format)
    const char *ca_file;        // Optional: CA cert for client verification
    bool verify_peer;           // Whether to verify client certificates
    int min_tls_version;        // Minimum TLS version (0 for defaults)
} ssl_config_t;
```

## Building on Different Platforms

### Linux/macOS

```bash
mkdir build && cd build
cmake ..
make
```

### Windows (Visual Studio)

```bash
mkdir build && cd build
cmake .. -G "Visual Studio 16 2019"
cmake --build .
```

### Windows (MinGW)

```bash
mkdir build && cd build
cmake .. -G "MinGW Makefiles"
mingw32-make
```

## Testing

Run the test suite:

```bash
cd build
make test

# Or run tests directly
./tests/test_weblib
```

## Requirements

- C11 compatible compiler (GCC, Clang, MSVC)
- CMake 3.10 or higher
- POSIX threads (Linux/macOS) or Windows threads
- OpenSSL 1.1.0 or higher (for SSL/TLS support)

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

## Contributing

Contributions are welcome! Please feel free to submit a Pull Request.

## Roadmap

- [ ] Full async I/O support with event loops
- [ ] WebSocket support
- [x] SSL/TLS support
- [ ] Request body parsing (form data, multipart)
- [ ] Cookie handling
- [ ] Session management
- [ ] Template engine
- [ ] Database connection pooling
- [ ] Rate limiting
- [ ] Static file serving

## Author

Kamran Khan

## Acknowledgments

This library was developed with AI assistance to demonstrate modern C programming patterns for web development.
