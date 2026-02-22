# Building a REST API with Modern C Web Library

## Introduction

In this tutorial, we'll build a complete REST API for managing a collection of tasks. Our API will support full CRUD (Create, Read, Update, Delete) operations with proper HTTP methods, JSON request/response handling, and production-ready middleware.

**What we'll build:**
- A task management API with the following endpoints:
  - `POST /api/tasks` - Create a new task
  - `GET /api/tasks` - List all tasks
  - `GET /api/tasks/:id` - Get a specific task
  - `PUT /api/tasks/:id` - Update a task
  - `DELETE /api/tasks/:id` - Delete a task
- JSON request/response handling
- Error handling with appropriate HTTP status codes
- Production middleware (logging, CORS, rate limiting, health checks)

## Project Setup

First, create a new project directory and set up your `CMakeLists.txt`:

```cmake
cmake_minimum_required(VERSION 3.10)
project(TaskAPI C)

set(CMAKE_C_STANDARD 11)

# Find the Modern C Web Library
find_package(weblib REQUIRED)

add_executable(task_api main.c)

# Link against the web library
target_link_libraries(task_api PRIVATE weblib::weblib)

# If weblib is in a local build directory
# include_directories(${CMAKE_SOURCE_DIR}/path/to/weblib/include)
# link_directories(${CMAKE_SOURCE_DIR}/path/to/weblib/build)
# target_link_libraries(task_api PRIVATE weblib pthread)
```

## Data Model

Let's define our task structure and an in-memory store:

```c
#include <weblib/http_server.h>
#include <weblib/router.h>
#include <weblib/json.h>
#include <weblib/middleware.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_TASKS 100
#define MAX_TITLE_LEN 256
#define MAX_DESC_LEN 1024

// Task structure
typedef struct {
    int id;
    char title[MAX_TITLE_LEN];
    char description[MAX_DESC_LEN];
    int completed;
} Task;

// In-memory task store
static Task tasks[MAX_TASKS];
static int task_count = 0;
static int next_id = 1;

// Helper function to find task by ID
static Task* find_task_by_id(int id) {
    for (int i = 0; i < task_count; i++) {
        if (tasks[i].id == id) {
            return &tasks[i];
        }
    }
    return NULL;
}
```

## Create Operation (POST /api/tasks)

The create operation parses JSON from the request body, validates it, adds a new task, and returns HTTP 201 Created:

```c
void create_task_handler(HttpRequest* req, HttpResponse* res, void* user_data) {
    // Parse request body as JSON
    const char* body = http_request_get_body(req);
    if (!body) {
        http_response_set_status(res, HTTP_BAD_REQUEST);
        http_response_send_text(res, "Missing request body");
        return;
    }

    JsonValue* json = json_parse(body);
    if (!json || json->type != JSON_OBJECT) {
        http_response_set_status(res, HTTP_BAD_REQUEST);
        http_response_send_text(res, "Invalid JSON");
        if (json) json_value_free(json);
        return;
    }

    // Extract fields
    JsonValue* title_val = json_object_get((JsonObject*)json, "title");
    JsonValue* desc_val = json_object_get((JsonObject*)json, "description");
    
    if (!title_val || title_val->type != JSON_STRING) {
        http_response_set_status(res, HTTP_BAD_REQUEST);
        http_response_send_text(res, "Missing or invalid 'title' field");
        json_value_free(json);
        return;
    }

    // Check capacity
    if (task_count >= MAX_TASKS) {
        http_response_set_status(res, HTTP_INTERNAL_SERVER_ERROR);
        http_response_send_text(res, "Task store full");
        json_value_free(json);
        return;
    }

    // Create new task
    Task* task = &tasks[task_count++];
    task->id = next_id++;
    strncpy(task->title, ((JsonString*)title_val)->value, MAX_TITLE_LEN - 1);
    task->title[MAX_TITLE_LEN - 1] = '\0';
    
    if (desc_val && desc_val->type == JSON_STRING) {
        strncpy(task->description, ((JsonString*)desc_val)->value, MAX_DESC_LEN - 1);
        task->description[MAX_DESC_LEN - 1] = '\0';
    } else {
        task->description[0] = '\0';
    }
    
    task->completed = 0;

    // Build response JSON
    JsonObject* response = json_object_create();
    json_object_set(response, "id", json_number_create(task->id));
    json_object_set(response, "title", json_string_create(task->title));
    json_object_set(response, "description", json_string_create(task->description));
    json_object_set(response, "completed", json_number_create(task->completed));

    http_response_set_status(res, HTTP_CREATED);
    http_response_send_json(res, (JsonValue*)response);

    json_value_free((JsonValue*)response);
    json_value_free(json);
}
```

## Read All (GET /api/tasks)

Return all tasks as a JSON array:

