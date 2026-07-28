/*
 * benchmark_server.c - Self-contained throughput/latency benchmark
 *
 * The library has shipped a benchmarking module (src/benchmark.c) since v0.9.0,
 * but nothing called it: no example, no test, no CI job. The "Benchmarking
 * Suite" feature therefore produced numbers for nobody. This runner closes that
 * gap - it starts a server, drives benchmark_run() against it, prints the
 * results, and exits, so a baseline is one command away:
 *
 *     ./build/examples/benchmark_server
 *     ./build/examples/benchmark_server --requests 5000 --port 9100
 *
 * WHAT THESE NUMBERS ARE, AND ARE NOT
 *
 * benchmark_run() opens a fresh TCP connection per request and sends one GET,
 * sequentially, from a single thread over loopback. So the figures are a
 * per-request latency floor and a single-client throughput ceiling. They are
 * NOT a capacity or concurrency claim: nothing here measures parallel clients,
 * keep-alive reuse, or behaviour under load. Treat a regression in these
 * numbers as a signal worth investigating, not as a production SLO.
 *
 * NO TLS PATH. The benchmark client speaks plain HTTP only, and it cannot be
 * extended to HTTPS in-process: this library implements TLS 1.3 server-side
 * only - there is no TLS client (src/tls/README.md). Measuring the TLS layer
 * needs an external driver; tests/benchmark_tls.sh does that with
 * `openssl s_client`.
 *
 * Copyright (c) 2024 Modern C Web Library
 * Licensed under MIT License
 */

#include "kamran.k"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#define DEFAULT_PORT      18080
#define DEFAULT_REQUESTS  2000
#define WARMUP_REQUESTS   200

/* ---------- Handlers: one trivial, one representative ---------- */

/* Smallest useful response - isolates request parsing + socket cost. */
static void handle_plain(http_request_t *req, http_response_t *res) {
    (void)req;
    http_response_send_text(res, HTTP_OK, "ok");
}

/* Builds and serialises a small JSON object - the common real workload. */
static void handle_json(http_request_t *req, http_response_t *res) {
    (void)req;
    json_value_t *json = json_object_create();
    if (!json) {
        http_response_send_text(res, HTTP_INTERNAL_ERROR, "oom");
        return;
    }
    json_object_set(json, "status", json_string_create("ok"));
    json_object_set(json, "version", json_string_create(WEBLIB_VERSION));
    http_response_send_json(res, HTTP_OK, json);
    json_value_free(json);
}

/* ---------- Argument parsing ---------- */

/*
 * atoi() cannot fail: it returns 0 for "abc" and silently wraps on overflow, so
 * `--port -1` would become 65535 and `--port abc` would become 0. Parse strictly
 * instead - reject anything that is not a complete, in-range decimal number.
 */
static bool parse_ull(const char *s, unsigned long long lo,
                      unsigned long long hi, unsigned long long *out) {
    char *end = NULL;
    unsigned long long v;

    if (!s || *s == '\0' || *s == '-') {   /* strtoull silently accepts "-1" */
        return false;
    }
    errno = 0;
    v = strtoull(s, &end, 10);
    if (errno != 0 || end == s || *end != '\0') {
        return false;
    }
    if (v < lo || v > hi) {
        return false;
    }
    *out = v;
    return true;
}

/* ---------- Runner ---------- */

static void usage(const char *argv0) {
    fprintf(stderr,
        "Usage: %s [--port N] [--requests N]\n"
        "  --port N       listen port (default %d)\n"
        "  --requests N   requests per scenario (default %d)\n"
        "\n"
        "Threaded mode only. Async mode cannot be driven from one process:\n"
        "http_server_listen() runs the event loop on the calling thread and\n"
        "never returns, leaving no thread to run the client. To measure async,\n"
        "run examples/async_server in one terminal and a client in another.\n",
        argv0, DEFAULT_PORT, DEFAULT_REQUESTS);
}

