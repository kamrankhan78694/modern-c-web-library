# Modern C Web Library Documentation

**Version 1.0.0** — Production Ready  
[![DOI](https://zenodo.org/badge/DOI/10.5281/zenodo.18793559.svg)](https://doi.org/10.5281/zenodo.18793559)

Welcome to the documentation for Modern C Web Library, a production-ready pure C web framework with zero external dependencies.

## Quick Links

### Getting Started
- **[Getting Started Tutorial](tutorials/getting-started.md)** — Your first web server in 5 minutes
- **[Complete API Reference](api/README.md)** — Comprehensive API documentation
- **[Deployment Guide](DEPLOYMENT.md)** — Production deployment best practices

### Tutorials
- **[Building a REST API](tutorials/rest-api.md)** — Complete CRUD API with middleware
- **[Real-Time WebSocket Applications](tutorials/websocket.md)** — Chat servers and live dashboards
- **[WebSocket Protocol Details](WEBSOCKET.md)** — Deep dive into WebSocket implementation

### Advanced Topics
- **[Debugging Guide](DEBUGGING.md)** — Debug tools and techniques
- **[Technical Debt](TECHNICAL_DEBT.md)** — Known limitations and future work

## What's in v1.0.0?

This is the first production-ready release with comprehensive features:

### Core HTTP Server
- Multi-threaded and async I/O modes
- Routing with path parameters
- Middleware chain processing
- Request/response helpers
- JSON parser and serializer
- Cross-platform (Linux, macOS, Windows)

### WebSocket Support
- RFC 6455 compliant implementation
- Synchronous and asynchronous modes
- Event loop integration
- Text and binary messages
- Ping/pong and fragmentation support

### Middleware Stack
- **Authentication** — JWT, Basic Auth, API Key
- **CORS** — Configurable cross-origin resource sharing
- **CSRF** — Double-submit cookie protection
- **Rate Limiting** — Token bucket algorithm
- **Static Files** — MIME types, ETag, path traversal prevention
- **Logging** — Configurable log levels
- **Error Handler** — Centralized JSON error responses
- **Metrics** — Request counting and statistics

### Advanced Features
- **Body Parser** — JSON, URL-encoded, multipart with file uploads
- **Compression** — Pure C gzip/DEFLATE implementation
- **Session Management** — Cookie-based with expiration
- **Template Engine** — `{{ variable }}` syntax rendering
- **Caching** — In-memory LRU cache with TTL
- **Thread Pool** — Bounded worker management
- **Database Pool** — Thread-safe connection pooling
- **Input Validation** — Length, charset, email, HTML sanitization
- **Health Check** — `/healthz` endpoint
- **Benchmarking** — Throughput and latency measurement

## Architecture Overview

The library follows a modular design with clean separation of concerns:

```
┌─────────────────────────────────────────────────┐
│              Application Layer                   │
│  (Your Routes, Handlers, Business Logic)        │
└─────────────────┬───────────────────────────────┘
                  │
┌─────────────────▼───────────────────────────────┐
│           Middleware Stack                       │
│  CORS │ Auth │ CSRF │ Rate Limit │ Logging     │
└─────────────────┬───────────────────────────────┘
                  │
┌─────────────────▼───────────────────────────────┐
│              Router                              │
│  Path Matching │ Parameter Extraction           │
└─────────────────┬───────────────────────────────┘
                  │
┌─────────────────▼───────────────────────────────┐
│          HTTP Server Core                        │
│  Request Parser │ Response Builder              │
└─────────────────┬───────────────────────────────┘
                  │
┌─────────────────▼───────────────────────────────┐
│          Event Loop / Threading                  │
│  epoll │ kqueue │ poll │ pthread                │
└─────────────────┬───────────────────────────────┘
                  │
┌─────────────────▼───────────────────────────────┐
│          Platform APIs                           │
│  Sockets │ Files │ Time │ Memory                │
└──────────────────────────────────────────────────┘
```

## Zero Dependencies Philosophy

This project is built entirely in pure C (C99/C11) with zero external dependencies:

- ❌ No OpenSSL
- ❌ No libcurl
- ❌ No cJSON
- ❌ No third-party libraries

Everything is implemented from scratch:
- ✅ HTTP parser
- ✅ JSON parser
- ✅ WebSocket protocol
- ✅ Compression (gzip/DEFLATE)
- ✅ JWT implementation (HMAC-SHA256)
- ✅ All middleware and features

**Why?** Maximum portability, transparency, and educational value.

## Platform Support

| Platform | Threading | Async I/O | Status |
|----------|-----------|-----------|---------|
| **Linux** | pthread | epoll | ✅ Full support |
| **macOS** | pthread | kqueue | ✅ Full support |
| **FreeBSD** | pthread | kqueue | ✅ Full support |
| **Windows** | Win32 threads | IOCP (planned) | ⚠️ Basic support |

### Future Plans

- Add Windows support with a proper IOCP backend and Win32 threading.

## Performance

Designed for high performance:
- **Async I/O**: Handles thousands of concurrent connections
- **Event Loop**: Platform-specific backends (epoll/kqueue)
- **Thread Pool**: Bounded worker management
- **Zero Copy**: Minimal memory allocations
- **LRU Cache**: Fast in-memory caching
- **Compression**: Optional gzip for bandwidth savings

Benchmark results available in `STRESS_TESTS.md`.

## Code Quality

- **129 unit tests** — Comprehensive test coverage
- **Valgrind clean** — Zero memory leaks
- **Static analysis** — Clean builds on GCC/Clang with `-Wall -Wextra -pedantic`
- **CI/CD** — Automated testing on Linux GCC/Clang and macOS
- **Code review** — All changes reviewed

## Examples

The `examples/` directory contains working examples:

- **simple_server.c** — Basic HTTP server (threaded mode)
- **async_server.c** — Async I/O server (event loop)
- **websocket_echo_server.c** — WebSocket echo server
- **async_websocket_echo_server.c** — Async WebSocket server
- **rest_api_server.c** — Full REST API with CRUD operations

## Citation

If you use this library in your research or project, please cite:

```bibtex
@software{khan2026modern,
  author       = {Kamran Khan},
  title        = {Modern C Web Library: Production-Ready Pure C Web Framework},
  year         = {2026},
  publisher    = {Zenodo},
  version      = {1.0.0},
  doi          = {10.5281/zenodo.18793559},
  url          = {https://doi.org/10.5281/zenodo.18793559}
}
```

**DOI:** https://doi.org/10.5281/zenodo.18793559

## Contributing

Contributions are welcome! Please read [CONTRIBUTING.md](../CONTRIBUTING.md) for guidelines.

## License

MIT License. See [LICENSE](../LICENSE) for details.

## Support

- **GitHub Issues**: [Report bugs or request features](https://github.com/kamrankhan78694/modern-c-web-library/issues)
- **Discussions**: [Ask questions or discuss ideas](https://github.com/kamrankhan78694/modern-c-web-library/discussions)
- **Documentation**: This documentation is continuously updated

---

**Happy coding with Modern C Web Library!** 🚀
