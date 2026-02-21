/*
 * rest_api_server.c - Full CRUD REST API example
 *
 * Demonstrates production patterns: input validation, JSON request/response,
 * middleware (logging, CORS, rate limiting, error handling), health check,
 * and metrics collection.
 *
 * Endpoints:
 *   GET    /api/items       — list all items
 *   GET    /api/items/:id   — get single item
 *   POST   /api/items       — create item (JSON body: {"name":"...","value":N})
 *   PUT    /api/items/:id   — update item (JSON body: {"name":"...","value":N})
 *   DELETE /api/items/:id   — delete item
 *   GET    /healthz         — health check
 *   GET    /metrics         — request metrics
 *
 * Build:
 *   cd build && cmake .. && make rest_api_server
 *
 * Run:
 *   ./examples/rest_api_server [port]   # default 8080
 */

#include "weblib.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>

/* --------------- In-memory data store --------------- */

#define MAX_ITEMS 256
#define MAX_NAME_LEN 128

typedef struct {
    int id;
    char name[MAX_NAME_LEN];
    double value;
    bool active;
} item_t;

static item_t items[MAX_ITEMS];
static int next_id = 1;

static void store_init(void) {
    memset(items, 0, sizeof(items));
    next_id = 1;
}

static item_t *store_find(int id) {
    for (int i = 0; i < MAX_ITEMS; i++) {
        if (items[i].active && items[i].id == id)
            return &items[i];
    }
    return NULL;
}

static item_t *store_create(const char *name, double value) {
    for (int i = 0; i < MAX_ITEMS; i++) {
        if (!items[i].active) {
            items[i].id = next_id++;
            snprintf(items[i].name, MAX_NAME_LEN, "%s", name);
            items[i].value = value;
            items[i].active = true;
            return &items[i];
        }
    }
    return NULL; /* store full */
}

static bool store_delete(int id) {
    item_t *it = store_find(id);
    if (!it)
        return false;
    it->active = false;
    return true;
}

/* --------------- JSON helpers --------------- */

static json_value_t *item_to_json(const item_t *it) {
    json_value_t *obj = json_object_create();
    if (!obj)
        return NULL;
    json_object_set(obj, "id", json_number_create((double)it->id));
    json_object_set(obj, "name", json_string_create(it->name));
    json_object_set(obj, "value", json_number_create(it->value));
    return obj;
}

/* --------------- Route handlers --------------- */

/* GET /api/items — list all items */
static void handle_list_items(http_request_t *req, http_response_t *res) {
    (void)req;
    json_value_t *arr = json_array_create();
    if (!arr) {
        http_response_send_text(res, HTTP_INTERNAL_ERROR, "out of memory");
        return;
    }
    for (int i = 0; i < MAX_ITEMS; i++) {
        if (items[i].active)
            json_array_append(arr, item_to_json(&items[i]));
    }
    json_value_t *root = json_object_create();
    json_object_set(root, "items", arr);
    json_object_set(root, "count", json_number_create((double)json_array_length(arr)));
    http_response_send_json(res, HTTP_OK, root);
    json_value_free(root);
}

/* GET /api/items/:id — get single item */
static void handle_get_item(http_request_t *req, http_response_t *res) {
    const char *id_str = http_request_get_param(req, "id");
    if (!id_str) {
        http_response_send_text(res, HTTP_BAD_REQUEST, "{\"error\":\"missing id\"}");
        return;
    }
    long long id_val = 0;
    if (!input_validate_integer(id_str, 1, MAX_ITEMS * 1000, &id_val)) {
        http_response_send_text(res, HTTP_BAD_REQUEST, "{\"error\":\"invalid id\"}");
        return;
    }
    item_t *it = store_find((int)id_val);
    if (!it) {
        http_response_send_text(res, HTTP_NOT_FOUND, "{\"error\":\"not found\"}");
        return;
    }
    json_value_t *json = item_to_json(it);
    http_response_send_json(res, HTTP_OK, json);
    json_value_free(json);
}

