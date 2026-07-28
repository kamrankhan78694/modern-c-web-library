/*
 * demo_app.c - Minimal dev server + demo app, for driving the library in a browser.
 *
 * Deliberately small, but it exercises the real request path rather than a
 * hello-world: router with a path parameter, the JSON engine, a middleware
 * chain (logging -> CORS -> metrics), the built-in /healthz and /metrics
 * endpoints, and both GET and POST with a parsed body.
 *
 * The page it serves calls its own API with fetch(), so opening it in a browser
 * is an end-to-end test of the stack: HTML out, JSON out, JSON in.
 *
 *     ./build/examples/demo_app            # http://localhost:8080
 *     ./build/examples/demo_app 9000       # a different port
 *
 * Plain HTTP only. The TLS layer is EXPERIMENTAL and UNAUDITED (off by default);
 * see examples/tls_server.c for HTTPS, and terminate TLS at a reverse proxy for
 * anything real.
 *
 * Copyright (c) 2024 Modern C Web Library
 * Licensed under MIT License
 */

#include "kamran.k"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>

#define DEFAULT_PORT 8080

static volatile sig_atomic_t g_stop = 0;
static time_t g_started;

static void on_signal(int sig) { (void)sig; g_stop = 1; }

/* ---------------------------------------------------------------------------
 * The page. Served as one static string: the point of the demo is the C server,
 * not a build pipeline, so there is no bundler and nothing to install.
 * ------------------------------------------------------------------------- */
static const char *PAGE =
"<!doctype html><html lang=\"en\"><head><meta charset=\"utf-8\">"
"<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
"<title>Modern C Web Library — demo</title><style>"
":root{color-scheme:light dark;--fg:#111;--dim:#666;--bd:#d8d8d8;--bg:#fff;--acc:#0b6b3a}"
"@media(prefers-color-scheme:dark){:root{--fg:#e8e8e8;--dim:#9a9a9a;--bd:#333;--bg:#111;--acc:#4ade80}}"
"*{box-sizing:border-box}body{margin:0;padding:2.5rem 1.25rem;background:var(--bg);color:var(--fg);"
"font:15px/1.6 ui-sans-serif,system-ui,-apple-system,'Segoe UI',sans-serif}"
".w{max-width:52rem;margin:0 auto}h1{font-size:1.5rem;margin:0 0 .25rem}"
".sub{color:var(--dim);margin:0 0 2rem}"
"code,pre{font-family:ui-monospace,SFMono-Regular,Menlo,monospace;font-size:13px}"
".card{border:1px solid var(--bd);border-radius:10px;padding:1rem 1.25rem;margin:0 0 1rem}"
".card h2{font-size:.8rem;text-transform:uppercase;letter-spacing:.06em;color:var(--dim);margin:0 0 .75rem}"
"pre{background:rgba(127,127,127,.09);padding:.75rem;border-radius:6px;overflow-x:auto;margin:.5rem 0 0}"
"button{font:inherit;padding:.45rem .9rem;border:1px solid var(--bd);border-radius:6px;"
"background:transparent;color:var(--fg);cursor:pointer}button:hover{border-color:var(--acc)}"
"input{font:inherit;padding:.45rem .6rem;border:1px solid var(--bd);border-radius:6px;"
"background:transparent;color:var(--fg);width:14rem}"
".row{display:flex;gap:.5rem;flex-wrap:wrap;align-items:center}"
"a{color:var(--acc)}.ok{color:var(--acc);font-weight:600}"
"</style></head><body><div class=\"w\">"
"<h1>Modern C Web Library</h1>"
"<p class=\"sub\">A pure-C HTTP server with zero external dependencies. This page is served by "
"<code>examples/demo_app.c</code>, and everything below is fetched from its API.</p>"

"<div class=\"card\"><h2>GET /api/info</h2>"
"<div class=\"row\"><button onclick=\"info()\">Refresh</button>"
"<span id=\"st\" class=\"ok\"></span></div><pre id=\"o1\">…</pre></div>"

"<div class=\"card\"><h2>GET /api/greet/:name — path parameter</h2>"
"<div class=\"row\"><input id=\"nm\" value=\"kamran\"><button onclick=\"greet()\">Send</button></div>"
"<pre id=\"o2\">…</pre></div>"

"<div class=\"card\"><h2>POST /api/echo — JSON in, JSON out</h2>"
"<div class=\"row\"><input id=\"msg\" value=\"hello from the browser\"><button onclick=\"echo()\">Post</button></div>"
"<pre id=\"o3\">…</pre></div>"

"<div class=\"card\"><h2>Built in</h2><div class=\"row\">"
"<a href=\"/healthz\" target=\"_blank\">/healthz</a><a href=\"/metrics\" target=\"_blank\">/metrics</a>"
"</div></div>"

