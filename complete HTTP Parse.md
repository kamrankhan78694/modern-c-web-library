# Complete HTTP Parser Implementation Plan

## Goals
- Replace the placeholder `parse_request` with a robust HTTP/1.1 parser that supports headers, query strings, request bodies (Content-Length + chunked), and method validation.
- Ensure both threaded and async I/O paths share the same parsing logic with incremental buffering and graceful error handling.
- Populate request/response header maps and route parameters so middleware and handlers observe real data.
- Maintain the pure C, dependency-free philosophy while keeping code understandable for future contributors.

## Context & Constraints
- Current request structs (`http_request`, `http_response`) expose `void *headers` and `void *params` but nothing is wired.
- Both sync (`handle_connection`) and async (`async_read_handler`) assume the entire request fits in a single read; the new parser must operate incrementally.
- Buffer size is fixed at 8192 bytes. Add safeguards for larger payloads (reject rather than overflow) and allow streaming bodies for handlers later.
- Router currently expects `http_request_get_param` to return values; plan should unblock `TODO` items by the end.
- Tests presently cover only basic object construction. New tests must exercise parsing behaviour end-to-end via sockets wherever possible.

## Deliverables Overview
1. Request/response header storage utilities (simple hash table or linked list lookup).
2. Incremental HTTP parser with clear states: start line, headers, body (length-based or chunked), completion.
3. Connection management updates to loop on `recv`/`send`, handle keep-alive, and surface parser results.
4. Error handling paths (400, 413, 414, 426 etc.) with informative logs.
5. Updated router parameter extraction + helpers.
6. Documentation and tests (unit + integration) covering all code paths.

## Phase 1 – Groundwork
1. **Audit Structures**: Revisit `http_request`/`http_response` definitions in `include/weblib.h`. Decide on internal header representation (e.g., linked list of `key/value` pairs with case-insensitive comparison). Document expectations in comments for future maintainers.
2. **Utility Module**: Either extend `http_server.c` with static helpers or add a new internal file (`src/http_headers.c`) to store/retrieve headers, normalize keys, and free resources. Keep this internal (no public API changes beyond the existing helper functions).
3. **Memory Helpers**: Define functions for request lifecycle (init/reset/free partial state) to reuse between sync/async code paths.

## Phase 2 – Parser Design
1. **State Machine Definition**: Draft an enum capturing parsing stages (`REQUEST_LINE`, `HEADERS`, `BODY`, `COMPLETE`, `ERROR`).
2. **Parser Context**: Introduce a struct (`http_parser_t`) hung off the connection (both sync and async) that tracks current state, buffer offsets, header counts, method, path, etc.
3. **API**: Implement `int http_parser_execute(http_parser_t *parser, const char *data, size_t len)` returning bytes consumed, completion flag, or error code.
4. **Request Line Parsing**: Validate method tokens, enforce HTTP version (`HTTP/1.0` and `HTTP/1.1`), limit path length (e.g., 4096), and capture query string. Reject invalid methods with 405/501.

## Phase 3 – Header Handling
1. **Header Canonicalization**: Convert incoming header keys to lowercase (or store original + lowercase copy) for case-insensitive lookup.
2. **Storage**: Insert key/value into request header list; detect duplicates requiring merging (e.g., `Cookie`) or override.
3. **Essential Headers**: Track `Host`, `Content-Length`, `Transfer-Encoding`, `Connection`, `Expect`, etc. Validate numeric values and conflicting directives.
4. **`http_request_get_header`**: Implement lookup using the new storage. Update header setter for response to allocate storage and include in `send_response`.

## Phase 4 – Body Parsing
1. **Content-Length**: When present, read exactly N bytes into `req->body`. Enforce configurable maximum (initially reuse `BUFFER_SIZE`, but plan for future streaming by letting parser mark `body_incomplete`).
2. **Chunked Encoding**: Implement chunk size parsing, accumulate until final zero chunk, handle extensions and CRLF validation.
3. **Unsupported Cases**: Reject message bodies for methods that should not have them when `Content-Length > 0` (e.g., GET unless `Expect: 100-continue`).
4. **Expect Header**: For `Expect: 100-continue`, respond with 100 interim when we support streaming; until then, reject with 417 to keep behaviour explicit.

