# Test Audit Report — `tests/test_weblib.c`

Each test reviewed for **false greens** — tests that pass but don't actually validate what they claim to test.

## Scope of this audit

This report covers **`tests/test_weblib.c` only**. It does not cover the other ctest suites —
`test_stress`, `test_kamran_header`, `test_async_websocket`, `test_worker`, `test_wasm`, or the seven
TLS suites (`TlsTests`, `TlsCryptoTests`, `TlsParseTests`, `TlsTransportTests`, `TlsFuzzTests`,
`TlsHttpTests`, `TlsInteropOpenssl`).

The audit itself was performed against the **129 tests** present at the time (commits `839b244` /
`cc665d9`). `tests/test_weblib.c` now runs **173 tests**. The 37 added between that audit and test
166 are listed in their own table below with the position they occupy in `main()`; they have **not**
been reviewed for false greens. Six more were added after that table was written and are named at
the end of it — 37 + 6 accounts for the gap between 129 and 172.

Two things to know about the main table before you read it. The `#` column is audit order, which
for tests added mid-list no longer matches current `main()` order. And the **Printed name** column
records the name as it stood at audit time — where a 🔴 row's fix included a rename, the new name is
in the Notes, not the column (tests 19, 29 and 113).

## Legend

| Symbol | Meaning |
|--------|---------|
| ✅ | **OK** — test name matches what it verifies |
| ⚠️ | **Weak** — test is honest but coverage is thin |
| 🔴 | **False green** — test name/comment claims something that is never verified; fixed in this PR |
| ❓ | **Not yet audited** — added after this audit; no false-green review has been done |

---

## Test-by-test audit