/*
 * Benchmark one route and report. Returns 0 on success.
 *
 * A warm-up pass runs first and is discarded: the first requests against a
 * fresh server pay one-off costs (thread-pool spin-up, first allocations) that
 * would otherwise land in the max and p99 and make runs incomparable.
 */
static int bench_route(uint16_t port, const char *path, const char *label,
                       uint64_t requests) {
    benchmark_stats_t warm;
    benchmark_stats_t stats;

    if (benchmark_run(port, path, WARMUP_REQUESTS, &warm) != 0) {
        fprintf(stderr, "warm-up failed for %s - is the server up?\n", path);
        return -1;
    }
    if (benchmark_run(port, path, requests, &stats) != 0) {
        fprintf(stderr, "benchmark failed for %s\n", path);
        return -1;
    }

    printf("\n--- %s (GET %s) ---\n", label, path);
    benchmark_print(stdout, &stats);

    if (stats.failed > 0) {
        fprintf(stderr,
                "WARNING: %llu of %llu requests failed - the numbers above are "
                "not meaningful.\n",
                (unsigned long long)stats.failed,
                (unsigned long long)stats.total_requests);
        return -1;
    }
    return 0;
}

int main(int argc, char **argv) {
    uint16_t port = DEFAULT_PORT;
    uint64_t requests = DEFAULT_REQUESTS;

    for (int i = 1; i < argc; i++) {
        unsigned long long v;
        if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            /* 1-65535: port 0 asks the OS to pick, which this runner cannot then connect to. */
            if (!parse_ull(argv[++i], 1, 65535, &v)) {
                fprintf(stderr, "invalid --port '%s' (expected 1-65535)\n\n", argv[i]);
                usage(argv[0]);
                return 1;
            }
            port = (uint16_t)v;
        } else if (strcmp(argv[i], "--requests") == 0 && i + 1 < argc) {
            /* Upper bound is arbitrary but keeps the run finite and the latency
             * sample array a sane size. */
            if (!parse_ull(argv[++i], 1, 10000000ULL, &v)) {
                fprintf(stderr, "invalid --requests '%s' (expected 1-10000000)\n\n", argv[i]);
                usage(argv[0]);
                return 1;
            }
            requests = v;
        } else {
            usage(argv[0]);
            return 1;
        }
    }

    router_t *router = router_create();
    http_server_t *server = http_server_create();
    if (!router || !server) {
        fprintf(stderr, "failed to create server\n");
        router_destroy(router);
        http_server_destroy(server);
        return 1;
    }

    router_add_route(router, HTTP_GET, "/plain", handle_plain);
    router_add_route(router, HTTP_GET, "/json", handle_json);
    http_server_set_router(server, router);

    printf("=== Modern C Web Library - benchmark (threaded) ===\n");
    printf("weblib %s  port %u  %llu requests/scenario "
           "(+%d warm-up, discarded)\n",
           weblib_version(), (unsigned)port,
           (unsigned long long)requests, WARMUP_REQUESTS);
    printf("Sequential, single client, one connection per request, loopback.\n"
           "A latency floor - not a concurrency or capacity measurement.\n");

    if (http_server_listen(server, port) != 0) {
        fprintf(stderr, "failed to listen on port %u (in use?)\n", (unsigned)port);
        http_server_destroy(server);
        router_destroy(router);
        return 1;
    }

    /* Give the accept thread a moment to reach accept() before the first connect. */
    usleep(200000);

    int rc = 0;
    if (bench_route(port, "/plain", "plain text", requests) != 0) rc = 1;
    if (bench_route(port, "/json", "JSON object", requests) != 0) rc = 1;

    printf("\nNo TLS figures: the benchmark client speaks plain HTTP, and this\n"
           "library has no TLS client to extend it with (server-side only).\n"
           "Use tests/benchmark_tls.sh, which drives openssl s_client.\n");

    http_server_stop(server);
    http_server_destroy(server);
    router_destroy(router);
    return rc;
}