## Phase 5 – Connection Lifecycle Updates
1. **Sync Path**: Replace single `recv` call in `handle_connection` with loop calling `http_parser_execute` until request complete, error, or client closes. Introduce timeouts (e.g., configurable idle limit) to avoid hanging threads.
2. **Async Path**: Store parser context in `async_connection_t`; feed data chunks as they arrive. Once complete, transition to write state.
3. **Keep-Alive**: Honour `Connection: keep-alive` for HTTP/1.1 default. After sending response, either reuse connection (loop back to parser reset) or close on `Connection: close`. Keep initial implementation simple (close after each response) but document path for future improvement.
4. **Send Loop**: Replace single `send` calls with loops handling partial writes and `EAGAIN` for async mode.

## Phase 6 – Error Handling & Responses
1. Map parser errors to HTTP status codes with short explanations:
	- Malformed start line → 400
	- Unsupported method → 405 / 501
	- Header line too long / total headers exceed cap → 431
	- Body size exceeds limit → 413
	- Missing Host in HTTP/1.1 → 400
2. Ensure errors set `res->sent` and close connection. Log with `fprintf(stderr, ...)` for now.
3. Provide consistent default headers (Date, Connection) in error replies; ensure `Content-Length` matches body.

## Phase 7 – Router & Middleware Integration
1. **Route Params**: Implement `extract_params` to populate `req->params` using a small key/value list. Update `http_request_get_param` to read from this storage.
2. **Query String Helpers**: Optionally add helper to parse query string into map (future enhancement). Document expectation but keep out of parser scope to maintain focus.
3. **Response Headers**: Implement `http_response_set_header` to store headers for serialization in `send_response`. Ensure duplicates (e.g., `Set-Cookie`) are supported.

## Phase 8 – Testing Strategy
1. **Unit Tests** (`tests/test_weblib.c` or new file):
	- Parse valid GET/POST requests with headers and bodies.
	- Reject malformed start lines, invalid Content-Length, mixed chunked + length, oversize headers.
	- Validate response header setting.
2. **Integration Tests**: Spin up the threaded server on random port, send raw HTTP requests via socket, assert responses. Cover keep-alive, 400/413 errors, and chunked body.
3. **Async Path Smoke Test**: Start async example, send request to ensure parser works in event loop path (one or two cases to avoid long runtime).
4. **Valgrind**: Run under valgrind (via existing Docker scripts) to confirm no leaks from header storage.

## Phase 9 – Documentation & Cleanup
1. Update `README.md` usage examples to reflect available headers/params and limitations.
2. Revise `TODO.md` once features land (mark “Complete HTTP Parser” task as done).
3. Add inline comments near non-obvious logic (state transitions, chunked parsing) per project guidance (keep concise).
4. Ensure public API comments in `weblib.h` mention header lookup behaviour and error responses for malformed requests.

## Dependencies & Sequencing Notes
- Phase 1 must precede parser work so shared data structures exist.
- Parser (Phase 2–4) should be developed alongside connection loop changes to validate assumptions early.
- Router/middleware changes depend on parser populating headers/params; coordinate to avoid broken tests mid-way.
- Test suite can be built incrementally but defer network integration tests until parser + connection updates are stable.
- Keep commits scoped per phase so rollbacks are easy for future maintainers or AI agents.

## Risk Mitigations
- Establish maximum limits (header count, header line length, body size) and enforce via constants to avoid DoS vectors.
- Validate all allocations; on failure, send 500 and close connection to avoid undefined behaviour.
- Keep parser self-contained to simplify future refactors or streaming body support.

## Out of Scope (Future Work)
- HTTP/2, TLS, upgraded protocols.
- Full streaming body support with handler callbacks.
- Request pipelining and connection pooling.

Following this plan sequentially will provide a production-ready HTTP parsing layer while leaving clear extension hooks for advanced features.
