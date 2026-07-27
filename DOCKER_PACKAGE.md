# Modern C Web Library - Docker Package

[![Docker](https://img.shields.io/badge/docker-ghcr.io-blue)](https://github.com/kamrankhan78694/modern-c-web-library/pkgs/container/modern-c-web-library)
[![Version](https://img.shields.io/badge/version-2.0.0-green)](https://github.com/kamrankhan78694/modern-c-web-library/releases/tag/v2.0.0)
[![License](https://img.shields.io/badge/license-MIT-blue)](LICENSE)

Docker image for the Modern C Web Library. It ships the project's three bundled example servers — a WebSocket echo server and two HTTP servers — built from `Dockerfile.release` with `-DCMAKE_BUILD_TYPE=Release`, running as a non-root user behind a health check. They are working demonstrations of the library rather than a finished application: use them to try it out, or as the starting point for your own binary.

## Quick Start

### Pull and Run

```bash
# Pull the latest image
docker pull ghcr.io/kamrankhan78694/modern-c-web-library:latest

# Run WebSocket Echo Server (default)
docker run -p 8080:8080 ghcr.io/kamrankhan78694/modern-c-web-library:latest

# Run Async HTTP Server
docker run -p 8080:8080 -e SERVER_TYPE=async ghcr.io/kamrankhan78694/modern-c-web-library:latest

# Run Simple HTTP Server
docker run -p 8080:8080 -e SERVER_TYPE=simple ghcr.io/kamrankhan78694/modern-c-web-library:latest

# Custom port
docker run -p 3000:3000 -e PORT=3000 ghcr.io/kamrankhan78694/modern-c-web-library:latest
```

### Test WebSocket Server

```bash
# Start the container
docker run -d -p 8080:8080 --name weblib ghcr.io/kamrankhan78694/modern-c-web-library:latest

# Open http://localhost:8080 in your browser
# The WebSocket echo server includes a built-in HTML test client
```

## Available Images

- `latest` - Latest stable release (currently 2.0.0)
- `2.0.0` - Exact release version
- `2.0` - Major.minor version
- `2` - Major version only

The tags CI publishes carry **no `v` prefix**: `.github/workflows/docker-publish.yml` uses `docker/metadata-action` with `type=semver,pattern={{version}}`, which strips the `v` from the git tag. Pull `...:2.0.0`, not `...:v2.0.0` — the latter is not a tag CI creates, and will normally come back as `manifest unknown`. (The older `publish-package.sh` helper does push a `v`-prefixed tag, so a stale `v`-prefixed tag may still linger in the registry; see [PUBLISH_GUIDE.md](PUBLISH_GUIDE.md).)

## Features

### What's in the Image (2.0.0)

✅ **WebSocket Support** (RFC 6455)
- Automatic ping/pong handling
- Text and binary messages
- Multiple concurrent connections
- Graceful connection close

✅ **HTTP Server**
- Multi-threaded mode
- Async I/O mode (epoll/kqueue/poll)
- Flexible routing with parameters
- Middleware support

✅ **Zero Dependencies**
- Pure ISO C implementation
- No external libraries
- Cross-platform support

## Environment Variables

| Variable | Default | Description |
|----------|---------|-------------|
| `PORT` | `8080` | Server listening port |
| `SERVER_TYPE` | `websocket` | Server type: `websocket`, `async`, or `simple` |

## Server Types

### WebSocket Echo Server (Default)
`examples/websocket_echo_server.c` — echoes back every message it receives and serves a small HTML test client at `/`. Ping/pong is handled for you.
```bash
docker run -p 8080:8080 ghcr.io/kamrankhan78694/modern-c-web-library:latest
```

### Async HTTP Server
`examples/async_server.c` — enables async mode (`http_server_set_async`) and serves from an event loop over the platform's best backend: epoll on Linux, kqueue on BSD/macOS, `poll` elsewhere.
```bash
docker run -p 8080:8080 -e SERVER_TYPE=async ghcr.io/kamrankhan78694/modern-c-web-library:latest
```

### Simple HTTP Server
`examples/simple_server.c` — the thread-pool HTTP server.
```bash
docker run -p 8080:8080 -e SERVER_TYPE=simple ghcr.io/kamrankhan78694/modern-c-web-library:latest
```

## Docker Compose

```yaml
version: '3.8'

services:
  weblib:
    image: ghcr.io/kamrankhan78694/modern-c-web-library:latest
    ports:
      - "8080:8080"
    environment:
      - PORT=8080
      - SERVER_TYPE=websocket
    restart: unless-stopped
    healthcheck:
      test: ["CMD", "pidof", "websocket_echo_server"]
      interval: 30s
      timeout: 3s
      retries: 3
```

## Building from Source

```bash
# Clone the repository
git clone https://github.com/kamrankhan78694/modern-c-web-library.git
cd modern-c-web-library

# Build using the release Dockerfile
docker build -f Dockerfile.release -t my-weblib:latest .

# Run
docker run -p 8080:8080 my-weblib:latest
```

## Security

- Runs as non-root user (`weblib`)
- Minimal runtime dependencies
- Multi-stage build for smaller attack surface
- Health check included

> **This image serves plain HTTP, not HTTPS.** `Dockerfile.release` builds with the default CMake options, so the repository's experimental pure-C TLS 1.3 layer (`WEBLIB_ENABLE_TLS`, default OFF) is not compiled into it. Terminate TLS at a proven proxy — nginx, Caddy, HAProxy, or your cloud load balancer — in front of this container. That layer is EXPERIMENTAL and UNAUDITED and is not for production use either way; see [docs/DEPLOYMENT.md](https://github.com/kamrankhan78694/modern-c-web-library/blob/main/docs/DEPLOYMENT.md).

## Runtime Characteristics

- **Concurrent connections**: capped at 128 by default (`MAX_CONNECTIONS` in `src/http_server.c`), in both threaded and async mode. Raise it in your own binary with `http_server_set_max_connections()`; the bundled examples leave it at the default.
- **Memory footprint**: no runtime, no garbage collector — the process is the binary plus libc.
- **Container size**: multi-stage build, so the runtime layer carries the binaries and `ca-certificates`, not the toolchain.

The repository has no published throughput or latency benchmarks. If numbers matter for your use, measure them on your own hardware.

## Architecture

```
┌─────────────────────────────────┐
│   Client (Browser/App)          │
└────────────┬────────────────────┘
             │ HTTP/WebSocket
             ▼
┌─────────────────────────────────┐
│   Docker Container              │
│  ┌───────────────────────────┐  │
│  │  WebSocket Echo Server    │  │
│  │  - RFC 6455 Compliant     │  │
│  │  - Ping/Pong Auto-handle  │  │
│  │  - Multi-connection       │  │
│  └───────────────────────────┘  │
│                                  │
│  OR                              │
│                                  │
│  ┌───────────────────────────┐  │
│  │  Async HTTP Server        │  │
│  │  - Event Loop             │  │
│  │  - Non-blocking I/O       │  │
│  └───────────────────────────┘  │
└─────────────────────────────────┘
```

## Use Cases

- 🚀 Real-time chat applications
- 🎮 Live gaming servers
- 📊 Live data dashboards
- 🔔 Push notification systems
- 📡 IoT device communication
- 🌐 High-performance web backends

## Version History

- **v2.0.0** (2026-07-27) - Current release
- **v1.0.0** (2026-02-22) - First stable release
- **v0.9.0** (2026-02-20) - Performance and observability (cache, metrics, gzip, async WebSocket)
- **v0.3.0** (2025-11-14) - WebSocket frame processing (threaded mode)
- **v0.2.0** - WebSocket protocol implementation
- **v0.1.0** - Initial HTTP server with async I/O

See [CHANGELOG.md](https://github.com/kamrankhan78694/modern-c-web-library/blob/main/CHANGELOG.md) for the full history.

## Links

- 📦 [GitHub Package](https://github.com/kamrankhan78694/modern-c-web-library/pkgs/container/modern-c-web-library)
- 📖 [Documentation](https://github.com/kamrankhan78694/modern-c-web-library)
- 🐛 [Issues](https://github.com/kamrankhan78694/modern-c-web-library/issues)
- 🔖 [Releases](https://github.com/kamrankhan78694/modern-c-web-library/releases)

## License

MIT License - See [LICENSE](https://github.com/kamrankhan78694/modern-c-web-library/blob/main/LICENSE) for details.

---

**Built with zero external dependencies - Pure ISO C implementation** 🔥
