# Deployment Guide

This document describes how to build, package, and deploy applications built with the Modern C Web Library.

## Prerequisites

| Tool | Minimum Version | Purpose |
|------|----------------|---------|
| C compiler | GCC 7+ / Clang 6+ / MSVC 2017+ | Build the library |
| CMake | 3.10+ | Build system |
| POSIX threads | (system) | Thread pool (Linux/macOS) |
| Docker *(optional)* | 20.10+ | Containerised builds and deployment |

No external libraries are required — the project is pure C with zero dependencies beyond the standard library and platform APIs.

## Building for Production

### Native Build

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release \
         -DBUILD_TESTS=OFF \
         -DBUILD_EXAMPLES=OFF
make -j$(nproc)
```

This produces:

| Artifact | Path | Description |
|----------|------|-------------|
| `libweblib.a` | `build/libweblib.a` | Static library |
| `libweblib_shared.so` | `build/libweblib_shared.so` | Shared library |

### Install System-Wide

```bash
sudo make install
# Headers → /usr/local/include/weblib.h, /usr/local/include/db_pool.h
# Libraries → /usr/local/lib/libweblib.a, /usr/local/lib/libweblib_shared.so
```

### Docker Build (Recommended for CI)

```bash
# Build and run tests in a clean container
./docker-run.sh test

# Build a minimal release image
docker build -f Dockerfile.release -t myapp:latest .
```

## Linking Your Application

### CMake

```cmake
find_library(WEBLIB weblib PATHS /usr/local/lib)
target_link_libraries(myapp ${WEBLIB} pthread)
```

### Manual Compilation

```bash
gcc -O2 -o myapp myapp.c -lweblib -lpthread
```

## Runtime Configuration

All configuration is done in code at server creation time. Key settings to tune for production:

```c
http_server_t *server = http_server_create();

/* Thread pool size — match to available CPU cores */
http_server_set_thread_count(server, 16);

/* Socket timeouts — prevent slow-loris attacks */
http_server_set_timeout(server, 30, 30);  /* read_sec, write_sec */

/* Register health check for load-balancer probes */
health_check_register(router);

/* Bind to port */
http_server_listen(server, 8080);
```

### Recommended Production Settings

| Setting | Default | Recommendation | Notes |
|---------|---------|---------------|-------|
| Thread count | 16 | CPU cores × 2 | Clamped to [1, 256] |
| Read timeout | 30 s | 30 s | Prevents slow-loris |
| Write timeout | 30 s | 30 s | Prevents stalled connections |
| Max body size | 1 MiB | (compile-time) | `MAX_BODY_BYTES` in http_server.c |
| Max headers | 100 | (compile-time) | `MAX_HEADER_COUNT` in http_server.c |
| Max connections | 128 | (compile-time) | `MAX_CONNECTIONS` in http_server.c |

## Deployment Patterns

### Standalone Binary

The simplest deployment — compile a static binary and run it:

```bash
gcc -O2 -static -o myapp myapp.c -lweblib -lpthread
scp myapp server:/opt/myapp/
ssh server '/opt/myapp/myapp 8080'
```

### Docker Container

Use the provided `Dockerfile.release` as a base:

```dockerfile
FROM ubuntu:22.04 AS builder
RUN apt-get update && apt-get install -y build-essential cmake
COPY . /src
WORKDIR /src/build
RUN cmake .. -DCMAKE_BUILD_TYPE=Release && make -j$(nproc)

FROM ubuntu:22.04
COPY --from=builder /src/build/libweblib.a /usr/local/lib/
COPY --from=builder /src/include/ /usr/local/include/
COPY myapp.c /app/
WORKDIR /app
RUN gcc -O2 -o myapp myapp.c -lweblib -lpthread
EXPOSE 8080
CMD ["./myapp", "8080"]
```

### Systemd Service

Create `/etc/systemd/system/myapp.service`:

```ini
[Unit]
Description=My Web Application
After=network.target

[Service]
Type=simple
ExecStart=/opt/myapp/myapp 8080
Restart=on-failure
RestartSec=5
User=www-data
Group=www-data
LimitNOFILE=65536

[Install]
WantedBy=multi-user.target
```

```bash
sudo systemctl daemon-reload
sudo systemctl enable --now myapp
```

### Behind a Reverse Proxy (nginx)

For production, run behind nginx for TLS termination:

```nginx
upstream myapp {
    server 127.0.0.1:8080;
}

server {
    listen 443 ssl;
    server_name example.com;

    ssl_certificate     /etc/ssl/certs/example.com.pem;
    ssl_certificate_key /etc/ssl/private/example.com.key;

    location / {
        proxy_pass http://myapp;
        proxy_set_header Host $host;
        proxy_set_header X-Real-IP $remote_addr;
        proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
        proxy_set_header X-Forwarded-Proto $scheme;
    }

    location /healthz {
        proxy_pass http://myapp/healthz;
        access_log off;
    }
}
```

## Health Checks

The library provides a built-in health check endpoint:

```c
health_check_register(router);  /* Adds GET /healthz */
```

Response format:
```json
{"status": "ok", "uptime_seconds": 12345}
```

Use this endpoint for:
- **Load balancer** health probes
- **Kubernetes** liveness/readiness probes
- **Monitoring** dashboards (Prometheus blackbox exporter, etc.)

### Kubernetes Example

```yaml
livenessProbe:
  httpGet:
    path: /healthz
    port: 8080
  initialDelaySeconds: 5
  periodSeconds: 10