```c
void get_all_tasks_handler(HttpRequest* req, HttpResponse* res, void* user_data) {
    JsonArray* tasks_array = json_array_create();

    for (int i = 0; i < task_count; i++) {
        JsonObject* task_obj = json_object_create();
        json_object_set(task_obj, "id", json_number_create(tasks[i].id));
        json_object_set(task_obj, "title", json_string_create(tasks[i].title));
        json_object_set(task_obj, "description", json_string_create(tasks[i].description));
        json_object_set(task_obj, "completed", json_number_create(tasks[i].completed));
        
        json_array_append(tasks_array, (JsonValue*)task_obj);
    }

    http_response_set_status(res, HTTP_OK);
    http_response_send_json(res, (JsonValue*)tasks_array);
    json_value_free((JsonValue*)tasks_array);
}
```

## Read One (GET /api/tasks/:id)

Retrieve a single task by ID using route parameters:

```c
void get_task_handler(HttpRequest* req, HttpResponse* res, void* user_data) {
    // Get ID from route parameter
    const char* id_str = http_request_get_param(req, "id");
    if (!id_str) {
        http_response_set_status(res, HTTP_BAD_REQUEST);
        http_response_send_text(res, "Missing task ID");
        return;
    }

    int id = atoi(id_str);
    Task* task = find_task_by_id(id);

    if (!task) {
        http_response_set_status(res, HTTP_NOT_FOUND);
        http_response_send_text(res, "Task not found");
        return;
    }

    // Build response
    JsonObject* task_obj = json_object_create();
    json_object_set(task_obj, "id", json_number_create(task->id));
    json_object_set(task_obj, "title", json_string_create(task->title));
    json_object_set(task_obj, "description", json_string_create(task->description));
    json_object_set(task_obj, "completed", json_number_create(task->completed));

    http_response_set_status(res, HTTP_OK);
    http_response_send_json(res, (JsonValue*)task_obj);
    json_value_free((JsonValue*)task_obj);
}
```

## Update (PUT /api/tasks/:id)

Update an existing task:

```c
void update_task_handler(HttpRequest* req, HttpResponse* res, void* user_data) {
    const char* id_str = http_request_get_param(req, "id");
    if (!id_str) {
        http_response_set_status(res, HTTP_BAD_REQUEST);
        http_response_send_text(res, "Missing task ID");
        return;
    }

    int id = atoi(id_str);
    Task* task = find_task_by_id(id);

    if (!task) {
        http_response_set_status(res, HTTP_NOT_FOUND);
        http_response_send_text(res, "Task not found");
        return;
    }

    // Parse request body
    const char* body = http_request_get_body(req);
    if (!body) {
        http_response_set_status(res, HTTP_BAD_REQUEST);
        http_response_send_text(res, "Missing request body");
        return;
    }

    JsonValue* json = json_parse(body);
    if (!json || json->type != JSON_OBJECT) {
        http_response_set_status(res, HTTP_BAD_REQUEST);
        http_response_send_text(res, "Invalid JSON");
        if (json) json_value_free(json);
        return;
    }

    // Update fields if provided
    JsonValue* title_val = json_object_get((JsonObject*)json, "title");
    if (title_val && title_val->type == JSON_STRING) {
        strncpy(task->title, ((JsonString*)title_val)->value, MAX_TITLE_LEN - 1);
        task->title[MAX_TITLE_LEN - 1] = '\0';
    }

    JsonValue* desc_val = json_object_get((JsonObject*)json, "description");
    if (desc_val && desc_val->type == JSON_STRING) {
        strncpy(task->description, ((JsonString*)desc_val)->value, MAX_DESC_LEN - 1);
        task->description[MAX_DESC_LEN - 1] = '\0';
    }

    JsonValue* completed_val = json_object_get((JsonObject*)json, "completed");
    if (completed_val && completed_val->type == JSON_NUMBER) {
        task->completed = (int)((JsonNumber*)completed_val)->value;
    }

    // Build response
    JsonObject* task_obj = json_object_create();
    json_object_set(task_obj, "id", json_number_create(task->id));
    json_object_set(task_obj, "title", json_string_create(task->title));
    json_object_set(task_obj, "description", json_string_create(task->description));
    json_object_set(task_obj, "completed", json_number_create(task->completed));

    http_response_set_status(res, HTTP_OK);
    http_response_send_json(res, (JsonValue*)task_obj);

    json_value_free((JsonValue*)task_obj);
    json_value_free(json);
}
```

## Delete (DELETE /api/tasks/:id)

Remove a task from the store:

```c
void delete_task_handler(HttpRequest* req, HttpResponse* res, void* user_data) {
    const char* id_str = http_request_get_param(req, "id");
    if (!id_str) {
        http_response_set_status(res, HTTP_BAD_REQUEST);
        http_response_send_text(res, "Missing task ID");
        return;
    }

    int id = atoi(id_str);
    
    // Find and remove task
    for (int i = 0; i < task_count; i++) {
        if (tasks[i].id == id) {
            // Shift remaining tasks down
            for (int j = i; j < task_count - 1; j++) {
                tasks[j] = tasks[j + 1];
            }
            task_count--;
            
            http_response_set_status(res, HTTP_NO_CONTENT);
            http_response_send_text(res, "");
            return;
        }
    }

    http_response_set_status(res, HTTP_NOT_FOUND);
    http_response_send_text(res, "Task not found");
}
```

## Adding Production Middleware

