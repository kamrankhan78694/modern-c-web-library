# Modern C Web Library - AI Agent Instructions

## Project Philosophy: Pure C Implementation

This is a **strict pure C project** with zero external dependencies. All contributions must align with this core principle:

- **Language**: ISO C (C99/C11) only - no Python, JavaScript, or other languages
- **Dependencies**: None - implement everything from scratch in C
- **Libraries**: Only standard C library + platform APIs (POSIX, Windows API)
- **Goal**: Demonstrate that modern web backends can be built entirely in self-sufficient C

**Critical**: Never suggest external libraries (OpenSSL, libcurl, cJSON, etc.). If a feature needs implementation, write it in pure C.

## Architecture Overview

### Component Structure

The library follows a modular design with four core components:

1. **HTTP Server** (`src/http_server.c`): Handles both threaded and async I/O modes
   - Threaded mode: `pthread` for concurrent connections
   - Async mode: Event loop for non-blocking I/O (see below)
   - Connection handling: `MAX_CONNECTIONS=128`, `BUFFER_SIZE=8192`

2. **Event Loop** (`src/event_loop.c`): Platform-specific async I/O backends
   - Linux: `epoll` (high performance)
   - macOS/BSD: `kqueue` (high performance) 
   - Fallback: `poll` (portable)
   - Compile-time selection via preprocessor directives
   - Max events: 1024, Max timers: 64

3. **Router** (`src/router.c`): Request routing with middleware chain
   - Static limits: `MAX_ROUTES=256`, `MAX_MIDDLEWARES=32`
   - Route parameters: `/users/:id` pattern matching
   - Middleware: Boolean return (true=continue, false=stop)

4. **JSON Parser** (`src/json.c`): Hand-written JSON parser/serializer
   - No external JSON library - custom implementation
   - Linked list structures for objects/arrays
   - Memory management: All JSON must be freed with `json_value_free()`

### Operating Modes

The server supports two modes that affect I/O handling:

**Threaded Mode** (default):
- `pthread` creates thread per connection
- Blocking I/O operations
- Simpler programming model
- Example: `examples/simple_server.c`

**Async Mode** (via `http_server_set_async(true)`):
- Event loop with non-blocking I/O
- Single-threaded, handles thousands of connections
- Requires event-driven programming
- Example: `examples/async_server.c`

## Development Workflows

### Building and Testing

**Using Docker (Recommended for Contributors):**
```bash
# Run all tests in consistent environment
./docker-run.sh test

# Start development container with volume mount
./docker-run.sh dev

# Inside container: build, test, debug
cd build
cmake .. && make
make test
./tests/test_weblib
valgrind --leak-check=full ./tests/test_weblib
```

**Native Build:**
```bash
# Initial build
mkdir build && cd build
cmake ..
make

# Run tests (custom test framework, no external test libs)
make test
./tests/test_weblib

# Run examples
./examples/simple_server [port]    # Default: 8080
./examples/async_server [port]
```

**Docker Quick Reference:**
- `./docker-run.sh test` - Run tests
- `./docker-run.sh dev` - Development shell
- `./docker-run.sh async` - Run server
- See `DOCKER.md` for complete guide

### Platform-Specific Compilation

The project uses preprocessor directives for platform detection:

```c
#ifdef __linux__
    // Linux-specific (epoll)
#elif defined(__APPLE__) || defined(__FreeBSD__)
    // macOS/BSD-specific (kqueue)
#elif defined(_WIN32)
    // Windows-specific
#else
    // Portable fallback (poll)
#endif
```

CMake sets `PLATFORM_LIBS` automatically:
- Linux/macOS: `pthread`
- Windows: `ws2_32`

## Coding Conventions

### Naming and Style

- **Functions**: `snake_case` → `http_server_create()`, `event_loop_run()`
- **Types**: `snake_case_t` suffix → `http_server_t`, `json_value_t`
- **Enums**: `UPPER_SNAKE_CASE` → `HTTP_GET`, `HTTP_OK`, `EVENT_READ`
- **Internal/Static**: Prefix `_` → `_internal_helper()`
- **Indentation**: 4 spaces (no tabs)
- **Braces**: K&R style (opening brace on same line)

### Memory Management Patterns

**Critical**: All allocations must have corresponding frees. Common patterns:

```c
// Server lifecycle
http_server_t *server = http_server_create();  // malloc
// ... use server ...
http_server_destroy(server);                   // free

// JSON lifecycle
json_value_t *json = json_object_create();
json_object_set(json, "key", json_string_create("value"));
http_response_send_json(res, HTTP_OK, json);
json_value_free(json);  // MUST free after use

// Router lifecycle
router_t *router = router_create();
router_add_route(router, HTTP_GET, "/", handler);
http_server_set_router(server, router);
// ... 
router_destroy(router);  // Separate from server
```

### Error Handling Pattern

Return codes follow POSIX convention:
- Success: `0`
- Failure: `-1` (check `errno` for system errors)
- Pointers: `NULL` on failure