/* POST /api/items — create item */
static void handle_create_item(http_request_t *req, http_response_t *res) {
    if (!req->body || req->body_length == 0) {
        http_response_send_text(res, HTTP_BAD_REQUEST, "{\"error\":\"empty body\"}");
        return;
    }
    json_value_t *body = json_parse(req->body);
    if (!body) {
        http_response_send_text(res, HTTP_BAD_REQUEST, "{\"error\":\"invalid JSON\"}");
        return;
    }
    json_value_t *name_val = json_object_get(body, "name");
    json_value_t *value_val = json_object_get(body, "value");
    if (!name_val || name_val->type != JSON_STRING) {
        json_value_free(body);
        http_response_send_text(res, HTTP_BAD_REQUEST,
            "{\"error\":\"name is required and must be a string\"}");
        return;
    }
    const char *name = name_val->data.string_val;
    if (!input_validate_length(name, 1, MAX_NAME_LEN - 1)) {
        json_value_free(body);
        http_response_send_text(res, HTTP_BAD_REQUEST,
            "{\"error\":\"name must be 1-127 characters\"}");
        return;
    }
    double value = (value_val && value_val->type == JSON_NUMBER) ?
                   value_val->data.number_val : 0.0;

    item_t *it = store_create(name, value);
    json_value_free(body);
    if (!it) {
        http_response_send_text(res, HTTP_INTERNAL_ERROR,
            "{\"error\":\"store full\"}");
        return;
    }
    json_value_t *json = item_to_json(it);
    http_response_send_json(res, HTTP_CREATED, json);
    json_value_free(json);
}

/* PUT /api/items/:id — update item */
static void handle_update_item(http_request_t *req, http_response_t *res) {
    const char *id_str = http_request_get_param(req, "id");
    if (!id_str) {
        http_response_send_text(res, HTTP_BAD_REQUEST, "{\"error\":\"missing id\"}");
        return;
    }
    long long id_val = 0;
    if (!input_validate_integer(id_str, 1, MAX_ITEMS * 1000, &id_val)) {
        http_response_send_text(res, HTTP_BAD_REQUEST, "{\"error\":\"invalid id\"}");
        return;
    }
    item_t *it = store_find((int)id_val);
    if (!it) {
        http_response_send_text(res, HTTP_NOT_FOUND, "{\"error\":\"not found\"}");
        return;
    }
    if (!req->body || req->body_length == 0) {
        http_response_send_text(res, HTTP_BAD_REQUEST, "{\"error\":\"empty body\"}");
        return;
    }
    json_value_t *body = json_parse(req->body);
    if (!body) {
        http_response_send_text(res, HTTP_BAD_REQUEST, "{\"error\":\"invalid JSON\"}");
        return;
    }
    json_value_t *name_val = json_object_get(body, "name");
    json_value_t *value_val = json_object_get(body, "value");
    if (name_val && name_val->type == JSON_STRING) {
        if (!input_validate_length(name_val->data.string_val, 1, MAX_NAME_LEN - 1)) {
            json_value_free(body);
            http_response_send_text(res, HTTP_BAD_REQUEST,
                "{\"error\":\"name must be 1-127 characters\"}");
            return;
        }
        snprintf(it->name, MAX_NAME_LEN, "%s", name_val->data.string_val);
    }
    if (value_val && value_val->type == JSON_NUMBER)
        it->value = value_val->data.number_val;
    json_value_free(body);

    json_value_t *json = item_to_json(it);
    http_response_send_json(res, HTTP_OK, json);
    json_value_free(json);
}

/* DELETE /api/items/:id — delete item */
static void handle_delete_item(http_request_t *req, http_response_t *res) {
    const char *id_str = http_request_get_param(req, "id");
    if (!id_str) {
        http_response_send_text(res, HTTP_BAD_REQUEST, "{\"error\":\"missing id\"}");
        return;
    }
    long long id_val = 0;
    if (!input_validate_integer(id_str, 1, MAX_ITEMS * 1000, &id_val)) {
        http_response_send_text(res, HTTP_BAD_REQUEST, "{\"error\":\"invalid id\"}");
        return;
    }
    if (!store_delete((int)id_val)) {
        http_response_send_text(res, HTTP_NOT_FOUND, "{\"error\":\"not found\"}");
        return;
    }
    http_response_send_text(res, HTTP_OK, "{\"deleted\":true}");
}

/* --------------- Globals for signal handling --------------- */

