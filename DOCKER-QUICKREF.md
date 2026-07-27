# Docker Quick Reference

## Essential Commands for Contributors

### First Time Setup
```bash
# 1. Install Docker Desktop
# Download from: https://docs.docker.com/get-docker/

# 2. Clone repository
git clone https://github.com/kamrankhan78694/modern-c-web-library.git
cd modern-c-web-library

# 3. Make scripts executable
chmod +x docker-run.sh docker-verify.sh

# 4. Verify Docker setup
./docker-verify.sh
```

### Daily Development Workflow

```bash
# Start development container (opens shell)
./docker-run.sh dev

# Inside container (your checkout is mounted at /workspace, which hides the
# build/ directory baked into the image — so create it yourself):
mkdir -p build && cd build
cmake ..
make                    # Build
make test               # Run tests (6 ctest suites)
./examples/async_server # Run server
valgrind --leak-check=full ./tests/test_weblib  # Check memory
```

If you have already built on your **host**, the mounted `build/` holds a host CMake cache that is invalid inside the container and `cmake ..` aborts with a `CMakeCache.txt` directory-mismatch error — run `rm -rf build` on the host first.

### Quick Commands

| Command | Purpose |
|---------|---------|
| `./docker-run.sh test` | Run the default test suite (6 ctest suites; no TLS) |
| `./docker-run.sh dev` | Start dev shell |
| `./docker-run.sh async` | Run async server |
| `./docker-run.sh threaded 3000` | Run threaded server on port 3000 |
| `./docker-run.sh build` | Build images only |

### Docker Compose Commands

```bash
# Run async server (default)
docker-compose up weblib-async

# Run threaded server
docker-compose --profile threaded up weblib-threaded

# Development mode
docker-compose --profile dev up weblib-dev

# Stop all services
docker-compose down
```

### Manual Docker Commands

```bash
# Build development image
docker build -f Dockerfile.dev -t modern-c-weblib:dev .

# Build production image
docker build -t modern-c-weblib:latest .

# Run dev container with volume mount
docker run -it -v $(pwd):/workspace -p 8080:8080 modern-c-weblib:dev

# Run production server
docker run -p 8080:8080 modern-c-weblib:latest

# Run tests only (no mount, so `cd build` is the image's own build directory —
# rebuild the image to pick up local edits)
docker run --rm modern-c-weblib:dev /bin/bash -c "cd build && make test"
```

### Troubleshooting

```bash
# Docker not running
# → Start Docker Desktop

# Port already in use
./docker-run.sh async 8081  # Use different port

# Rebuild without cache
docker build --no-cache -f Dockerfile.dev -t modern-c-weblib:dev .

# View logs
docker logs <container-name>

# Clean up
docker system prune          # Remove unused containers/images
docker container prune       # Remove stopped containers
docker image prune          # Remove unused images
```

### Testing Checklist

Before submitting a PR:
- [ ] `./docker-run.sh test` - The 6 default ctest suites pass
- [ ] `./docker-run.sh dev` then `mkdir -p build && cd build && cmake .. && make 2>&1 | grep -i warning` - No warnings
- [ ] `valgrind --leak-check=full ./tests/test_weblib` - No memory leaks
- [ ] If you touched `src/tls/`: build with TLS on and run all 13 suites (below)
- [ ] Code follows style guide (see CONTRIBUTING.md)

`./docker-run.sh test` configures with a bare `cmake ..`, so `WEBLIB_ENABLE_TLS` stays OFF and none of `src/tls/` is compiled or tested. To cover it, inside `./docker-run.sh dev`:

```bash
mkdir -p build-tls && cd build-tls
cmake .. -DCMAKE_BUILD_TYPE=RelWithDebInfo -DWEBLIB_ENABLE_TLS=ON -DWEBLIB_TLS_TEST_HOOKS=ON
make -j"$(nproc)"
ctest -N                            # must list 13 tests, not 6
ctest --output-on-failure --no-tests=error
```

The TLS layer is **EXPERIMENTAL and UNAUDITED** — see `src/tls/README.md`. `WEBLIB_TLS_TEST_HOOKS` exposes a deterministic-RNG seam for `TlsHttpTests` and must never be on in a production build.

### Development Tips

1. **Edit Locally, Build in Docker**
   - Use your favorite editor on your machine
   - Files are automatically synced via volume mount
   - Build and test in consistent Docker environment

2. **Debugging**
   ```bash
   ./docker-run.sh dev
   mkdir -p build && cd build && cmake .. && make
   gdb ./examples/async_server
   ```

3. **Memory Leak Detection**
   ```bash
   ./docker-run.sh dev
   mkdir -p build && cd build && cmake .. && make
   valgrind --leak-check=full --show-leak-kinds=all ./tests/test_weblib
   ```

4. **Check Build Warnings**
   ```bash
   docker run --rm -v "$(pwd):/workspace" modern-c-weblib:dev /bin/bash -c \
     'mkdir -p /workspace/build && cd /workspace/build && cmake .. && make 2>&1 | grep -i warning'
   ```
   The mount is what makes this see your edits; without it you would be rebuilding the source snapshot baked into the image.

### File Structure

```
modern-c-web-library/
├── Dockerfile              # Production image (multi-stage)
├── Dockerfile.release      # Published ghcr.io image (non-root, healthcheck)
├── Dockerfile.dev          # Development image (full toolchain)
├── docker-compose.yml      # Service orchestration
├── docker-run.sh           # Helper script
├── docker-verify.sh        # Verification script
├── .dockerignore          # Files to exclude from build
└── DOCKER.md              # Complete Docker guide
```

`Dockerfile` is what `docker-compose.yml` and `docker-run.sh` build. `Dockerfile.release` is what CI publishes to `ghcr.io` (`.github/workflows/docker-publish.yml`); unlike `Dockerfile` it also ships `websocket_echo_server`, runs as a non-root `weblib` user, and defines a `HEALTHCHECK`.

### Environment Details

**Development Container:**
- Base: GCC 11
- Tools: cmake, make, gdb, valgrind, git, vim
- Size: ~1.2GB
- Default command: bash shell

**Production Container:**
- Base: Debian Bullseye Slim
- Size: ~150MB
- Contains: Compiled executables only
- Default command: Run async_server

### Support

- **Full Documentation**: See [DOCKER.md](DOCKER.md)
- **Contributing Guide**: See [CONTRIBUTING.md](CONTRIBUTING.md)
- **Issues**: https://github.com/kamrankhan78694/modern-c-web-library/issues

---

**Remember**: Docker is for development/CI only. The library itself remains pure C with zero dependencies!