"<script>"
"const j=(el,v)=>document.getElementById(el).textContent=JSON.stringify(v,null,2);"
"const fail=(el,e)=>document.getElementById(el).textContent='request failed: '+e;"
"async function info(){try{const r=await fetch('/api/info');const d=await r.json();j('o1',d);"
"document.getElementById('st').textContent='HTTP '+r.status;}catch(e){fail('o1',e)}}"
"async function greet(){try{const n=encodeURIComponent(document.getElementById('nm').value||'world');"
"const r=await fetch('/api/greet/'+n);j('o2',await r.json());}catch(e){fail('o2',e)}}"
"async function echo(){try{const r=await fetch('/api/echo',{method:'POST',"
"headers:{'Content-Type':'application/json'},"
"body:JSON.stringify({message:document.getElementById('msg').value,from:'browser'})});"
"j('o3',await r.json());}catch(e){fail('o3',e)}}"
"info();greet();echo();"
"</script></div></body></html>";

/* ---------------------------------------------------------------------------
 * Handlers
 * ------------------------------------------------------------------------- */

static void handle_index(http_request_t *req, http_response_t *res) {
    (void)req;
    http_response_send_text(res, HTTP_OK, PAGE);
    /* ORDER MATTERS. http_response_send_text() sets Content-Type: text/plain,
     * and it adds the header with replace_existing=false — so setting the
     * header *before* the send does not win, it appends a SECOND Content-Type
     * and the response goes out with both (invalid per RFC 9110 and rendered as
     * plain text by the browser). http_response_set_header() replaces, so it has
     * to come after. Tracked as BUG-11 in BUGS.md: send_text should replace, not
     * append, and there is no send_html() helper. */
    http_response_set_header(res, "Content-Type", "text/html; charset=utf-8");
}

static void handle_info(http_request_t *req, http_response_t *res) {
    (void)req;
    json_value_t *o = json_object_create();
    if (!o) { http_response_send_text(res, HTTP_INTERNAL_ERROR, "oom"); return; }

    json_object_set(o, "library", json_string_create("modern-c-web-library"));
    json_object_set(o, "version", json_string_create(WEBLIB_VERSION));
    json_object_set(o, "server_header", json_string_create(WEBLIB_VERSION_STRING));
    json_object_set(o, "uptime_seconds",
                    json_number_create((double)(time(NULL) - g_started)));
    json_object_set(o, "mode", json_string_create("threaded"));
    /* Stated, not implied: this build has no TLS compiled in unless asked for. */
#ifdef WEBLIB_TLS
    json_object_set(o, "tls_compiled_in", json_bool_create(true));
#else
    json_object_set(o, "tls_compiled_in", json_bool_create(false));
#endif
    http_response_send_json(res, HTTP_OK, o);
    json_value_free(o);
}

static void handle_greet(http_request_t *req, http_response_t *res) {
    const char *name = http_request_get_param(req, "name");
    json_value_t *o = json_object_create();
    if (!o) { http_response_send_text(res, HTTP_INTERNAL_ERROR, "oom"); return; }

    /* The value came off the wire. Validate before echoing it back: the
     * template engine auto-escapes, but this path builds JSON by hand. */
    if (!name || !input_validate_length(name, 1, 64)) {
        json_object_set(o, "error",
                        json_string_create("name must be 1-64 characters"));
        http_response_send_json(res, HTTP_BAD_REQUEST, o);
        json_value_free(o);
        return;
    }
    json_object_set(o, "greeting", json_string_create("hello"));
    json_object_set(o, "name", json_string_create(name));
    http_response_send_json(res, HTTP_OK, o);
    json_value_free(o);

    /* Route the decoded path parameter into a response header — deliberately.
     * This is the shape that produces header injection in real applications:
     * attacker-controlled bytes reaching a header value. Doing it here means the
     * end-to-end suite's CRLF check actually exercises the library's guard in
     * header_list_add() rather than passing because nothing in the app ever put
     * a parameter in a header. If that guard regresses, a request for
     * /api/greet/x%0d%0aX-Injected:%20evil starts returning an X-Injected
     * header and the suite fails. The value is validated above (1-64 chars),
     * and the guard is the backstop. */
    http_response_set_header(res, "X-Greeted-Name", name);
}

