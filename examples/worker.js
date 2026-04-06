/**
 * worker.js - Cloudflare Worker JavaScript Glue
 *
 * Bridges the Cloudflare Workers fetch event to the C/WASM library.
 * The WASM module is compiled from worker_example.c (or your own C code)
 * using Emscripten.
 *
 * Build the WASM module:
 *   emcc -o worker.js worker_example.c -I../include -L../build-wasm -lweblib \
 *        -sEXPORTED_RUNTIME_METHODS=ccall,cwrap \
 *        -sEXPORTED_FUNCTIONS=_worker_init,_worker_fetch,_worker_cleanup,_worker_response_get_status,_worker_response_get_body,_worker_response_get_header_count,_worker_response_get_header_name,_worker_response_get_header_value,_worker_response_destroy,_malloc,_free \
 *        -sWASM=1 -sMODULARIZE=1 -sEXPORT_NAME=createModule
 *
 * Deploy with wrangler (see wrangler.toml in this directory).
 *
 * Copyright (c) 2024 Modern C Web Library
 * Licensed under MIT License
 */

import createModule from "./worker.wasm.js";

let wasmModule = null;

/**
 * Initialise the WASM module on first request.
 */
async function ensureInit() {
    if (wasmModule) return;
    wasmModule = await createModule();
    wasmModule._worker_init();
}

/**
 * Cloudflare Workers fetch handler.
 * Converts the incoming Request into WASM calls and returns a Response.
 */
export default {
    async fetch(request) {
        await ensureInit();

        const url = new URL(request.url);
        const method = request.method;
        const path = url.pathname + url.search;

        /* Allocate strings in WASM memory */
        const methodPtr = wasmModule.allocateUTF8(method);
        const pathPtr = wasmModule.allocateUTF8(path);

        /* Call the C fetch handler */
        const resPtr = wasmModule._worker_fetch(methodPtr, pathPtr);

        /* Free input strings */
        wasmModule._free(methodPtr);
        wasmModule._free(pathPtr);

        if (!resPtr) {
            return new Response("Internal Server Error", { status: 500 });
        }

        /* Read response fields from WASM */
        const status = wasmModule._worker_response_get_status(resPtr);

        const bodyPtr = wasmModule._worker_response_get_body(resPtr);
        const body = bodyPtr ? wasmModule.UTF8ToString(bodyPtr) : "";

        const headerCount = wasmModule._worker_response_get_header_count(resPtr);
        const headers = new Headers();
        for (let i = 0; i < headerCount; i++) {
            const namePtr = wasmModule._worker_response_get_header_name(resPtr, i);
            const valuePtr = wasmModule._worker_response_get_header_value(resPtr, i);
            if (namePtr && valuePtr) {
                headers.set(
                    wasmModule.UTF8ToString(namePtr),
                    wasmModule.UTF8ToString(valuePtr)
                );
            }
        }

        /* Destroy the WASM response */
        wasmModule._worker_response_destroy(resPtr);

        return new Response(body, { status, headers });
    },
};