static http_server_t *g_server = NULL;
static volatile sig_atomic_t shutdown_requested = 0;

static void signal_handler(int signum) {
    (void)signum;
    shutdown_requested = 1;
}

/* --------------- Main --------------- */

int main(int argc, char *argv[]) {
    uint16_t port = 8080;
    if (argc > 1) {
        port = (uint16_t)atoi(argv[1]);
        if (port == 0) {
            fprintf(stderr, "Invalid port number\n");
            return 1;
        }
    }

    store_init();

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    /* Create server and router */
    g_server = http_server_create();
    if (!g_server) {
        fprintf(stderr, "Failed to create server\n");
        return 1;
    }
    http_server_set_timeout(g_server, 30, 30);
    http_server_set_thread_count(g_server, 16);

    router_t *router = router_create();
    if (!router) {
        fprintf(stderr, "Failed to create router\n");
        http_server_destroy(g_server);
        return 1;
    }

    /* Production middleware stack */
    log_config_t log_cfg = {0};
    log_cfg.level = LOG_LEVEL_INFO;
    log_cfg.output = stderr;
    middleware_fn_t logger = log_middleware_create(&log_cfg);
    if (logger)
        router_use_middleware(router, logger);

    cors_options_t cors_cfg = {0};
    cors_cfg.allowed_origins = NULL; /* NULL means allow all origins */
    cors_cfg.allowed_methods = "GET,POST,PUT,DELETE,OPTIONS";
    cors_cfg.allowed_headers = "Content-Type,Authorization";
    cors_cfg.max_age = 86400;
    middleware_fn_t cors = cors_middleware_create(&cors_cfg);
    if (cors)
        router_use_middleware(router, cors);

    ratelimit_config_t rl_cfg = {0};
    rl_cfg.requests_per_window = 100;
    rl_cfg.window_seconds = 60;
    rl_cfg.burst_size = 100;
    middleware_fn_t rl = ratelimit_middleware_create(&rl_cfg);
    if (rl)
        router_use_middleware(router, rl);

    middleware_fn_t err = error_handler_middleware_create(NULL);
    if (err)
        router_use_middleware(router, err);

    middleware_fn_t metrics = metrics_middleware_create();
    if (metrics)
        router_use_middleware(router, metrics);

    /* CRUD routes */
    router_add_route(router, HTTP_GET, "/api/items", handle_list_items);
    router_add_route(router, HTTP_GET, "/api/items/:id", handle_get_item);
    router_add_route(router, HTTP_POST, "/api/items", handle_create_item);
    router_add_route(router, HTTP_PUT, "/api/items/:id", handle_update_item);
    router_add_route(router, HTTP_DELETE, "/api/items/:id", handle_delete_item);

    /* Observability endpoints */
    health_check_register(router);
    metrics_register(router);

    http_server_set_router(g_server, router);

    printf("REST API server starting on port %d\n", port);
    printf("Endpoints:\n");
    printf("  GET    /api/items       — list items\n");
    printf("  GET    /api/items/:id   — get item\n");
    printf("  POST   /api/items       — create item\n");
    printf("  PUT    /api/items/:id   — update item\n");
    printf("  DELETE /api/items/:id   — delete item\n");
    printf("  GET    /healthz         — health check\n");
    printf("  GET    /metrics         — request metrics\n");
    printf("\nPress Ctrl+C to stop.\n\n");

    if (http_server_listen(g_server, port) < 0) {
        fprintf(stderr, "Failed to start server on port %d\n", port);
        router_destroy(router);
        http_server_destroy(g_server);
        metrics_middleware_destroy();
        cors_middleware_destroy();
        ratelimit_middleware_destroy();
        log_middleware_destroy();
        error_handler_middleware_destroy();
        return 1;
    }

    while (!shutdown_requested)
        sleep(1);

    printf("\nShutting down...\n");
    http_server_stop(g_server);

    /* Cleanup */
    router_destroy(router);
    http_server_destroy(g_server);
    metrics_middleware_destroy();
    cors_middleware_destroy();
    ratelimit_middleware_destroy();
    log_middleware_destroy();
    error_handler_middleware_destroy();

    printf("Server stopped.\n");
    return 0;
}