Production APIs need logging, CORS support, rate limiting, and error handling:

```c
void setup_middleware(Router* router) {
    // Health check endpoint for monitoring
    health_check_register(router);

    // Add logging middleware to track requests
    router_use(router, logging_middleware, NULL);

    // CORS middleware for cross-origin requests
    CorsConfig cors_config = {
        .allowed_origins = "*",
        .allowed_methods = "GET,POST,PUT,DELETE,OPTIONS",
        .allowed_headers = "Content-Type,Authorization",
        .max_age = 3600
    };
    router_use(router, cors_middleware, &cors_config);

    // Rate limiting to prevent abuse
    RateLimitConfig rate_config = {
        .max_requests = 100,
        .window_seconds = 60
    };
    router_use(router, rate_limit_middleware, &rate_config);

    // Error handler for consistent error responses
    router_use(router, error_handler_middleware, NULL);
}
```

## Complete Example

Here's the full `main.c` putting it all together:

```c
int main(void) {
    // Create HTTP server
    HttpServer* server = http_server_create("0.0.0.0", 8080);
    if (!server) {
        fprintf(stderr, "Failed to create server\n");
        return 1;
    }

    // Configure server for production
    http_server_set_timeout(server, 30);  // 30-second timeout
    http_server_set_thread_count(server, 4);  // 4 worker threads

    // Create router
    Router* router = router_create();

    // Setup middleware
    setup_middleware(router);

    // Register CRUD routes
    router_add_route(router, HTTP_POST, "/api/tasks", create_task_handler, NULL);
    router_add_route(router, HTTP_GET, "/api/tasks", get_all_tasks_handler, NULL);
    router_add_route(router, HTTP_GET, "/api/tasks/:id", get_task_handler, NULL);
    router_add_route(router, HTTP_PUT, "/api/tasks/:id", update_task_handler, NULL);
    router_add_route(router, HTTP_DELETE, "/api/tasks/:id", delete_task_handler, NULL);

    // Attach router to server
    http_server_set_router(server, router);

    printf("Task API server running on http://localhost:8080\n");
    printf("Endpoints:\n");
    printf("  POST   /api/tasks      - Create task\n");
    printf("  GET    /api/tasks      - List all tasks\n");
    printf("  GET    /api/tasks/:id  - Get task\n");
    printf("  PUT    /api/tasks/:id  - Update task\n");
    printf("  DELETE /api/tasks/:id  - Delete task\n");
    printf("  GET    /health         - Health check\n");

    // Start server (blocking)
    http_server_start(server);

    // Cleanup
    router_destroy(router);
    http_server_destroy(server);
    
    return 0;
}
```

## Testing with curl

Once your server is running, test each endpoint:

### Create a task
```bash
curl -X POST http://localhost:8080/api/tasks \
  -H "Content-Type: application/json" \
  -d '{"title":"Buy groceries","description":"Milk, eggs, bread"}'

# Response: {"id":1,"title":"Buy groceries","description":"Milk, eggs, bread","completed":0}
```

### Get all tasks
```bash
curl http://localhost:8080/api/tasks

# Response: [{"id":1,"title":"Buy groceries",...}]
```

### Get a specific task
```bash
curl http://localhost:8080/api/tasks/1

# Response: {"id":1,"title":"Buy groceries","description":"Milk, eggs, bread","completed":0}
```

### Update a task
```bash
curl -X PUT http://localhost:8080/api/tasks/1 \
  -H "Content-Type: application/json" \
  -d '{"completed":1}'

# Response: {"id":1,"title":"Buy groceries","description":"Milk, eggs, bread","completed":1}
```

### Delete a task
```bash
curl -X DELETE http://localhost:8080/api/tasks/1

# Response: 204 No Content
```

### Check health
```bash
curl http://localhost:8080/health

# Response: {"status":"healthy"}
```

### Test with verbose output
```bash
curl -v http://localhost:8080/api/tasks
# Shows full HTTP headers including CORS headers
```

## Summary

You've now built a complete REST API with the Modern C Web Library! This tutorial covered:

✅ **CRUD Operations** - All five standard REST operations  
✅ **JSON Handling** - Parsing requests and building responses  
✅ **Route Parameters** - Dynamic URL segments with `:id`  
✅ **HTTP Status Codes** - Proper 200, 201, 204, 400, 404 responses  
✅ **Production Middleware** - Logging, CORS, rate limiting, health checks  
✅ **Error Handling** - Graceful validation and error responses  
✅ **Performance** - Multi-threaded server with configurable timeouts  

### Next Steps

- **Persistence**: Replace the in-memory store with a database (SQLite, PostgreSQL)
- **Authentication**: Add JWT or session-based authentication middleware
- **Validation**: Implement comprehensive input validation
- **Documentation**: Generate OpenAPI/Swagger documentation
- **Testing**: Add unit tests and integration tests
- **Deployment**: Containerize with Docker and deploy to production

For more examples, see:
- `examples/rest_api_server.c` - Full REST API implementation
- `examples/middleware_demo.c` - Advanced middleware patterns
- `docs/api/` - Complete API reference

Happy coding! 🚀