| # | Function | Printed name | Verdict | Notes |
|---|----------|-------------|---------|-------|
| 1 | `test_router_create` | `router_create` | ✅ | Creates router, asserts non-NULL, destroys. |
| 2 | `test_router_add_route` | `router_add_route` | ✅ | Adds route, asserts return code 0. |
| 3 | `test_json_object_create` | `json_object_create` | ✅ | Asserts type == JSON_OBJECT. |
| 4 | `test_json_string_create` | `json_string_create` | ✅ | Asserts type and value match. |
| 5 | `test_json_number_create` | `json_number_create` | ✅ | Asserts type and exact numeric value. |
| 6 | `test_json_bool_create` | `json_bool_create` | 🔴→✅ | Was only testing `true`. **Fixed**: added `false` case with assertion `bool_val == false`. |
| 7 | `test_json_object_operations` | `json_object_set/get` | ✅ | Sets key, retrieves, verifies value. |
| 8 | `test_json_stringify` | `json_stringify` | 🔴→✅ | Was missing assertions for "active" and "true" fields. **Fixed**: added both checks. |
| 9 | `test_json_parse_string` | `json_parse (string)` | ✅ | Parses and verifies string value. |
| 10 | `test_json_parse_number` | `json_parse (number)` | ✅ | Exact value check. |
| 11 | `test_json_parse_bool` | `json_parse (bool)` | ✅ | Tests both true and false. |
| 12 | `test_json_parse_null` | `json_parse (null)` | ✅ | Verifies type == JSON_NULL. |
| 13 | `test_json_parse_object` | `json_parse (object)` | ✅ | Parses object, verifies both fields. |
| 14 | `test_server_create` | `http_server_create` | ✅ | Create/destroy. |
| 15 | `test_event_loop_create` | `event_loop_create` | ✅ | Create/destroy. |
| 16 | `test_server_async_mode` | `http_server_set_async` | ✅ | Enable → event loop exists; disable → NULL. |
| 17 | `test_event_loop_timeout` | `event_loop_add_timeout` | 🔴 | Sets `timeout_called = false` and registers a callback that sets it `true`, but **never runs the event loop** and **never asserts `timeout_called`**. Dead code creates illusion of verification. **Fixed**: replaced dead code with timer-count assertion. |
| 18 | `test_event_loop_cancel_timeout` | `event_loop_cancel_timeout` | ✅ | Adds then cancels; checks return code. |
| 19 | `test_websocket_frame_encode` | `websocket_frame_encode` | 🔴 | Name says "frame encode" but **only creates a connection and checks `is_open`** — identical to test 20. Zero frame-encoding logic is tested. **Fixed**: renamed to `websocket_connection_open_state`, added `get_fd` and post-close assertions. |
| 20 | `test_websocket_connection_create` | `websocket_connection_create` | ✅ | Creates conn, checks is_open, tests user_data round-trip. |
| 21 | `test_websocket_handshake_key` | `websocket_handshake_key` | ⚠️ | Only tests failure path (no headers). Comment admits this openly. |
| 22 | `test_json_array_create` | `json_array_create` | ✅ | Type and length==0. |
| 23 | `test_json_array_append_get` | `json_array_append/get` | ✅ | Multi-type append, get by index, bounds check. |
| 24 | `test_json_parse_array` | `json_parse (array)` | ✅ | Parses `[1,2,3]`, verifies length and values. |
| 25 | `test_json_parse_array_mixed` | `json_parse (mixed array)` | ✅ | Verifies all 4 element types. |
| 26 | `test_json_array_stringify` | `json_stringify (array)` | ✅ | Exact string match `[1,2,3]`. |
| 27 | `test_json_nested_array` | `json_parse (nested array)` | ✅ | Deep element verification. |
| 28 | `test_json_object_with_array` | `json_parse (object with array)` | ✅ | Object containing array, verified. |
| 29 | `test_body_parser_urlencoded` | `body_parser (url-encoded)` | 🔴 | Supplies URL-encoded body but **no Content-Type header**, so parser does nothing. Name claims "url-encoded" parsing works, but the actual parsing path is never exercised. **Fixed**: renamed to `body_parser (no Content-Type → no fields)`, added assertions that no fields were parsed. |
| 30 | `test_body_parser_data_structures` | `body_parser (data structures)` | ✅ | Manual struct creation/free, NULL free safety. |
| 31 | `test_body_parser_empty` | `body_parser (empty body)` | ✅ | Empty body → NULL fields/files. |
| 32 | `test_body_parser_null` | `body_parser (null handling)` | ✅ | All NULL safety checks. |
| 33 | `test_cookie_get` | `cookie (get from request)` | ⚠️ | Only negative/NULL cases. No positive case (actual cookie extraction). Limited by test infrastructure. |
| 34 | `test_cookie_set` | `cookie (set on response)` | 🔴 | Sets a cookie with domain, path, max_age, secure, httponly, samesite but **never checks the `Set-Cookie` header value**. Comment says "verified by the fact it doesn't crash". **Fixed**: added header content assertions. |
| 35 | `test_cookie_delete` | `cookie (delete)` | 🔴 | Comment says "Should set `Set-Cookie: session=; Path=/; Max-Age=0`" but **never verifies any of that**. **Fixed**: added assertions for `session=` and `Max-Age=0` in header. |
| 36 | `test_cors_create_destroy` | `cors (create/destroy)` | ✅ | NULL→NULL, valid→non-NULL, double destroy safe. |
| 37 | `test_cors_handler` | `cors (handler)` | ⚠️ | Only tests no-Origin pass-through. Doesn't test actual CORS behavior with an Origin header. |
| 38 | `test_ratelimit_create_destroy` | `ratelimit (create/destroy)` | ✅ | NULL, invalid, valid, double destroy. |
| 39 | `test_static_file_create_destroy` | `static_file (create/destroy)` | ✅ | Multiple error cases and valid. |
| 40 | `test_static_file_serve` | `static_file (serve file)` | ✅ | Creates file, serves, verifies status + body content + length. |
| 41 | `test_static_file_not_found` | `static_file (not found)` | ✅ | Non-existent file → continue chain. |
| 42 | `test_static_file_path_traversal` | `static_file (path traversal)` | ✅ | Traversal blocked. |
| 43 | `test_http_status_codes` | `http_status_codes (new codes)` | ✅ | Enum value checks. |
| 44 | `test_session_store_create_destroy` | `session_store (create/destroy)` | ✅ | Create, destroy, NULL-destroy safe. |
| 45 | `test_session_create_get` | `session (create/get)` | ✅ | Create, retrieve, verify ID match, not-expired. |
| 46 | `test_session_data_operations` | `session (data operations)` | ✅ | Set, get, update, remove, isolation verified. |
| 47 | `test_session_destroy_session` | `session (destroy session)` | ✅ | Destroy then verify gone. |
| 48 | `test_session_expiration` | `session (expiration)` | ⚠️ | Only tests non-expiring session (max_age=0). Never tests that a short-lived session actually expires. |
| 49 | `test_session_cleanup` | `session (cleanup expired)` | 🔴 | Only tests that a non-expiring session **survives** cleanup. **Never creates an expired session and verifies it gets cleaned up.** The primary purpose of `session_cleanup_expired` is untested. **Fixed**: added expired-session creation, sleep, and verify cleanup count==1. |
| 50 | `test_session_null_handling` | `session (null handling)` | ✅ | All NULL safety. |
| 51 | `test_session_cookie_set` | `session (cookie set)` | 🔴 | Sets and "deletes" session cookies but **never checks what headers were actually set**. Just a crash test. **Fixed**: added header content assertions for both set and delete paths. |
| 52 | `test_template_create_destroy` | `template (create/destroy)` | ✅ | Create, destroy, NULL destroy. |
| 53 | `test_template_variables` | `template (variables)` | ✅ | Set, get, update, missing-key. |
| 54 | `test_template_render` | `template (render)` | ✅ | Substitution, no-vars, unknown-var, NULL. |
| 55 | `test_template_load_file` | `template (load file)` | ✅ | File→load→render→verify, non-existent, NULL. |
| 56 | `test_basic_auth_create_destroy` | `basic_auth (create/destroy)` | ✅ | NULL config, missing verify, double destroy. |
| 57 | `test_apikey_auth_create_destroy` | `apikey_auth (create/destroy)` | ✅ | Same pattern. |
| 58 | `test_jwt_auth_create_destroy` | `jwt_auth (create/destroy)` | ✅ | NULL config, NULL secret, zero len. |
| 59 | `test_db_pool_create_destroy` | `db_pool (create/destroy)` | ✅ | NULL, missing conn string, valid, NULL destroy. |
| 60 | `test_db_pool_acquire_release` | `db_pool (acquire/release)` | ✅ | Acquire, validate, release, check stats. |
| 61 | `test_json_escape_decode` | `json_parse (escape decoding)` | ✅ | Thorough: \n \t \\ \" \r \b \f \/ \uXXXX, and invalid \q. |
| 62 | `test_json_stringify_escapes` | `json_stringify (escape encoding)` | ✅ | Round-trip verification for special chars. |
| 63 | `test_json_stringify_key_escape` | `json_stringify (key escaping)` | ✅ | Key with quotes is escaped. |
| 64 | `test_json_unterminated` | `json_parse (unterminated)` | ✅ | Unterminated object/array/string rejected. |
| 65 | `test_json_trailing_garbage` | `json_parse (trailing garbage)` | ✅ | Trailing chars rejected, whitespace OK. |
| 66 | `test_json_keyword_termination` | `json_parse (keyword termination)` | ✅ | "trueness", "nullable", "falsehood" rejected. |
| 67 | `test_json_null_create` | `json_null_create` | ✅ | Create, stringify → "null". |
| 68 | `test_json_depth_limit` | `json_parse (depth limit)` | ✅ | 600-deep nesting rejected, moderate OK. |
| 69 | `test_session_expired_cleanup_on_get` | `session (expired auto-cleanup on get)` | ✅ | Creates 1s session, sleeps 2s, get→NULL. |
| 70 | `test_router_null_path` | `router (null path safety)` | ✅ | NULL req → returns -1. |
| 71 | `test_event_loop_timer_safety` | `event_loop (timer safety)` | ✅ | Add/cancel multiple timers, double-cancel fails. |
| 72 | `test_thread_pool_create_destroy` | `thread_pool (create/destroy)` | ✅ | Create, check pending==0, destroy. |
| 73 | `test_thread_pool_null_handling` | `thread_pool (null handling)` | ✅ | NULL pool, NULL work function. |
| 74 | `test_thread_pool_submit_and_complete` | `thread_pool (submit 100 items)` | ✅ | Submits 100, destroy waits, counter==100. |
| 75 | `test_thread_pool_clamp_limits` | `thread_pool (clamp limits)` | ✅ | 0, 999, 0-queue all succeed. |
| 76 | `test_server_set_timeout` | `http_server_set_timeout` | ✅ | Defaults, custom, zero, negative, NULL. |
| 77 | `test_server_set_thread_count` | `http_server_set_thread_count` | ✅ | Valid, min clamp, max clamp, NULL. |
| 78 | `test_server_state` | `http_server_get_state` | ✅ | Initial state STOPPED, NULL→STOPPED. |
| 79 | `test_log_middleware_create_destroy` | `log_middleware (create/destroy)` | ✅ | Default, custom, NULL config. |
| 80 | `test_log_middleware_invoke` | `log_middleware (invoke)` | 🔴 | Creates a log middleware with a tmpfile sink, invokes it, but **never reads the tmpfile to verify anything was logged**. **Fixed**: added `ftell` + `fread` assertions to verify log output contains request info. |
| 81 | `test_error_handler_create_destroy` | `error_handler_middleware (create/destroy)` | ✅ | Create with NULL, custom config. |
| 82 | `test_error_handler_apply` | `error_handler_apply` | ✅ | 404 → body with "404"/"Not Found", 200 → no change, NULL safe. |
| 83 | `test_csrf_create_destroy` | `csrf_middleware (create/destroy)` | ✅ | NULL and custom config. |
| 84 | `test_csrf_safe_methods` | `csrf_middleware (safe methods pass through)` | ✅ | GET returns true (allowed). |
| 85 | `test_input_validate_length` | `input_validate_length` | ✅ | Comprehensive range and edge cases. |
| 86 | `test_input_validate_charset` | `input_validate_charset` | ✅ | Valid, invalid, empty, NULL. |
| 87 | `test_input_validate_integer` | `input_validate_integer` | ✅ | In-range, out-of-range, non-numeric, leading space, trailing char, empty, NULL, NULL out_val. |
| 88 | `test_input_validate_email` | `input_validate_email` | ✅ | Valid and many invalid patterns. |
| 89 | `test_input_is_alphanumeric` | `input_is_alphanumeric` | ✅ | Valid, space, punctuation, empty, NULL. |
| 90 | `test_input_sanitize_html` | `input_sanitize_html` | ✅ | `<>&'"` all escaped, plain text unchanged, NULL. |
| 91 | `test_parser_duplicate_transfer_encoding` | `parser (duplicate Transfer-Encoding → 400)` | ✅ | Live server, duplicate TE → 400, good request → success. |
| 92 | `test_integration_get_200` | `integration (GET → 200)` | ✅ | Live server, GET → "HTTP/1.1 200". |
| 93 | `test_integration_not_found` | `integration (GET unknown path → 404)` | ✅ | Unknown path → "404". |
| 94 | `test_integration_post_body` | `integration (POST with body → echo)` | 🔴 | Named "echo" but **only checks for "200" status, never verifies the body "hello world" was echoed back**. An implementation that returns 200 with empty body would pass. **Fixed**: added `strstr(buf, "hello world")` assertion. |
| 95 | `test_integration_json_response` | `integration (GET → JSON response)` | ✅ | Checks 200, `"message"`, `"hello"`. |
| 96 | `test_integration_malformed_request` | `integration (malformed request → 400)` | ✅ | Garbage → 400 or 501. |
| 97 | `test_integration_concurrent_connections` | `integration (sequential connections)` | ✅ | 5 sequential requests all get 200. |
| 98 | `test_health_check_register` | `health_check (register)` | ✅ | Register OK, NULL → -1. |
| 99 | `test_health_check_endpoint` | `health_check (GET /healthz → 200 JSON)` | ✅ | Live server, checks "status", "ok", "uptime_seconds". |
| 100 | `test_cache_create_destroy` | `cache (create/destroy)` | ✅ | Create, count==0, destroy, NULL, zero-capacity. |
| 101 | `test_cache_set_get` | `cache (set/get)` | ✅ | Set, get, update, missing, NULL safety. |
| 102 | `test_cache_delete` | `cache (delete)` | ✅ | Delete existing, delete non-existent, NULL. |
| 103 | `test_cache_clear` | `cache (clear)` | ✅ | Clear 3 items, verify count==0 and get==NULL. |
| 104 | `test_cache_lru_eviction` | `cache (LRU eviction)` | ✅ | Capacity-3 cache, verify eviction order with access pattern. |
| 105 | `test_cache_ttl` | `cache (TTL expiration)` | ✅ | TTL=1s, sleep 2s, verify expired; TTL=0 persists. |
| 106 | `test_metrics_create_destroy` | `metrics_middleware (create/destroy)` | ✅ | Create, double-create fails, destroy, re-create OK. |
| 107 | `test_metrics_register` | `metrics (register)` | ✅ | NULL → -1, valid → 0. |
| 108 | `test_metrics_record_status` | `metrics (record_status)` | 🔴 | Records status codes but **never verifies they were stored**. Only tests no-crash after destroy. **Fixed**: added `metrics_handler` call and verified response body contains `"total_requests"`. |
| 109 | `test_metrics_endpoint` | `metrics (GET /metrics → 200 JSON)` | ✅ | Live server, checks "total_requests", "methods", "uptime_seconds". |
| 110 | `test_crc32_compute` | `crc32_compute` | ✅ | Known value for "123456789", NULL, empty. |
| 111 | `test_compression_negotiate` | `compression_negotiate` | ✅ | gzip accepted, quality, rejected, missing, NULL, wildcard. |
| 112 | `test_compression_should_compress` | `compression_should_compress` | ✅ | Text types yes, binary no, size threshold, NULL. |
| 113 | `test_gzip_compress_valid` | `gzip (compress produces valid output)` | 🔴 | Named "gzip compress produces valid output" but **only tests CRC32** (already tested in #110). `gzip_compress` is not in the public API and is never called. **Fixed**: replaced with `http_response_send_compressed` call, verify gzip magic bytes or fallback body match; renamed to `gzip (compress via send_compressed)`. |
| 114 | `test_benchmark_timestamp` | `benchmark_timestamp_us` | ✅ | Non-zero, monotonic. |
| 115 | `test_benchmark_stats` | `benchmark (NULL handling)` | ✅ | NULL path, NULL stats, zero requests. |
| 116 | `test_benchmark_print` | `benchmark_print` | ✅ | NULL fp, NULL stats, /dev/null. |
| 117 | `test_benchmark_integration` | `benchmark (live server)` | ✅ | Live server, 5 requests, verify stats. |
| 118 | `test_sigpipe_handling` | `BUG-1: SIGPIPE handling` | ✅ | Verifies SIG_IGN after server create. |
| 119 | `test_session_store_thread_safety` | `BUG-2: session store thread safety` | ⚠️ | Named "thread safety" but only tests sequential operations on single thread. Does verify two-session isolation. |
| 120 | `test_event_loop_timer_count` | `BUG-5: event_loop timer count query` | ✅ | Initial 0, add → 1, NULL → -1, max==64. |
| 121 | `test_server_connection_tracking` | `BUG-6: server connection tracking` | ✅ | Initial 0, set_max, invalid values, NULL. |
| 122 | `test_middleware_user_data` | `BUG-4: middleware user_data (multiple instances)` | ✅ | Same function, different user_data, both counters increment. |
| 123 | `test_middleware_null_user_data` | `BUG-4: middleware NULL user_data (backward compat)` | ✅ | NULL user_data → counter stays 0. |
| 124 | `test_middleware_global_fallback` | `BUG-4: CORS global fallback (backward compat)` | ✅ | CORS with NULL user_data passes through. |
| 125 | `test_kamran_signature` | `kamran_signature (author watermark)` | ✅ | Signature contains author and "weblib". |
| 126 | `test_kamran_server_header` | `kamran_server_header (no duplicate Server headers)` | ✅ | Live server, watermark present, no duplicate. |
| 127 | `test_kamran_error_response_header` | `kamran_error_response (404 carries Server watermark)` | ✅ | 404 response has Server watermark. |
| 128 | `test_kamran_override_user_server_header` | `kamran_override (user Server header replaced by watermark)` | ✅ | Custom Server header overridden, no duplicate. |
| 129 | `test_kamran_multiple_servers` | `kamran_multiple_servers (create/destroy cycles safe)` | ✅ | 5 create/destroy cycles, signature still works. |

---

## Tests added since this audit — not yet reviewed

These 37 tests were added to `tests/test_weblib.c` after the audit above. They all pass, but none has
been checked for false greens, so treat the descriptions as "what the test is named", not "what the
test was verified to assert". `main() #` is the test's position in the current `main()` (1–166).

| main() # | Function | Printed name | Verdict |
|---|----------|-------------|---------|
| 9 | `test_json_number_precision` | `json_stringify (number precision / round-trip)` | ❓ |
| 12 | `test_json_parse_locale_independent` | `json_parse/stringify (locale-independent numbers)` | ❓ |
| 39 | `test_cors_rejects_wildcard_with_credentials` | `cors refuses wildcard origin + credentials (CWE-942)` | ❓ |
| 40 | `test_cors_wildcard_does_not_reflect` | `cors wildcard emits '*' and never reflects Origin / credentials` | ❓ |
| 51 | `test_session_get_data_owned_copy` | `session_get_data returns an independent owned copy (UAF-safe)` | ❓ |
| 52 | `test_session_data_ops_on_dead_session` | `session data ops on destroyed/unknown session fail safely` | ❓ |
| 53 | `test_session_keyed_access_reclaims_expired` | `keyed data access reclaims an expired session slot` | ❓ |
| 54 | `test_session_cookie_idle_timeout` | `session-cookie (max_age=0) idle-timeout reclamation` | ❓ |
| 64 | `test_template_autoescape` | `template (auto-escape / raw)` | ❓ |
| 67 | `test_sha256_kat` | `sha256 (RFC 6234 known-answer)` | ❓ |
| 68 | `test_hmac_sha256_kat` | `hmac_sha256 (RFC 4231 known-answer)` | ❓ |
| 69 | `test_base64_kat` | `base64_decode (RFC 4648 known-answer)` | ❓ |
| 71 | `test_jwt_auth_verify` | `jwt_auth (verify: alg/exp/nbf)` | ❓ |
| 74 | `test_db_pool_destroy_race` | `db_pool (destroy with checked-out + blocked acquirers)` | ❓ |
| 75 | `test_db_pool_double_release` | `db_pool (double release is a safe no-op)` | ❓ |
| 127 | `test_compression_negotiate_locale` | `compression_negotiate (locale-independent q-value)` | ❓ |
| 146 | `test_env_config_get_string` | `env_config_get (string accessor)` | ❓ |
| 147 | `test_env_config_get_int` | `env_config_get_int (integer accessor)` | ❓ |
| 148 | `test_env_config_get_bool` | `env_config_get_bool (boolean accessor)` | ❓ |
| 149 | `test_env_config_get_port` | `env_config_get_port (port accessor)` | ❓ |
| 150 | `test_env_config_require` | `env_config_require (required variable)` | ❓ |
| 151 | `test_env_config_server_apply` | `http_server_apply_env (server integration)` | ❓ |
| 152 | `test_env_config_is_set` | `env_config_is_set (presence check)` | ❓ |
| 153 | `test_env_secure_value_lifecycle` | `env_secure_value lifecycle (get/read/free)` | ❓ |
| 154 | `test_env_secure_value_missing` | `env_secure_value (missing variable returns NULL)` | ❓ |
| 155 | `test_env_secure_value_null_safety` | `env_secure_value (NULL-safety on accessors)` | ❓ |
| 156 | `test_env_secure_value_wipe` | `env_secure_value (memory wipe on free)` | ❓ |
| 157 | `test_env_config_redact` | `env_config_redact (log-safe masking)` | ❓ |
| 158 | `test_env_config_redact_integration` | `env_config_redact (integration with secure value)` | ❓ |
| 159 | `test_secure_zero` | `secure_zero (memory wipe)` | ❓ |
| 160 | `test_secure_compare` | `secure_compare (constant-time comparison)` | ❓ |
| 161 | `test_secure_random_bytes` | `secure_random_bytes (CSPRNG)` | ❓ |
| 162 | `test_security_headers_create_destroy` | `security_headers_middleware (create/destroy)` | ❓ |
| 163 | `test_parser_cl_before_te_smuggling` | `parser (Content-Length before Transfer-Encoding → 400)` | ❓ |
| 164 | `test_header_injection_rejected` | `header injection (CRLF in value → rejected)` | ❓ |
| 165 | `test_websocket_fragment_oom` | `websocket fragment buffer OOM → clean close` | ❓ |
| 166 | `test_websocket_oversized_frame` | `websocket oversized frame rejected (DoS guard)` | ❓ |

Six further tests were added by the response-hook work and are not numbered above:

| Test | Covers |
|------|--------|
| `test_metrics_status_counted_through_router` | #136: a real route through `router_route()` moves the status counters |
| `test_metrics_totals_match_status_classes` | `total_requests` equals the sum of the status classes |
| `test_metrics_identity_holds_for_1xx` | a `101` upgrade does not break that identity |
| `test_metrics_middleware_only_still_counts` | middleware without `metrics_register()` still counts |
| `test_response_hook_registration_is_idempotent` | a duplicate hook is not installed twice |
| `test_undecodable_path_param_refuses_request` | `%00` in a path parameter → 400, handler not run |

Unlike the rows above, each of these was verified to FAIL with its fix reverted, so none is a false
green.

---

## Summary

| Category | Count |
|----------|-------|
| ✅ OK | 111 |
| ⚠️ Weak (honest but thin) | 5 |
| 🔴→✅ False green (fixed) | 13 |
| **Total audited** | **129** |
| ❓ Added since this audit, not yet reviewed | 37 |
| **Tests in `test_weblib.c` today** | **166** |

The ✅ figure was previously written as 109, which did not sum to 129. Tallying the verdict column of
the table above gives 111 ✅ / 5 ⚠️ / 13 🔴→✅.

### False greens fixed in this PR (commit `cc665d9`)

1. **test_event_loop_timeout** — dead `timeout_called` code never asserted
2. **test_websocket_frame_encode** — tested connection creation, not frame encoding
3. **test_body_parser_urlencoded** — named "url-encoded" but Content-Type header missing so parsing never exercised
4. **test_cookie_set** — never verified Set-Cookie header content
5. **test_cookie_delete** — never verified Set-Cookie header content
6. **test_session_cleanup** — never tested that expired sessions are actually cleaned up
7. **test_session_cookie_set** — never verified cookie header content
8. **test_log_middleware_invoke** — never verified log output was written
9. **test_metrics_record_status** — never verified recorded metrics data
10. **test_gzip_compress_valid** — named "gzip compress" but only tested CRC32
11. **test_integration_post_body** — named "echo" but never checked echoed body
12. **test_json_stringify** — missing assertions for "active" and "true" fields
13. **test_json_bool_create** — only tested `true`, not `false`
