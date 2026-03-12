/*
 * benchmark.c - HTTP throughput and latency benchmarking utilities
 *
 * Provides functions for measuring server performance:
 * - High-resolution timestamps via clock_gettime(CLOCK_MONOTONIC)
 * - Sequential HTTP GET benchmark with latency sampling
 * - Percentile statistics (p50/p95/p99)
 *
 * Pure C11, no external dependencies, POSIX + standard library only.
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <float.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include "kamran.k"

/* ============================================================================
 * High-Resolution Timing
 * ============================================================================ */

/**
 * Returns microseconds from a monotonic clock.
 */
uint64_t benchmark_timestamp_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000ULL + (uint64_t)ts.tv_nsec / 1000ULL;
}

/* ============================================================================
 * Sorting Helper (for percentile computation)
 * ============================================================================ */

static int _cmp_double(const void *a, const void *b) {
    double da = *(const double *)a;
    double db = *(const double *)b;
    if (da < db) return -1;
    if (da > db) return  1;
    return 0;
}

/* ============================================================================
 * Benchmark Implementation
 * ============================================================================ */

/**
 * Send a single HTTP GET request and measure latency.
 * @param port   Server port (127.0.0.1)
 * @param path   Request path
 * @param out_us Receives the request latency in microseconds
 * @return HTTP status code (e.g. 200), or -1 on connection/read error
 */
static int _benchmark_one_request(uint16_t port, const char *path,
                                  double *out_us) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd);
        return -1;
    }

    /* Build minimal HTTP/1.1 request */
    char req_buf[512];
    int req_len = snprintf(req_buf, sizeof(req_buf),
        "GET %s HTTP/1.1\r\n"
        "Host: 127.0.0.1:%u\r\n"
        "Connection: close\r\n"
        "\r\n",
        path, (unsigned)port);

    uint64_t t0 = benchmark_timestamp_us();

    /* Send request */
    ssize_t sent = 0;
    while (sent < req_len) {
        ssize_t n = send(fd, req_buf + sent, (size_t)(req_len - sent), 0);
        if (n < 0) {
            if (errno == EINTR) continue;
            close(fd);
            return -1;
        }
        sent += n;
    }

    /* Read response (only need status line) */
    char resp_buf[4096];
    ssize_t total_read = 0;
    while (total_read < (ssize_t)sizeof(resp_buf) - 1) {
        ssize_t n = recv(fd, resp_buf + total_read,
                         sizeof(resp_buf) - 1 - (size_t)total_read, 0);
        if (n <= 0) break;
        total_read += n;
    }

    uint64_t t1 = benchmark_timestamp_us();
    close(fd);

    *out_us = (double)(t1 - t0);

    if (total_read <= 0) return -1;

    resp_buf[total_read] = '\0';

    /* Parse status code: "HTTP/1.1 200 OK\r\n..." */
    int status = 0;
    if (total_read >= 12 && strncmp(resp_buf, "HTTP/", 5) == 0) {
        const char *sp = strchr(resp_buf, ' ');
        if (sp) {
            status = atoi(sp + 1);
        }
    }
    return status > 0 ? status : -1;
}

/**
 * Run a sequential throughput benchmark.
 */
int benchmark_run(uint16_t port, const char *path,
                  uint64_t num_requests, benchmark_stats_t *stats) {
    if (!path || !stats || num_requests == 0) {
        return -1;
    }

    memset(stats, 0, sizeof(*stats));

    /* Allocate latency sample array */
    double *latencies = (double *)calloc(num_requests, sizeof(double));
    if (!latencies) {
        return -1;
    }

    uint64_t successful = 0;
    uint64_t failed = 0;
    double min_us = DBL_MAX;
    double max_us = 0;
    double sum_us = 0;

    uint64_t t_start = benchmark_timestamp_us();

    for (uint64_t i = 0; i < num_requests; i++) {
        double lat = 0;
        int status = _benchmark_one_request(port, path, &lat);

        latencies[i] = lat;
        sum_us += lat;

        if (lat < min_us) min_us = lat;
        if (lat > max_us) max_us = lat;

        if (status >= 200 && status < 300) {
            successful++;
        } else {
            failed++;
        }
    }

    uint64_t t_end = benchmark_timestamp_us();

    /* Compute statistics */
    stats->total_requests = num_requests;
    stats->successful = successful;
    stats->failed = failed;
    stats->elapsed_seconds = (double)(t_end - t_start) / 1000000.0;
    stats->requests_per_second = (stats->elapsed_seconds > 0)
                                 ? (double)num_requests / stats->elapsed_seconds
                                 : 0;
    stats->avg_latency_us = (num_requests > 0) ? sum_us / (double)num_requests : 0;
    stats->min_latency_us = (num_requests > 0) ? min_us : 0;
    stats->max_latency_us = max_us;

    /* Sort latencies for percentile computation */
    qsort(latencies, (size_t)num_requests, sizeof(double), _cmp_double);

    if (num_requests > 0) {
        size_t idx50 = (size_t)((double)num_requests * 0.50);
        size_t idx95 = (size_t)((double)num_requests * 0.95);
        size_t idx99 = (size_t)((double)num_requests * 0.99);

        if (idx50 >= num_requests) idx50 = num_requests - 1;
        if (idx95 >= num_requests) idx95 = num_requests - 1;
        if (idx99 >= num_requests) idx99 = num_requests - 1;

        stats->p50_latency_us = latencies[idx50];
        stats->p95_latency_us = latencies[idx95];
        stats->p99_latency_us = latencies[idx99];
    }

    free(latencies);
    return 0;
}

/**
 * Pretty-print benchmark statistics.
 */
void benchmark_print(FILE *fp, const benchmark_stats_t *stats) {
    if (!fp || !stats) return;

    fprintf(fp, "\n===== Benchmark Results =====\n");
    fprintf(fp, "Total requests:   %lu\n", (unsigned long)stats->total_requests);
    fprintf(fp, "Successful (2xx): %lu\n", (unsigned long)stats->successful);
    fprintf(fp, "Failed:           %lu\n", (unsigned long)stats->failed);
    fprintf(fp, "Elapsed time:     %.3f s\n", stats->elapsed_seconds);
    fprintf(fp, "Throughput:       %.1f req/s\n", stats->requests_per_second);
    fprintf(fp, "\nLatency (microseconds):\n");
    fprintf(fp, "  avg:  %.1f\n", stats->avg_latency_us);
    fprintf(fp, "  min:  %.1f\n", stats->min_latency_us);
    fprintf(fp, "  max:  %.1f\n", stats->max_latency_us);
    fprintf(fp, "  p50:  %.1f\n", stats->p50_latency_us);
    fprintf(fp, "  p95:  %.1f\n", stats->p95_latency_us);
    fprintf(fp, "  p99:  %.1f\n", stats->p99_latency_us);
    fprintf(fp, "=============================\n\n");
}
