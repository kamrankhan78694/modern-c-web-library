# Modern C Web Library - Docker Package

[![Docker](https://img.shields.io/badge/docker-ghcr.io-blue)](https://github.com/kamrankhan78694/modern-c-web-library/pkgs/container/modern-c-web-library)
[![Version](https://img.shields.io/badge/version-0.3.0-green)](https://github.com/kamrankhan78694/modern-c-web-library/releases/tag/v0.3.0)
[![License](https://img.shields.io/badge/license-MIT-blue)](LICENSE)

Production-ready Docker container for the Modern C Web Library featuring HTTP and WebSocket server capabilities.

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

- `latest` - Latest stable release (currently v0.3.0)
- `v0.3.0` - Tagged release version
- `0.3` - Major.minor version
- `0` - Major version only

## Features

### v0.3.0 Highlights

✅ **WebSocket Support** (RFC 6455 compliant)
- Automatic ping/pong handling (<0.001s latency)
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
Production-ready WebSocket server with automatic ping/pong handling:
```bash
docker run -p 8080:8080 ghcr.io/kamrankhan78694/modern-c-web-library:latest
```

### Async HTTP Server
High-performance async I/O server for thousands of connections:
```bash
docker run -p 8080:8080 -e SERVER_TYPE=async ghcr.io/kamrankhan78694/modern-c-web-library:latest
```

### Simple HTTP Server
Multi-threaded HTTP server:
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

## Performance

- **Ping/Pong Latency**: <0.001s
- **Concurrent Connections**: Thousands (async mode)
- **Memory Footprint**: Minimal (pure C, no garbage collection)
- **Container Size**: Optimized with multi-stage build

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
│  │  - High Concurrency       │  │
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

- **v0.3.0** (2025-11-14) - WebSocket frame processing (threaded mode)
- **v0.2.0** - WebSocket protocol implementation
- **v0.1.0** - Initial HTTP server with async I/O

## Links

- 📦 [GitHub Package](https://github.com/kamrankhan78694/modern-c-web-library/pkgs/container/modern-c-web-library)
- 📖 [Documentation](https://github.com/kamrankhan78694/modern-c-web-library)
- 🐛 [Issues](https://github.com/kamrankhan78694/modern-c-web-library/issues)
- 🔖 [Releases](https://github.com/kamrankhan78694/modern-c-web-library/releases)

## License

MIT License - See [LICENSE](https://github.com/kamrankhan78694/modern-c-web-library/blob/main/LICENSE) for details.

---

**Built with zero external dependencies - Pure ISO C implementation** 🔥
