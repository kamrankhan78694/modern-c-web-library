/*
 * wasm_example.c - WebAssembly Example for Modern C Web Library
 *
 * Demonstrates the WASM-safe subset of the library:
 *   - JSON parsing and serialization
 *   - URL router / route matching
 *   - Template rendering
 *   - Input validation
 *
 * Native build:
 *   gcc -o wasm_example wasm_example.c -I../include -L../build -lweblib
 *   ./wasm_example
 *
 * Emscripten (WASM) build:
 *   emcc -o wasm_example.js wasm_example.c -I../include -L../build -lweblib \
 *        -sEXPORTED_RUNTIME_METHODS=ccall,cwrap -sWASM=1
 *
 * Copyright (c) 2024 Modern C Web Library
 * Licensed under MIT License
 */

#include "kamran.k"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Demo route handler */
static void api_handler(http_request_t *req, http_response_t *res) {
    (void)req;
    (void)res;
    printf("  [handler] /api/data route matched\n");
}

int main(void) {
    printf("=== Modern C Web Library — WASM Example ===\n\n");

    /* 1. Library info */
    printf("1. Library Version: %s\n", wasm_weblib_version());
    printf("   Capabilities:    %s\n", wasm_weblib_capabilities());
    printf("   Has JSON:        %s\n",
           wasm_weblib_has_capability("json") ? "yes" : "no");
    printf("   Has Router:      %s\n",
           wasm_weblib_has_capability("router") ? "yes" : "no");
    printf("\n");

    /* 2. JSON parsing */
    printf("2. JSON Parsing\n");
    const char *json_input = "{\"name\":\"Kamran\",\"version\":1,\"wasm\":true}";
    printf("   Input:  %s\n", json_input);

    json_value_t *parsed = wasm_json_parse(json_input);
    if (parsed) {
        char *output = wasm_json_stringify(parsed);
        printf("   Parsed: %s\n", output ? output : "(null)");
        wasm_free(output);
        wasm_json_free(parsed);
    }
    printf("\n");

    /* 3. JSON construction */
    printf("3. JSON Construction\n");
    json_value_t *obj = wasm_json_object_create();
    wasm_json_object_set(obj, "library", wasm_json_string_create("weblib"));
    wasm_json_object_set(obj, "wasm", wasm_json_bool_create(true));

    json_value_t *features = wasm_json_array_create();
    wasm_json_array_push(features, wasm_json_string_create("json"));
    wasm_json_array_push(features, wasm_json_string_create("router"));
    wasm_json_array_push(features, wasm_json_string_create("template"));
    wasm_json_object_set(obj, "features", features);

    char *json_str = wasm_json_stringify(obj);
    printf("   Built:  %s\n", json_str ? json_str : "(null)");
    wasm_free(json_str);
    wasm_json_free(obj);
    printf("\n");

    /* 4. Router */
    printf("4. Router\n");
    router_t *router = wasm_router_create();
    wasm_router_add_route(router, HTTP_GET, "/api/data", api_handler);
    wasm_router_add_route(router, HTTP_POST, "/api/data", api_handler);
    printf("   Added GET  /api/data\n");
    printf("   Added POST /api/data\n");
    wasm_router_destroy(router);
    printf("\n");

    /* 5. Input validation */
    printf("5. Input Validation\n");
    printf("   Email 'user@example.com': %s\n",
           wasm_validate_email("user@example.com") ? "valid" : "invalid");
    printf("   Email 'not-an-email':     %s\n",
           wasm_validate_email("not-an-email") ? "valid" : "invalid");

    int num = 0;
    wasm_validate_integer("42", &num);
    printf("   Integer '42':             %d\n", num);
    printf("   String 'hello' [1,10]:    %s\n",
           wasm_validate_string_length("hello", 1, 10) ? "valid" : "invalid");
    printf("\n");

    /* 6. Template rendering */
    printf("6. Template Rendering\n");
    template_context_t *ctx = wasm_template_context_create();
    wasm_template_context_set(ctx, "name", "WebAssembly");
    wasm_template_context_set(ctx, "author", "Kamran");

    char *rendered = wasm_template_render(
        "Hello {{name}}! Built by {{author}}.", ctx);
    printf("   Template: Hello {{name}}! Built by {{author}}.\n");
    printf("   Rendered: %s\n", rendered ? rendered : "(null)");
    wasm_free(rendered);
    wasm_template_context_destroy(ctx);

    printf("\n=== Done ===\n");
    return 0;
}
