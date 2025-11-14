# Modern C Web Library

Clone from : https://github.com/kamrankhan78694/modern-c-web-library.git

A modern AI-assisted C library for building efficient, scalable, and feature-rich web backends with support for routing, async I/O, middleware, and JSON handling.

## // Philosophy: Life, Code, Evolution

```
if(system.working) {
    // Classic wisdom
    printf("If it ain't broke, don't fix it…\n");
} else {
    // Evolution kicks in
    printf("Attempting adapt_fast()…\n");
    if(adapt_fast()) {
        printf("Adaptation succeeded. Survive(); Thrive();\n");
    } else {
        perish(); // The ultimate fallback
    }
}

// adapt_fast() attempts changes.  
// perish() triggers if adaptation fails.  
// Life, like code, only preserves what works—and tests what survives.

```

## Design Philosophy

This project is a **pure ISO C implementation** (C99 or later) designed to demonstrate that modern web functionality can be achieved entirely in standard C, without relying on external dependencies or third-party libraries.

**Core Principles:**
- **Pure ISO C**: All code is written in standard C (C99 or newer) for maximum portability
- **Zero External Dependencies**: No third-party libraries or frameworks are permitted
- **Self-Sufficient**: The library implements all required functionality from the ground up
- **Educational Foundation**: Serves as a reference implementation of modern C design patterns
- **Minimal & Transparent**: Every line of code is part of the project—no hidden dependencies

**Why Pure C?**
This approach ensures that developers can:
- Study and understand every aspect of the implementation
- Trust the codebase as a minimal, foundational reference
- Extend functionality without dependency conflicts
- Deploy on any platform with a C compiler
- Maintain full control over the entire codebase

Contributors should align with this philosophy. To maintain project integrity, suggestions involving external packages or higher-level language integrations cannot be accepted, as they conflict with the core mission of being a self-sufficient C library.

## Language Policy

The entire project is written in **standard ISO C** (C99 or newer). This is a strict policy to maintain the integrity of the project as a pure C foundation.

**Language Requirements:**
- **C Only**: All source code, utilities, and tests must be written in C
- **No Foreign Languages**: Python scripts, JavaScript wrappers, or any other language integrations are prohibited
- **Standard C APIs**: Only standard C library functions and platform-specific system calls are permitted
- **No Code Generation**: All code must be written in C, not generated from other languages

**Why This Matters:**
- **Maximum Portability**: Standard C runs on virtually any platform with a C compiler
- **Transparency**: Every component is visible and understandable without learning multiple languages
- **Foundational Clarity**: Demonstrates that complex systems can be built entirely in C
- **Educational Value**: Provides a pure example of C craftsmanship and design
- **No Hidden Magic**: What you see is what you get—no abstraction layers or runtime dependencies

This policy emphasizes **C craftsmanship** over convenience through other ecosystems. The goal is to prove that modern web backends can be elegant, efficient, and maintainable using nothing but well-designed C code.

## Features

- **HTTP Server**: Multi-threaded and async I/O HTTP server
- **Async I/O**: Full event loop support with epoll (Linux), kqueue (BSD/macOS), and poll fallback
- **Event Loop**: High-performance non-blocking I/O for handling thousands of concurrent connections
- **Routing**: Flexible routing with support for route parameters (e.g., `/users/:id`)
- **Middleware**: Chain middleware functions for request processing
- **JSON Support**: Built-in JSON parser and serializer
- **Cross-Platform**: Works on Linux, macOS, and Windows
- **Modern C Patterns**: Clean, modular API design

## Quick Start

### Option 1: Using Docker (Recommended for Contributors)

Docker provides a consistent build environment without installing dependencies:

```bash
# Clone the repository
git clone https://github.com/kamrankhan78694/modern-c-web-library.git
cd modern-c-web-library

# Build and run tests
./docker-run.sh test

# Start development environment
./docker-run.sh dev

# Run the server
./docker-run.sh async
```

