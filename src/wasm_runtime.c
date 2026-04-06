/*
 * wasm_runtime.c - WebAssembly Runtime Compatibility Layer
 *
 * Provides WASM-specific initialization, exported API wrappers, and
 * stub implementations for platform-dependent features unavailable
 * in WASM environments (sockets, real threading, etc.).
 *
 * When compiled with Emscripten (emcc), the WASM_EXPORT macro expands
 * to EMSCRIPTEN_KEEPALIVE, ensuring key functions are not dead-code
 * eliminated and remain callable from JavaScript.
 *
 * WASM-safe components (usable from JS/WASM host):
 *   - JSON parser/serializer
 *   - URL router / route matching
 *   - Template engine
 *   - Input validation
 *   - Cookie parsing
 *   - Body parsing
 *   - Compression utilities
 *
 * Copyright (c) 2024 Modern C Web Library
 * Licensed under MIT License
 */

#include "kamran.k"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ===== WASM Capability Query ===== */

static const char *WASM_CAPABILITY_STRING =
    "weblib-wasm/1.0 (json,router,template,validation,cookie,body_parser,compression)";

WASM_EXPORT
const char *wasm_weblib_version(void) {
    return WEBLIB_VERSION_STRING;
}

WASM_EXPORT
const char *wasm_weblib_capabilities(void) {
    return WASM_CAPABILITY_STRING;
}

WASM_EXPORT
bool wasm_weblib_has_capability(const char *name) {
    if (!name) return false;

    /* List of capabilities available in WASM builds */
    static const char *caps[] = {
        "json", "router", "template", "validation",
        "cookie", "body_parser", "compression", NULL
    };

    for (int i = 0; caps[i]; i++) {
        if (strcmp(caps[i], name) == 0) return true;
    }
    return false;
}

/* ===== JSON API Wrappers ===== */

WASM_EXPORT
json_value_t *wasm_json_parse(const char *input) {
    if (!input) return NULL;
    return json_parse(input);
}

WASM_EXPORT
char *wasm_json_stringify(json_value_t *value) {
    if (!value) return NULL;
    return json_stringify(value);
}

WASM_EXPORT
json_value_t *wasm_json_object_create(void) {
    return json_object_create();
}

WASM_EXPORT
json_value_t *wasm_json_array_create(void) {
    return json_array_create();
}

WASM_EXPORT
json_value_t *wasm_json_string_create(const char *value) {
    return json_string_create(value);
}

WASM_EXPORT
json_value_t *wasm_json_number_create(double value) {
    return json_number_create(value);
}

WASM_EXPORT
json_value_t *wasm_json_bool_create(bool value) {
    return json_bool_create(value);
}

WASM_EXPORT
json_value_t *wasm_json_null_create(void) {
    return json_null_create();
}

WASM_EXPORT
void wasm_json_object_set(json_value_t *obj, const char *key,
                          json_value_t *value) {
    json_object_set(obj, key, value);
}

WASM_EXPORT
json_value_t *wasm_json_object_get(json_value_t *obj, const char *key) {
    return json_object_get(obj, key);
}

WASM_EXPORT
void wasm_json_array_push(json_value_t *arr, json_value_t *value) {
    json_array_append(arr, value);
}

WASM_EXPORT
void wasm_json_free(json_value_t *value) {
    json_value_free(value);
}

/* ===== Router API Wrappers ===== */

WASM_EXPORT
router_t *wasm_router_create(void) {
    return router_create();
}

WASM_EXPORT
void wasm_router_destroy(router_t *router) {
    router_destroy(router);
}

WASM_EXPORT
int wasm_router_add_route(router_t *router, http_method_t method,
                          const char *path, route_handler_t handler) {
    return router_add_route(router, method, path, handler);
}

/* ===== Input Validation Wrappers ===== */

WASM_EXPORT
bool wasm_validate_email(const char *email) {
    return input_validate_email(email);
}

WASM_EXPORT
bool wasm_validate_integer(const char *str, int *out) {
    if (!str || !out) return false;
    long long val = 0;
    bool ok = input_validate_integer(str, -2147483648LL, 2147483647LL, &val);
    if (ok) *out = (int)val;
    return ok;
}

WASM_EXPORT
bool wasm_validate_string_length(const char *str, size_t min, size_t max) {
    return input_validate_length(str, min, max);
}

/* ===== Template API Wrappers ===== */

WASM_EXPORT
template_context_t *wasm_template_context_create(void) {
    return template_context_create();
}

WASM_EXPORT
void wasm_template_context_set(template_context_t *ctx, const char *name,
                               const char *value) {
    template_context_set(ctx, name, value);
}

WASM_EXPORT
char *wasm_template_render(const char *tmpl, template_context_t *ctx) {
    return template_render(tmpl, ctx);
}

WASM_EXPORT
void wasm_template_context_destroy(template_context_t *ctx) {
    template_context_destroy(ctx);
}

/* ===== Memory Management ===== */

WASM_EXPORT
void wasm_free(void *ptr) {
    free(ptr);
}