readinessProbe:
  httpGet:
    path: /healthz
    port: 8080
  initialDelaySeconds: 3
  periodSeconds: 5
```

## Graceful Shutdown

The server supports graceful shutdown with drain timeout:

```c
#include <signal.h>

static http_server_t *g_server = NULL;

void sighandler(int sig) {
    (void)sig;
    if (g_server) {
        http_server_shutdown(g_server, 10);  /* 10 s drain */
    }
}

int main(void) {
    g_server = http_server_create();
    signal(SIGINT, sighandler);
    signal(SIGTERM, sighandler);
    /* ... setup and listen ... */
}
```

State transitions: `STOPPED → RUNNING → DRAINING → STOPPED`

## Monitoring and Observability

### Logging Middleware

```c
log_config_t log_cfg = { .level = LOG_LEVEL_INFO, .output = stderr };
middleware_fn_t logger = log_middleware_create(&log_cfg);
router_use_middleware(router, logger);
```

Output: `[2026-02-20 12:34:56] INFO  GET /api/users`

### Error Handler Middleware

```c
middleware_fn_t err = error_handler_middleware_create(NULL);
router_use_middleware(router, err);
```

Automatically fills `{"error":"Not Found","status":404}` for unhandled errors.

## Security Checklist

Before deploying to production, verify:

- [ ] Run behind a reverse proxy with TLS termination (nginx, Caddy, HAProxy)
- [ ] Set socket timeouts (`http_server_set_timeout`)
- [ ] Enable rate limiting (`ratelimit_middleware_create`)
- [ ] Enable CSRF protection for browser-facing endpoints (`csrf_middleware_create`)
- [ ] Validate all user input (`input_validate_*` functions)
- [ ] Sanitize HTML output (`input_sanitize_html`)
- [ ] Set appropriate CORS policy (`cors_middleware_create`)
- [ ] Configure auth middleware for protected routes
- [ ] Set `LimitNOFILE` / `ulimit -n` for production socket counts
- [ ] Review compile-time limits (`MAX_BODY_BYTES`, `MAX_HEADER_COUNT`, etc.)

## Verifying the Deployment

```bash
# Health check
curl -s http://localhost:8080/healthz | jq .
# Expected: {"status":"ok","uptime_seconds":N}

# Smoke test
curl -v http://localhost:8080/

# Load test (requires a load testing tool)
# wrk -t4 -c100 -d30s http://localhost:8080/healthz
```

---

## Planned v2.0 Deployment Enhancements

> The following deployment patterns are planned for v2.0 (Phases 11–20) and are not yet available.

### Native TLS (Phase 14 — v1.4.0)

Once Phase 14 ships, the server will support native TLS 1.3 without a reverse proxy:

```c
http_server_t *server = http_server_create();
http_server_enable_tls(server, "/path/to/cert.pem", "/path/to/key.pem");
http_server_listen(server, 8443);  /* HTTPS on port 8443 */
```

This eliminates the need for nginx/Caddy TLS termination in simple deployments.

### AI Inference Deployment (Phase 16 — v1.6.0)

Run AI inference endpoints with built-in SSE streaming and batch coalescing:

```bash
# Run the AI inference demo server
./docker-run.sh ai-demo

# Or build and run natively
./build/examples/ai_inference_server 8080
```

**Deployment pattern for llama.cpp integration:**
```
┌──────────────┐    ┌───────────────────┐    ┌──────────────┐
│ AI Client    │───▶│ Modern C Web Lib  │───▶│ llama.cpp    │
│ (HTTP/SSE)   │◀───│ (batch coalescing)│◀───│ (inference)  │
└──────────────┘    └───────────────────┘    └──────────────┘
```

### Agent Orchestration Deployment (Phase 18 — v1.8.0)

Deploy the agent orchestration server for multi-agent AI systems:

```bash
./build/examples/agent_orchestration_server 8080
```

Agents connect via WebSocket and communicate using JSON-RPC 2.0.

### Observability Stack (Phase 19 — v1.9.0)

Integrate with Prometheus and Grafana for production monitoring:

```yaml
# prometheus.yml
scrape_configs:
  - job_name: 'modern-c-web-lib'
    static_configs:
      - targets: ['localhost:8080']
    metrics_path: '/metrics'
```

### Performance Tuning for v2.0

| Setting | v1.0 Default | v2.0 Recommendation | Notes |
|---------|-------------|---------------------|-------|
| Thread count | 16 | CPU cores (auto-detect) | Work-stealing scheduler (Phase 17) |
| Memory allocator | malloc/free | Arena per connection | Phase 11 arena allocator |
| I/O backend | epoll/kqueue | io_uring (Linux 5.1+) | Phase 12, auto-fallback |
| TLS | Reverse proxy | Native TLS 1.3 | Phase 14, optional |
| HTTP version | HTTP/1.1 | HTTP/2 + HTTP/1.1 | Phase 15, ALPN negotiation |