See [DOCKER.md](DOCKER.md) for complete Docker documentation.

### Option 2: Building Locally

#### Building the Library

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

#### Running the Example Server

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

## Docker Development Environment

For contributors who want a consistent, reproducible environment without installing dependencies locally, we provide a Docker setup.

### Quick Start with Docker

```bash
# Build the Docker image
docker build -t modern-c-web-library .

# Run the container (executes build, tests, and verification)
docker run --rm modern-c-web-library
```

### Development with Docker Compose

For active development with live code editing:

```bash
# Start the development container
docker-compose run --rm weblib-dev
```

**First-time setup inside the container (when /workspace/build is empty):**

```bash
mkdir -p build && cd build
cmake ..
make
make test
```

**For subsequent rebuilds after code changes:**

```bash
cd build
make
make test
```

The `docker-compose.yml` configuration mounts your source code, so you can edit files locally and rebuild inside the container.

### What's Included in the Docker Environment

- **Ubuntu 22.04** base image
- **Build tools**: gcc, g++, make, cmake, git
- **Pre-built library**: The image builds and tests the library during image creation
- **Verification script**: Automatically runs to validate the build

### Docker Commands Reference

```bash
# Build the image
docker build -t modern-c-web-library .

# Run tests in container
docker run --rm modern-c-web-library

# Start interactive shell
docker run --rm -it modern-c-web-library /bin/bash

# Run example server (requires port mapping)
docker run --rm -p 8080:8080 modern-c-web-library ./build/examples/simple_server

# Use docker-compose for development
docker-compose run --rm weblib-dev /bin/bash
```

### Benefits of Docker for Contributors

- **No local dependencies**: No need to install gcc, cmake, or other tools
- **Consistent environment**: Same build environment for all contributors
- **Isolated testing**: Test changes without affecting your system
- **CI/CD ready**: Same environment can be used in continuous integration

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

### Async I/O Mode

The library supports full async I/O with event loops for high-performance, non-blocking request handling:

```c
#include "weblib.h"

int main(void) {
    // Create server
    http_server_t *server = http_server_create();
    
    // Enable async I/O mode
    http_server_set_async(server, true);
    
    // Get event loop (for advanced use cases)
    event_loop_t *loop = http_server_get_event_loop(server);
    
    // Create and set up router
    router_t *router = router_create();
    router_add_route(router, HTTP_GET, "/", handle_root);
    http_server_set_router(server, router);
    
    // Start server (runs event loop internally)
    http_server_listen(server, 8080);
    
    // Cleanup
    router_destroy(router);
    http_server_destroy(server);
    
    return 0;
}
```

**Event Loop Backends:**
- **Linux**: epoll (high performance)
- **macOS/BSD**: kqueue (high performance)
- **Fallback**: poll (portable)

### Event Loop API

For advanced use cases, you can use the event loop directly:

```c
// Create event loop
event_loop_t *loop = event_loop_create();

// Add file descriptor to monitor
event_loop_add_fd(loop, fd, EVENT_READ | EVENT_WRITE, callback, user_data);

// Modify events for a file descriptor
event_loop_modify_fd(loop, fd, EVENT_READ);

// Remove file descriptor
event_loop_remove_fd(loop, fd);

// Add timeout
int timer_id = event_loop_add_timeout(loop, 1000, timeout_callback, user_data);

// Cancel timeout
event_loop_cancel_timeout(loop, timer_id);

// Run event loop
event_loop_run(loop);

// Stop event loop (from signal handler or callback)
event_loop_stop(loop);

// Cleanup
event_loop_destroy(loop);
```

## API Reference

### HTTP Server

- `http_server_t *http_server_create(void)` - Create a new HTTP server
- `int http_server_listen(http_server_t *server, uint16_t port)` - Start listening on port
- `void http_server_stop(http_server_t *server)` - Stop the server
- `void http_server_destroy(http_server_t *server)` - Destroy server and free resources
- `int http_server_set_async(http_server_t *server, bool enable)` - Enable/disable async I/O mode
- `event_loop_t *http_server_get_event_loop(http_server_t *server)` - Get server's event loop

