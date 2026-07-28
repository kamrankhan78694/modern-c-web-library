# Docker Guide for Modern C Web Library

This guide helps contributors build, test, and develop the Modern C Web Library using Docker for a consistent, reproducible environment.

## Quick Start for Contributors

### Prerequisites
- Docker installed ([Get Docker](https://docs.docker.com/get-docker/))
- Git (to clone the repository)

### Build and Test in One Command

```bash
# Clone the repository
git clone https://github.com/kamrankhan78694/modern-c-web-library.git
cd modern-c-web-library

# Build and run tests
./docker-run.sh test
```

That's it! Docker will handle all dependencies, build the library, and run the default test suite (7 ctest suites). The experimental TLS 1.3 layer is **off** by default and is not built or tested by this command — see [Testing the Experimental TLS Layer](#testing-the-experimental-tls-layer).

## Docker Images

### Production Image (`Dockerfile`)
- **Purpose**: Lightweight runtime image for deployment
- **Size**: ~150MB
- **Contains**: `simple_server` and `async_server` only (no build tools)
- **Use case**: Local production-style runs; also what `docker-compose.yml` and `docker-run.sh` build

### Release Image (`Dockerfile.release`)
- **Purpose**: The image published to `ghcr.io` (see `.github/workflows/docker-publish.yml`)
- **Contains**: `simple_server`, `async_server` and `websocket_echo_server`, built with `-DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=OFF`
- **Extras**: Runs as the non-root `weblib` user, ships a `HEALTHCHECK`, and picks a server via the `SERVER_TYPE` / `PORT` environment variables
- **Use case**: Deployment. See [DOCKER_PACKAGE.md](DOCKER_PACKAGE.md)

### Development Image (`Dockerfile.dev`)
- **Purpose**: Full development environment
- **Size**: ~1.2GB  
- **Contains**: GCC, CMake, Make, GDB, Valgrind, Git
- **Use case**: Building, testing, debugging, contributing

## Common Development Workflows

### 1. Run Tests

```bash
./docker-run.sh test
```

This will:
1. Build the development Docker image
2. Compile the library inside the container
3. Run the default test suite (7 ctest suites)
4. Display test results

> **This is the default build only.** `Dockerfile.dev` configures with a bare `cmake ..`, so `WEBLIB_ENABLE_TLS` stays OFF (its default in `CMakeLists.txt`) and none of `src/tls/` is compiled. The 7 TLS suites are all inside `if(WEBLIB_ENABLE_TLS)` in `tests/CMakeLists.txt`, so a change under `src/tls/` is **not** covered by `./docker-run.sh test`. See [Testing the Experimental TLS Layer](#testing-the-experimental-tls-layer).

### 2. Interactive Development

Start a development container with your code mounted:

```bash
./docker-run.sh dev
```

Inside the container:
```bash
# Create/enter the build directory.
# The mount replaces /workspace with your checkout, so the build/ directory
# baked into the image is not visible here — create it yourself.
mkdir -p /workspace/build && cd /workspace/build

# Build the project
cmake ..
make

# Run tests
make test
./tests/test_weblib

# Run example server
./examples/simple_server
# or
./examples/async_server

# Debug with GDB
gdb ./examples/async_server

# Check for memory leaks
valgrind --leak-check=full ./tests/test_weblib
```

Your local code changes are immediately available in the container via volume mount.

> If you have already built on your **host** machine, the mounted `build/` contains a host CMake cache that is invalid inside the container and `cmake ..` will abort with a `CMakeCache.txt` directory-mismatch error. Run `rm -rf build` on the host first, or use a separate directory name inside the container (for example `build-docker`).

### 3. Quick Build Verification

```bash
# Build images only (no run)
./docker-run.sh build
```

### 4. Run the Server

```bash
# Run async server on port 8080
./docker-run.sh async

# Run threaded server on custom port
./docker-run.sh threaded 3000
```

## Using Docker Compose

### Run Async Server (Default)
```bash
docker-compose up weblib-async
```
Access at: http://localhost:8080

### Run Threaded Server
```bash
docker-compose --profile threaded up weblib-threaded
```
Access at: http://localhost:8081

### Development Mode
```bash
docker-compose --profile dev up weblib-dev
```
Starts the dev container with your checkout mounted at `/workspace` and the container's port 8080 published on host port **8082**.

As with `./docker-run.sh dev`, the mount hides the `build/` directory that was created when the image was built, so run `mkdir -p /workspace/build && cd /workspace/build && cmake .. && make` inside the container rather than `cd build`.

## Manual Docker Commands

### Build Development Image
```bash
docker build -f Dockerfile.dev -t modern-c-weblib:dev .
```

### Run Development Container
```bash
docker run -it -v $(pwd):/workspace -p 8080:8080 modern-c-weblib:dev
```

### Build Production Image
```bash
docker build -t modern-c-weblib:latest .
```

### Run Production Container
```bash
docker run -p 8080:8080 modern-c-weblib:latest
```

## Testing Workflow

### Run the Default Test Suite
```bash
./docker-run.sh test
```

This runs `ctest` (7 suites: `WebLibTests`, `KamranHeaderTests`, `AsyncWebSocketTests`, `StressTests`, `WorkerTests`, `WasmTests`, `StressDemoApp`) and then `./tests/test_weblib` directly. The tail of the `test_weblib` output looks like:

```
Testing router_create... PASSED
Testing router_add_route... PASSED
Testing json_object_create... PASSED
Testing json_string_create... PASSED
...
===================================
Tests run: N
Tests passed: N
Tests failed: 0
```

`docker-verify.sh` greps for exactly that `Tests failed: 0` line, so it is the string to look for.

### Testing the Experimental TLS Layer

The pure-C TLS 1.3 layer is **EXPERIMENTAL and UNAUDITED** — it has had no external cryptographic audit, and it is not for production use. See [`src/tls/README.md`](src/tls/README.md).

It is also off by default, which means `./docker-run.sh test` never compiles or exercises it. To build it with TLS and its test hooks and run all 14 suites:

```bash
docker build -f Dockerfile.dev -t modern-c-weblib:dev .
docker run --rm -v "$(pwd):/workspace" modern-c-weblib:dev /bin/bash -c \
  'mkdir -p /workspace/build-tls && cd /workspace/build-tls && \
   cmake .. -DCMAKE_BUILD_TYPE=RelWithDebInfo -DWEBLIB_ENABLE_TLS=ON -DWEBLIB_TLS_TEST_HOOKS=ON && \
   make -j"$(nproc)" && ctest --output-on-failure --no-tests=error'
```

Notes:
- The single quotes around the `bash -c` payload matter: they stop your host shell from expanding `$(nproc)` (macOS has no `nproc`).
- `ctest -N` should list **13** tests, not 6. The extra 7 are `TlsTests`, `TlsCryptoTests`, `TlsParseTests`, `TlsTransportTests`, `TlsFuzzTests`, `TlsHttpTests` and `TlsInteropOpenssl`.
- `TlsInteropOpenssl` self-skips (exit 0) when the container has no TLS 1.3-capable `openssl s_client`, so read the ctest output instead of assuming it ran.
- `WEBLIB_TLS_TEST_HOOKS` exposes a deterministic-RNG seam used only by `TlsHttpTests`. It must never be enabled in a production build.

### Run Tests Manually

The next two blocks start the container **without** a volume mount, so `cd build` works as written — that is the `build/` directory `Dockerfile.dev` produced when the image was built. The trade-off is that they test the source snapshot baked into the image, so re-run `docker build -f Dockerfile.dev -t modern-c-weblib:dev .` after editing, or use the mounted flow in [Interactive Development](#2-interactive-development) instead.

```bash
# Start dev container
docker run -it modern-c-weblib:dev

# Inside container
cd build
make test
./tests/test_weblib
```

### Run Tests with Valgrind
```bash
# Start dev container
docker run -it modern-c-weblib:dev

# Inside container
cd build
valgrind --leak-check=full --show-leak-kinds=all ./tests/test_weblib
```

## Contributing with Docker

### Step 1: Fork and Clone
```bash
git clone https://github.com/YOUR_USERNAME/modern-c-web-library.git
cd modern-c-web-library
```

### Step 2: Start Development Container
```bash
./docker-run.sh dev
```

### Step 3: Make Changes
Edit files on your local machine using your preferred editor. Changes are reflected immediately in the container via volume mount.

### Step 4: Build and Test
Inside the container (the mount hides the image's `build/`, so create it):
```bash
mkdir -p /workspace/build && cd /workspace/build
cmake ..
make
make test
```

If you changed anything under `src/tls/`, also run the TLS build — see [Testing the Experimental TLS Layer](#testing-the-experimental-tls-layer). `make test` here runs 7 suites; the TLS build runs 14.

### Step 5: Verify No Warnings
```bash
cd /workspace/build
cmake .. && make 2>&1 | grep -i warning
# Should show no output
```

### Step 6: Check Memory Leaks
```bash
cd /workspace/build
valgrind --leak-check=full --show-leak-kinds=definite,indirect \
         --errors-for-leak-kinds=definite,indirect --error-exitcode=1 \
         ./tests/test_weblib
# Must exit 0: zero definite and zero indirect leaks. This is the exact command CI runs,
# over every tests/test_* binary, so check the others too before pushing.
#
# Do NOT expect Valgrind's "no leaks are possible" line. That appears only when zero bytes
# remain at exit, including *still-reachable* — and this binary starts a thread pool, whose
# cached thread stacks glibc never returns. Still-reachable and possible blocks are counted
# in the LEAK SUMMARY but are not gated.
```

### Step 7: Commit and Push
```bash
git add .
git commit -m "Your changes"
git push origin your-branch-name
```

## Troubleshooting

### Port Already in Use
```bash
# Use a different port
./docker-run.sh async 8081
```

### Build Cache Issues
```bash
# Rebuild without cache
docker build --no-cache -f Dockerfile.dev -t modern-c-weblib:dev .
```

### Container Permission Issues
If you get permission errors with volume mounts:
```bash
# Run with your user ID
docker run -it -v $(pwd):/workspace -u $(id -u):$(id -g) modern-c-weblib:dev
```

### View Container Logs
```bash
# If using docker-compose
docker-compose logs weblib-async

# If using docker run with -d flag
docker logs <container-id>
```

### Clean Up Containers
```bash
# Stop all containers
docker stop $(docker ps -aq)

# Remove all stopped containers
docker container prune

# Remove unused images
docker image prune
```

## Environment Details

### Installed Tools (Development Image)
- **Compiler**: GCC 11 (C11 standard)
- **Build System**: CMake 3.18+
- **Build Tool**: GNU Make
- **Debugger**: GDB
- **Memory Checker**: Valgrind
- **Editor**: Vim (basic)
- **Version Control**: Git

### Platform Information
Docker containers run Linux (Debian Bullseye), which means:
- Event loop uses **epoll** backend (Linux high-performance I/O)
- Threading uses **pthread** (POSIX threads)
- All tests run in a consistent Linux environment

### Build Configuration
Default CMake configuration:
- C Standard: C11
- Compiler Flags: `-Wall -Wextra -pedantic`
- Build Type: Default (no optimization flags for debugging)

## CI/CD Integration

### GitHub Actions Example
```yaml
name: Docker Build and Test

on: [push, pull_request]

jobs:
  test:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v3
      
      - name: Build and Test
        run: |
          chmod +x docker-run.sh
          ./docker-run.sh test
```

### GitLab CI Example
```yaml
docker-test:
  image: docker:latest
  services:
    - docker:dind
  script:
    - chmod +x docker-run.sh
    - ./docker-run.sh test
```

## Docker Helper Script Reference

The `docker-run.sh` script provides convenient shortcuts:

```bash
./docker-run.sh [MODE] [PORT]
```

**Modes:**
- `async` - Run async server (default port: 8080)
- `threaded` - Run threaded server
- `dev` - Start development container with shell
- `build` - Build Docker images only
- `test` - Build and run the default test suite (7 ctest suites; TLS off)
- `help` - Show usage information

**Examples:**
```bash
./docker-run.sh                  # Run async server on 8080
./docker-run.sh async 3000       # Run async server on 3000
./docker-run.sh dev              # Start dev container
./docker-run.sh test             # Run tests
./docker-run.sh build            # Build images
```

## Best Practices for Contributors

1. **Always run tests before committing:**
   ```bash
   ./docker-run.sh test
   ```
   If your change touches `src/tls/`, this is not enough — run the TLS build as well ([Testing the Experimental TLS Layer](#testing-the-experimental-tls-layer)).

2. **Check for memory leaks in new code:**
   ```bash
   docker run --rm -it -v "$(pwd):/workspace" modern-c-weblib:dev
   # Inside the container:
   mkdir -p /workspace/build && cd /workspace/build && cmake .. && make
   valgrind --leak-check=full ./tests/test_weblib
   ```

3. **Verify no compiler warnings:**
   ```bash
   docker run --rm -v "$(pwd):/workspace" modern-c-weblib:dev /bin/bash -c \
     'mkdir -p /workspace/build && cd /workspace/build && cmake .. && make 2>&1 | grep -i warning'
   ```

   Both commands mount your checkout, so they see your edits without rebuilding the image. If `cmake ..` complains about `CMakeCache.txt`, clear the host build directory first (`rm -rf build`).

4. **Test on fresh environment:**
   Docker ensures a clean build environment every time, catching dependency issues.

5. **Use volume mounts for development:**
   Edit locally, build in container - best of both worlds.

## Why Docker for This Project?

1. **Zero Setup**: Contributors don't need to install GCC, CMake, or other tools
2. **Consistent Environment**: Same build environment for everyone
3. **Platform Independent**: the *container* runs anywhere Docker does, including Windows hosts — but the library inside it is built for Linux. A native Windows build is not supported (POSIX-only networking core)
4. **CI/CD Ready**: Same Docker setup works in automated pipelines
5. **Isolation**: Doesn't interfere with your system tools/libraries
6. **Pure C Philosophy**: Docker is only for development; the library remains dependency-free

## Resources

- **Docker Documentation**: https://docs.docker.com
- **Project Repository**: https://github.com/kamrankhan78694/modern-c-web-library
- **Contributing Guide**: See [CONTRIBUTING.md](CONTRIBUTING.md)
- **Issues**: https://github.com/kamrankhan78694/modern-c-web-library/issues

## Questions?

If you have questions about the Docker setup:
1. Check this guide's troubleshooting section
2. Review Docker documentation
3. Open an issue on GitHub with the `docker` label

---

Happy coding! 🐳