static void handle_echo(http_request_t *req, http_response_t *res) {
    json_value_t *out = json_object_create();
    if (!out) { http_response_send_text(res, HTTP_INTERNAL_ERROR, "oom"); return; }

    if (!req->body || req->body_length == 0) {
        json_object_set(out, "error", json_string_create("empty request body"));
        http_response_send_json(res, HTTP_BAD_REQUEST, out);
        json_value_free(out);
        return;
    }

    json_value_t *in = json_parse(req->body);
    if (!in) {
        json_object_set(out, "error", json_string_create("invalid JSON"));
        http_response_send_json(res, HTTP_BAD_REQUEST, out);
        json_value_free(out);
        return;
    }

    json_object_set(out, "received_bytes", json_number_create((double)req->body_length));
    json_value_t *msg = json_object_get(in, "message");
    json_object_set(out, "message",
                    json_string_create((msg && msg->type == JSON_STRING)
                                       ? msg->data.string_val : "(no 'message' field)"));
    json_value_free(in);

    http_response_send_json(res, HTTP_OK, out);
    json_value_free(out);
}

/* ------------------------------------------------------------------------- */

int main(int argc, char **argv) {
    uint16_t port = DEFAULT_PORT;

    if (argc > 1) {
        char *end = NULL;
        long v = strtol(argv[1], &end, 10);
        if (!end || *end != '\0' || v < 1 || v > 65535) {
            fprintf(stderr, "usage: %s [port]   (1-65535, default %d)\n",
                    argv[0], DEFAULT_PORT);
            return 1;
        }
        port = (uint16_t)v;
    }

    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    g_started = time(NULL);

    router_t *router = router_create();
    http_server_t *server = http_server_create();
    if (!router || !server) {
        fprintf(stderr, "failed to create server\n");
        router_destroy(router);
        http_server_destroy(server);
        return 1;
    }

    /* Middleware runs in registration order. Logging first so every request is
     * recorded even if a later middleware short-circuits it. */
    middleware_fn_t log_mw = log_middleware_create(NULL);
    if (log_mw) router_use_middleware(router, log_mw);

    /* Wide-open CORS: this is a local dev demo. Name your origins in production —
     * cors_middleware_create() refuses credentials + wildcard outright (CWE-942). */
    cors_options_t cors = {0};
    cors.allowed_origins = NULL;          /* NULL inside the struct = "*" */
    cors.allow_credentials = false;
    middleware_fn_t cors_mw = cors_middleware_create(&cors);
    if (cors_mw) router_use_middleware(router, cors_mw);

    /* No metrics middleware: metrics_register() below installs the response
     * hook that does all the counting, at completion, so total_requests always
     * equals the sum of the status classes. Adding the middleware as well is
     * supported but counts the total on the way IN, which makes a /metrics
     * scrape include itself in the total while its own status lands after the
     * JSON is rendered — the document then disagrees with itself by one. */

    router_add_route(router, HTTP_GET,  "/",                handle_index);
    router_add_route(router, HTTP_GET,  "/api/info",        handle_info);
    router_add_route(router, HTTP_GET,  "/api/greet/:name", handle_greet);
    router_add_route(router, HTTP_POST, "/api/echo",        handle_echo);
    health_check_register(router);        /* GET /healthz */
    metrics_register(router);             /* GET /metrics */

    http_server_set_router(server, router);

    if (http_server_listen(server, port) != 0) {
        fprintf(stderr, "failed to listen on port %u (already in use?)\n",
                (unsigned)port);
        http_server_destroy(server);
        router_destroy(router);
        return 1;
    }

    printf("\n  Modern C Web Library %s — demo app\n", weblib_version());
    printf("  http://localhost:%u/\n\n", (unsigned)port);
    printf("    GET  /                  this page\n");
    printf("    GET  /api/info          version, uptime, whether TLS is compiled in\n");
    printf("    GET  /api/greet/:name   path parameter + input validation\n");
    printf("    POST /api/echo          JSON in, JSON out\n");
    printf("    GET  /healthz           built-in health check\n");
    printf("    GET  /metrics           built-in request metrics\n\n");
    /* Say what the bind actually is. http_server_listen() binds INADDR_ANY, so
     * this is reachable from the network, not just from this machine — the
     * "localhost" URL above is where YOU open it, not the limit of who can. On
     * an untrusted network that exposes an unauthenticated demo that also sends
     * Access-Control-Allow-Origin: *. Worth knowing before running it in a cafe. */
    printf("  Listening on ALL interfaces (0.0.0.0:%u), not only loopback.\n"
           "  This demo has no authentication and permissive CORS — do not run it\n"
           "  on an untrusted network.\n\n", (unsigned)port);
    fflush(stdout);

    /* listen() returns immediately in threaded mode; keep main alive. */
    while (!g_stop) {
        sleep(1);
    }

    printf("\nshutting down\n");
    http_server_stop(server);
    http_server_destroy(server);
    router_destroy(router);
    metrics_middleware_destroy();
    cors_middleware_destroy();
    log_middleware_destroy();
    return 0;
}