### Router

- `router_t *router_create(void)` - Create a new router
- `int router_add_route(router_t *router, http_method_t method, const char *path, route_handler_t handler)` - Add a route
- `int router_use_middleware(router_t *router, middleware_fn_t middleware)` - Add middleware
- `void router_destroy(router_t *router)` - Destroy router

### Event Loop

- `event_loop_t *event_loop_create(void)` - Create a new event loop
- `int event_loop_add_fd(event_loop_t *loop, int fd, int events, event_callback_t callback, void *user_data)` - Monitor file descriptor
- `int event_loop_modify_fd(event_loop_t *loop, int fd, int events)` - Modify events for file descriptor
- `int event_loop_remove_fd(event_loop_t *loop, int fd)` - Stop monitoring file descriptor
- `int event_loop_run(event_loop_t *loop)` - Run the event loop
- `void event_loop_stop(event_loop_t *loop)` - Stop the event loop
- `int event_loop_add_timeout(event_loop_t *loop, int timeout_ms, event_callback_t callback, void *user_data)` - Add timeout
- `int event_loop_cancel_timeout(event_loop_t *loop, int timer_id)` - Cancel timeout
- `void event_loop_destroy(event_loop_t *loop)` - Destroy event loop

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
│   ├── http_server.c      # HTTP server implementation (sync & async)
│   ├── router.c           # Router implementation
│   ├── json.c             # JSON parser/serializer
│   └── event_loop.c       # Event loop implementation (epoll/kqueue/poll)
├── examples/
│   ├── simple_server.c    # Example HTTP server (threaded mode)
│   └── async_server.c     # Example async HTTP server (event loop mode)
├── tests/
│   └── test_weblib.c      # Unit tests
├── CMakeLists.txt         # Main CMake configuration
├── README.md              # This file
├── LICENSE                # MIT License
└── .gitignore             # Git ignore rules
```

## Building on Different Platforms

### Using Docker (All Platforms)

Docker provides the easiest way to build and test on any platform:

```bash
# Build and test
./docker-run.sh test

# Development environment
./docker-run.sh dev

# Run server
./docker-run.sh async
```

See [DOCKER.md](DOCKER.md) for detailed Docker usage.

### Native Builds

#### Linux/macOS

```bash
mkdir build && cd build
cmake ..
make
```

#### Windows (Visual Studio)

```bash
mkdir build && cd build
cmake .. -G "Visual Studio 16 2019"
cmake --build .
```

#### Windows (MinGW)

```bash
mkdir build && cd build
cmake .. -G "MinGW Makefiles"
mingw32-make
```

## Testing

### Using Docker (Recommended)

```bash
# Run all tests in Docker
./docker-run.sh test

# Or with docker-compose
docker-compose --profile dev run --rm weblib-dev /bin/bash -c "cd build && make test"
```

### Native Testing

Run the test suite:

```bash
cd build
make test

# Or run tests directly
./tests/test_weblib
```

### Memory Leak Testing with Valgrind (Docker)

```bash
# Start dev container
./docker-run.sh dev

