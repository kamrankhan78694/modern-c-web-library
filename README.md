# Modern C Web Library

A modern AI-assisted C library for building efficient, scalable, and feature-rich web backends with support for routing, async I/O, middleware, and JSON handling.

## Features

- **HTTP Server**: Multi-threaded HTTP server with async I/O
- **Routing**: Flexible routing with support for route parameters (e.g., `/users/:id`)
- **Middleware**: Chain middleware functions for request processing
- **JSON Support**: Built-in JSON parser and serializer
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

### Form Data Parsing

The library automatically parses form data from POST requests based on the Content-Type header.

#### URL-encoded Form Data

```c
void handle_form_submit(http_request_t *req, http_response_t *res) {
    // Get form fields from URL-encoded data (application/x-www-form-urlencoded)
    const char *username = http_request_get_form_field(req, "username");
    const char *email = http_request_get_form_field(req, "email");
    
    json_value_t *json = json_object_create();
    json_object_set(json, "username", json_string_create(username ? username : ""));
    json_object_set(json, "email", json_string_create(email ? email : ""));
    
    http_response_send_json(res, HTTP_OK, json);
    json_value_free(json);
}
```

#### Multipart Form Data with File Uploads

```c
void handle_file_upload(http_request_t *req, http_response_t *res) {
    // Get regular form fields
    const char *description = http_request_get_form_field(req, "description");
    
    // Get uploaded file
    const char *filename = NULL;
    const char *content_type = NULL;
    size_t file_size = 0;
    const char *file_data = http_request_get_file(req, "file", 
                                                   &filename, 
                                                   &content_type, 
                                                   &file_size);
    
    if (file_data) {
        // Process file...
        json_value_t *json = json_object_create();
        json_object_set(json, "filename", json_string_create(filename));
        json_object_set(json, "size", json_number_create((double)file_size));
        json_object_set(json, "type", json_string_create(content_type));
        
        http_response_send_json(res, HTTP_OK, json);
        json_value_free(json);
    } else {
        http_response_send_text(res, HTTP_BAD_REQUEST, "No file uploaded");
    }
}
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

### Form Data

- `const char *http_request_get_form_field(http_request_t *req, const char *key)` - Get form field value (supports both URL-encoded and multipart)
- `const char *http_request_get_file(http_request_t *req, const char *field_name, const char **filename, const char **content_type, size_t *size)` - Get uploaded file from multipart request

## Project Structure

```
modern-c-web-library/
├── include/
│   └── weblib.h           # Public API header
├── src/
│   ├── http_server.c      # HTTP server implementation
│   ├── router.c           # Router implementation
│   ├── json.c             # JSON parser/serializer
│   └── body_parser.c      # Form data parser (URL-encoded & multipart)
├── examples/
│   └── simple_server.c    # Example HTTP server
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
- [x] Request body parsing (form data, multipart)
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
