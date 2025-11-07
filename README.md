# Modern C Web Library

A modern AI-assisted C library for building efficient, scalable, and feature-rich web backends with support for routing, async I/O, middleware, and JSON handling.

## Features

- **HTTP Server**: Multi-threaded HTTP server with async I/O
- **Routing**: Flexible routing with support for route parameters (e.g., `/users/:id`)
- **Middleware**: Chain middleware functions for request processing
- **JSON Support**: Built-in JSON parser and serializer
- **Database Connection Pooling**: Thread-safe connection pool for managing database connections
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

```bash
# From build directory
./examples/simple_server

# Or specify a custom port
./examples/simple_server 3000
```

The example server will start on port 8080 (or your specified port) with the following endpoints:

- `GET /` - Welcome message
- `GET /hello` - Hello World
- `GET /api/json` - JSON response example
- `GET /users/:id` - User info with route parameters
- `POST /api/data` - Echo posted data

### Running the Database Pool Example

```bash
# From build directory
./examples/db_pool_example
```

This example demonstrates thread-safe database connection pooling with multiple workers.

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

### Database Connection Pooling

```c
#include "db_pool.h"

// Create pool configuration
db_pool_config_t config = db_pool_config_default(DB_TYPE_GENERIC, "db://localhost/myapp");
config.min_connections = 2;
config.max_connections = 10;

// Create connection pool
db_pool_t *pool = db_pool_create(&config);

// Acquire a connection
db_connection_t *conn = db_pool_acquire(pool);
if (conn) {
    // Use the connection
    void *db_handle = db_connection_get_handle(conn);
    // ... perform database operations ...
    
    // Release connection back to pool
    db_pool_release(pool, conn);
}

// Get pool statistics
db_pool_stats_t stats;
db_pool_get_stats(pool, &stats);
printf("Active connections: %zu\n", stats.active_connections);

// Cleanup
db_pool_destroy(pool);
free(config.connection_string);
```

## API Reference

### HTTP Server

- `http_server_t *http_server_create(void)` - Create a new HTTP server
- `int http_server_listen(http_server_t *server, uint16_t port)` - Start listening on port
- `void http_server_stop(http_server_t *server)` - Stop the server
- `void http_server_destroy(http_server_t *server)` - Destroy server and free resources

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

### Database Connection Pool

- `db_pool_t *db_pool_create(const db_pool_config_t *config)` - Create a new connection pool
- `db_connection_t *db_pool_acquire(db_pool_t *pool)` - Acquire a connection from the pool
- `int db_pool_release(db_pool_t *pool, db_connection_t *conn)` - Release a connection back to the pool
- `int db_pool_get_stats(db_pool_t *pool, db_pool_stats_t *stats)` - Get pool statistics
- `int db_pool_close_idle(db_pool_t *pool)` - Close all idle connections
- `void db_pool_destroy(db_pool_t *pool)` - Destroy the pool and close all connections
- `void *db_connection_get_handle(db_connection_t *conn)` - Get the underlying database handle
- `bool db_connection_is_valid(db_connection_t *conn)` - Check if connection is valid
- `db_pool_config_t db_pool_config_default(db_type_t db_type, const char *connection_string)` - Create default configuration

## Project Structure

```
modern-c-web-library/
├── include/
│   ├── weblib.h           # Public API header
│   └── db_pool.h          # Database connection pool header
├── src/
│   ├── http_server.c      # HTTP server implementation
│   ├── router.c           # Router implementation
│   ├── json.c             # JSON parser/serializer
│   └── db_pool.c          # Database connection pool
├── examples/
│   ├── simple_server.c    # Example HTTP server
│   └── db_pool_example.c  # Database pool example
├── tests/
│   ├── test_weblib.c      # Unit tests
│   └── test_db_pool.c     # Database pool tests
├── CMakeLists.txt         # Main CMake configuration
├── README.md              # This file
├── LICENSE                # MIT License
└── .gitignore             # Git ignore rules
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
./tests/test_db_pool
```

## Requirements

- C11 compatible compiler (GCC, Clang, MSVC)
- CMake 3.10 or higher
- POSIX threads (Linux/macOS) or Windows threads

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

## Contributing

Contributions are welcome! Please feel free to submit a Pull Request.

## Roadmap

- [ ] Full async I/O support with event loops
- [ ] WebSocket support
- [ ] SSL/TLS support
- [ ] Request body parsing (form data, multipart)
- [ ] Cookie handling
- [ ] Session management
- [ ] Template engine
- [x] Database connection pooling
- [ ] Rate limiting
- [ ] Static file serving

## Author

Kamran Khan

## Acknowledgments

This library was developed with AI assistance to demonstrate modern C programming patterns for web development.