# Inside container
cd build
valgrind --leak-check=full ./tests/test_weblib
```

## Requirements

### For Docker Users (Recommended)
- Docker Desktop (Windows/macOS) or Docker Engine (Linux)
- Git
- **No other dependencies needed!**

### For Native Builds
- C11 compatible compiler (GCC, Clang, MSVC)
- CMake 3.10 or higher
- POSIX threads (Linux/macOS) or Windows threads

**No External Dependencies**: This library uses only standard C library functions and platform-specific system libraries and APIs (such as POSIX threads, Windows API for sockets and threading, etc.). No third-party libraries are required or used.

## Docker Support

The project includes full Docker support for development and deployment:

- **Development Environment**: Full toolchain with GCC, CMake, GDB, Valgrind
- **Production Image**: Minimal runtime image (~150MB)
- **Easy Testing**: Run tests with a single command
- **Consistent Builds**: Same environment for all contributors

**Quick Start:**
```bash
./docker-run.sh test    # Run tests
./docker-run.sh dev     # Start development container
./docker-run.sh async   # Run server
```

**Documentation**: See [DOCKER.md](DOCKER.md) for complete Docker guide.

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

## Contributing

Contributions are welcome! We appreciate your interest in improving the Modern C Web Library.

**Quick Start for Contributors:**
```bash
# Using Docker (recommended)
git clone https://github.com/kamrankhan78694/modern-c-web-library.git
cd modern-c-web-library
./docker-run.sh test    # Verify everything works
./docker-run.sh dev     # Start development environment
```

Please read our [Contributing Guidelines](CONTRIBUTING.md) for details on:
- Development setup and workflow
- Coding standards and style guide
- How to submit pull requests
- Testing requirements

Also review our [Code of Conduct](CODE_OF_CONDUCT.md) to understand our community expectations.

For a list of planned features and enhancements, check out [TODO.md](TODO.md).

## Test Results & Quality Metrics

**Current Status**: ✅ All tests passing (18/18)

**Latest Test Run** (November 2025):
```
Running Modern C Web Library Tests
===================================

Testing router_create... PASSED
Testing router_add_route... PASSED
Testing json_object_create... PASSED
Testing json_string_create... PASSED
Testing json_number_create... PASSED
Testing json_bool_create... PASSED
Testing json_object_set/get... PASSED
Testing json_stringify... PASSED
Testing json_parse (string)... PASSED
Testing json_parse (number)... PASSED
Testing json_parse (bool)... PASSED
Testing json_parse (null)... PASSED
Testing json_parse (object)... PASSED
Testing http_server_create... PASSED
Testing event_loop_create... PASSED
Testing http_server_set_async... PASSED
Testing event_loop_add_timeout... PASSED
Testing event_loop_cancel_timeout... PASSED

===================================
Tests run: 18
Tests passed: 18
Tests failed: 0
```

**Code Quality:**
- ✅ Zero compiler warnings (with `-Wall -Wextra`)
- ✅ All `sprintf` replaced with secure `snprintf`
- ✅ AddressSanitizer clean build
- ✅ Memory leak detection ready
- ✅ VS Code debugging configured with LLDB/GDB support

**Debugging Tools:**
- Full VS Code integration with launch configurations
- LLDB debugger support (macOS/Linux)
- GDB debugger support (Linux)
- AddressSanitizer integration for memory error detection
- Valgrind support (Linux/Docker)
- Comprehensive debugging guide in [docs/DEBUGGING.md](docs/DEBUGGING.md)

## Roadmap

- [x] Full async I/O support with event loops (epoll/kqueue/poll)
- [x] Route parameter extraction (`:param` syntax)
- [x] Security improvements (safe string operations)
- [x] Comprehensive debugging setup
- [ ] WebSocket support
- [ ] SSL/TLS support
- [ ] Request body parsing (form data, multipart)
- [ ] Cookie handling
- [ ] Session management
- [ ] Template engine
- [ ] Database connection pooling
- [ ] Rate limiting
- [ ] Static file serving

## Community & Support

- **Issues**: Found a bug or have a feature request? [Open an issue](https://github.com/kamrankhan78694/modern-c-web-library/issues)
- **Discussions**: Have questions or want to discuss ideas? [Start a discussion](https://github.com/kamrankhan78694/modern-c-web-library/discussions)
- **Contributing**: Check out [CONTRIBUTING.md](CONTRIBUTING.md) for contribution guidelines
- **TODO List**: See [TODO.md](TODO.md) for planned features and ways to contribute

## Author

Kamran Khan

## Acknowledgments

This library was developed with AI assistance to demonstrate modern C programming patterns for web development.
