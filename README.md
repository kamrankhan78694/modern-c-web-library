# Modern C Web Library

A modern AI-assisted C library for building efficient, scalable, and feature-rich web backends with support for routing, async I/O, middleware, and JSON handling.

## Features

- **HTTP Server**: Multi-threaded HTTP server with async I/O
- **Routing**: Flexible routing with support for route parameters (e.g., `/users/:id`)
- **Middleware**: Chain middleware functions for request processing
- **JSON Support**: Built-in JSON parser and serializer
- **Session Management**: Cookie-based session management with data storage and expiration
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
- `POST /session/login` - Create a session and login
- `GET /session/info` - Get current session information
- `POST /session/logout` - Logout and destroy session
- `GET /session/visits` - Track visit count with sessions

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

### Session Management

```c
#include "weblib.h"

// Create session store
session_store_t *store = session_store_create();

// Handler to login and create session
void handle_login(http_request_t *req, http_response_t *res) {
    // Create new session with 1 hour expiry
    char *session_id = session_create(store, 3600);
    
    // Get session and store user data
    session_t *session = session_get(store, session_id);
    session_set(session, "user_id", "123");
    session_set(session, "username", "john_doe");
    
    // Set session cookie in response
    session_set_cookie(res, session_id, 3600, "/");
    
    http_response_send_text(res, HTTP_OK, "Logged in!");
    free(session_id);
}

// Handler to access protected route
void handle_protected(http_request_t *req, http_response_t *res) {
    // Get session from request cookie
    session_t *session = session_from_request(store, req);
    
    if (!session) {
        http_response_send_text(res, HTTP_UNAUTHORIZED, "Please login");
        return;
    }
    
    // Access session data
    const char *username = session_get_data(session, "username");
    // ... use username
}

// Handler to logout
void handle_logout(http_request_t *req, http_response_t *res) {
    session_t *session = session_from_request(store, req);
    if (session) {
        const char *session_id = session_get_id(session);
        session_destroy(store, session_id);
        session_set_cookie(res, session_id, -1, "/"); // Delete cookie
    }
    http_response_send_text(res, HTTP_OK, "Logged out!");
}

// Don't forget to cleanup
session_store_destroy(store);
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

### Session Management

- `session_store_t *session_store_create(void)` - Create session store
- `void session_store_destroy(session_store_t *store)` - Destroy session store
- `char *session_create(session_store_t *store, int max_age)` - Create new session (returns session ID)
- `session_t *session_get(session_store_t *store, const char *session_id)` - Get session by ID
- `void session_destroy(session_store_t *store, const char *session_id)` - Destroy session
- `void session_set(session_t *session, const char *key, const char *value)` - Set session data
- `const char *session_get_data(session_t *session, const char *key)` - Get session data
- `void session_remove_data(session_t *session, const char *key)` - Remove session data
- `const char *session_get_id(session_t *session)` - Get session ID
- `bool session_is_expired(session_t *session)` - Check if session is expired
- `int session_cleanup_expired(session_store_t *store)` - Clean up expired sessions
- `session_t *session_from_request(session_store_t *store, http_request_t *req)` - Get session from request cookie
- `void session_set_cookie(http_response_t *res, const char *session_id, int max_age, const char *path)` - Set session cookie

## Project Structure

```
modern-c-web-library/
├── include/
│   └── weblib.h           # Public API header
├── src/
│   ├── http_server.c      # HTTP server implementation
│   ├── router.c           # Router implementation
│   ├── json.c             # JSON parser/serializer
│   └── session.c          # Session management
├── examples/
│   └── simple_server.c    # Example HTTP server with sessions
├── tests/
│   └── test_weblib.c      # Unit tests
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
- [x] Session management
- [ ] Template engine
- [ ] Database connection pooling
- [ ] Rate limiting
- [ ] Static file serving

## Author

Kamran Khan

## Acknowledgments

This library was developed with AI assistance to demonstrate modern C programming patterns for web development.