```c
int result = http_server_listen(server, port);
if (result < 0) {
    perror("Failed to start server");
    return 1;
}
```

### Route Handler Signature

All route handlers must match this signature:

```c
void handler_name(http_request_t *req, http_response_t *res) {
    // Extract data
    const char *param = http_request_get_param(req, "id");
    const char *header = http_request_get_header(req, "Content-Type");
    
    // Send response (marks res->sent = true internally)
    http_response_send_text(res, HTTP_OK, "Response body");
    // OR
    json_value_t *json = json_object_create();
    http_response_send_json(res, HTTP_OK, json);
    json_value_free(json);
}
```

### Middleware Pattern

Middleware chains execute before route handlers:

```c
bool middleware_name(http_request_t *req, http_response_t *res) {
    // Modify request/response
    http_response_set_header(res, "X-Custom", "value");
    
    // Control flow
    return true;   // Continue to next middleware/handler
    return false;  // Stop processing (e.g., auth failure)
}

// Registration (executes in order added)
router_use_middleware(router, logging_middleware);
router_use_middleware(router, cors_middleware);
```

## Common Implementation Patterns

### Adding New Routes

When adding endpoints, follow the example pattern:

1. Define handler function
2. Register in `main()` with `router_add_route()`
3. Use `http_request_get_param()` for URL parameters
4. Always send response via `http_response_send_text()` or `http_response_send_json()`

### Async I/O Implementation

When working with async mode:

1. Call `http_server_set_async(server, true)` before `http_server_listen()`
2. Get event loop: `event_loop_t *loop = http_server_get_event_loop(server)`
3. The server internally uses event callbacks for read/write operations
4. Signal handlers should call `event_loop_stop(loop)` for graceful shutdown

### Platform-Specific Features

When adding platform-specific code:

1. Use `#ifdef` guards matching existing patterns
2. Provide fallback implementation for unsupported platforms
3. Test on Linux, macOS, and Windows (if possible)
4. Document platform limitations in code comments

## Testing Strategy

Custom test framework (no external libraries like CUnit):

```c
// Test structure
void test_feature_name(void) {
    TEST("feature_name");
    
    // Setup
    feature_t *f = feature_create();
    ASSERT(f != NULL);
    
    // Test logic
    ASSERT(some_condition);
    
    // Cleanup
    feature_destroy(f);
    
    PASS();
}
```

Add new tests to `tests/test_weblib.c` and call from `main()`. No CTest configuration changes needed.

## Integration Points

### Server-Router Interaction

The server holds a pointer to the router but doesn't own it:

```c
http_server_t *server = http_server_create();
router_t *router = router_create();
http_server_set_router(server, router);  // Server stores reference only
// Must destroy both separately
router_destroy(router);
http_server_destroy(server);
```

### Event Loop Integration

In async mode, the server creates and manages the event loop internally:
- `http_server_set_async()` → creates `event_loop_t`
- `http_server_listen()` → runs event loop (blocking)
- `event_loop_stop()` → breaks from loop
- `http_server_destroy()` → destroys event loop

Don't create separate event loops unless implementing custom async patterns.

## Key Files Reference

- **API Surface**: `include/weblib.h` - all public declarations
- **Examples**: `examples/simple_server.c` (threaded), `examples/async_server.c` (event loop)
- **Build Config**: `CMakeLists.txt` - platform detection, compiler flags
- **Roadmap**: `TODO.md` - planned features prioritized by 🎯/🔧/💡
- **Contributing**: `CONTRIBUTING.md` - detailed style guide and philosophy

## Common Pitfalls to Avoid

1. **Never suggest external dependencies** - implement in pure C
2. **Don't mix async/threaded patterns** - server uses one mode at a time
3. **Memory leaks**: Always pair create/destroy, especially JSON objects
4. **Platform assumptions**: Test `#ifdef` guards on multiple platforms
5. **Buffer sizes**: Respect `BUFFER_SIZE=8192` and static array limits
6. **Response handling**: Don't send response multiple times (check `res->sent`)
7. **Threading**: Only threaded mode uses `pthread`, async mode is single-threaded

## When Adding New Features

Before implementing new functionality:

1. Check `TODO.md` for planned approach and priority
2. Verify it can be implemented in pure C without dependencies
3. Consider platform portability (Linux/macOS/Windows)
4. Write tests following existing test patterns
5. Add example usage to `examples/` if user-facing
6. Update `README.md` API reference section
7. Follow memory management patterns (create/destroy pairing)

## Quick Reference

**Start server**: `http_server_create()` → `http_server_set_router()` → `http_server_listen()` → cleanup  
**Add route**: `router_add_route(router, method, path, handler)`  
**JSON creation**: `json_object_create()` → `json_object_set()` → `json_value_free()`  
**Async mode**: `http_server_set_async(true)` before `http_server_listen()`  
**Event loop backends**: epoll (Linux) > kqueue (BSD/macOS) > poll (fallback)  
**Static limits**: 128 connections, 256 routes, 32 middlewares, 1024 events, 64 timers
