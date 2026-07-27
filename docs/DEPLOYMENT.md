# Deployment Guide

**Modern C Web Library v2.0.0** — Production Deployment

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
# Headers → /usr/local/include/kamran.k, /usr/local/include/db_pool.h
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

/* Concurrent connection cap — the default is only 128, in both threaded
   and async mode. Raise it deliberately, alongside your file-descriptor
   limit (LimitNOFILE / ulimit -n). */
http_server_set_max_connections(server, 1024);

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
| Max connections | 128 | Raise to fit your load | Runtime: `http_server_set_max_connections()` (must be ≥ 1). 128 is the `MAX_CONNECTIONS` default in http_server.c |

## Deployment Patterns

### Standalone Binary

The simplest deployment — compile a static binary and run it:

```bash
gcc -O2 -static -o myapp myapp.c -lweblib -lpthread
scp myapp server:/opt/myapp/
ssh server '/opt/myapp/myapp 8080'
```

### Docker Container

The repository ships a production `Dockerfile.release` (multi-stage, `-DBUILD_TESTS=OFF`, non-root `weblib` user, `HEALTHCHECK`) that builds the bundled example servers. To containerise **your own** app against the library, a minimal image looks like this:

```dockerfile
# Stage 1 — builder. This is the only stage with a compiler.
FROM ubuntu:22.04 AS builder
RUN apt-get update && apt-get install -y build-essential cmake \
    && rm -rf /var/lib/apt/lists/*
COPY . /src
WORKDIR /src/build
RUN cmake .. -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=OFF -DBUILD_EXAMPLES=OFF \
    && make -j"$(nproc)"
# myapp.c arrived with `COPY . /src`. Build it here, where the toolchain lives.
RUN mkdir -p /out \
    && gcc -O2 -o /out/myapp /src/myapp.c -I/src/include -L/src/build -lweblib -lpthread

# Stage 2 — runtime. No compiler, no source, no root.
FROM ubuntu:22.04
RUN useradd -m -u 1000 weblib
COPY --from=builder --chown=weblib:weblib /out/myapp /app/myapp
WORKDIR /app
USER weblib
EXPOSE 8080
CMD ["./myapp", "8080"]
```

`weblib` is a **static** library, so the finished binary carries everything it needs — the runtime stage does not need `libweblib.a` or the headers, and it must not install a compiler.

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

### Built-in TLS — Experimental, Not for Production

Alongside the proxy-termination pattern above, v2.0.0 adds a hand-written, zero-dependency pure-C TLS 1.3 **server**, exposed as `http_server_enable_tls()` and compiled only when the CMake option `WEBLIB_ENABLE_TLS` is set. It defaults to **OFF**: with it off, no `src/tls/` code is compiled at all.

It is **EXPERIMENTAL and UNAUDITED**. It has had no external cryptographic audit, and it is not for production without one. For anything you actually serve, terminate TLS at nginx, Caddy, HAProxy or your cloud load balancer, as shown above.

If you want to evaluate it in a non-production environment, these are the boundaries:

- **Native only.** The option has no effect on WASM/Emscripten or Cloudflare Workers builds.
- **Server only.** There is no TLS client.
- **Threaded mode only.** `http_server_enable_tls()` returns `-1` if async mode is enabled, and it must be called before `http_server_listen()`.
- **No WebSocket over TLS.** A WebSocket upgrade on a TLS connection is refused with 503.
- **One profile.** Cipher suite `TLS_CHACHA20_POLY1305_SHA256`, X25519 key exchange, Ed25519 signatures — so an Ed25519 certificate and a PKCS#8 Ed25519 private key. No AES-GCM, no RSA, no ECDSA, no TLS 1.2.
- **No browser support.** A real `openssl s_client` handshake and HTTPS round-trip is verified in CI, but browser page-load is not achieved: browser support for Ed25519-only certificates is limited and inconsistent.

The API takes PEM **buffers with explicit lengths**, not file paths — read the files yourself (see `examples/tls_server.c`):

```c
int http_server_enable_tls(http_server_t *server,
                           const char *cert_pem, size_t cert_len,
                           const char *key_pem,  size_t key_len);
```

Build it with:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo -DWEBLIB_ENABLE_TLS=ON
cmake --build build --parallel
cd build && ctest --output-on-failure
```

`WEBLIB_TLS_TEST_HOOKS` (default OFF) gates a deterministic-RNG seam used only by the `TlsHttpTests` suite. It must never be enabled in a production build. See [`src/tls/README.md`](../src/tls/README.md).

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
- [ ] Do **not** enable the built-in TLS layer (`-DWEBLIB_ENABLE_TLS=ON` / `http_server_enable_tls()`) in production — it is EXPERIMENTAL and UNAUDITED, and not for production without an external cryptographic audit. See [Built-in TLS](#built-in-tls--experimental-not-for-production).
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
