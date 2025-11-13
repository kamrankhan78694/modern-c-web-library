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

That's it! Docker will handle all dependencies, build the library, and run tests.

## Docker Images

### Production Image (`Dockerfile`)
- **Purpose**: Lightweight runtime image for deployment
- **Size**: ~150MB
- **Contains**: Only compiled executables (no build tools)
- **Use case**: Running the server in production

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
3. Run all tests
4. Display test results

### 2. Interactive Development

Start a development container with your code mounted:

```bash
./docker-run.sh dev
```

Inside the container:
```bash
# Navigate to build directory
cd build

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
Opens interactive shell with your code mounted.

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

### Run All Tests
```bash
./docker-run.sh test
```

Expected output:
```
Testing router_create... PASSED
Testing router_add_route... PASSED
Testing json_object_create... PASSED
Testing json_string_create... PASSED
...
All tests passed! (X/X)
```

### Run Tests Manually
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
Inside the container:
```bash
cd build
cmake ..
make
make test
```

### Step 5: Verify No Warnings
```bash
cd build
cmake .. && make 2>&1 | grep -i warning
# Should show no output
```

### Step 6: Check Memory Leaks
```bash
cd build
valgrind --leak-check=full ./tests/test_weblib
# Should show "no leaks are possible"
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
- `test` - Build and run all tests
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

2. **Check for memory leaks in new code:**
   ```bash
   docker run -it modern-c-weblib:dev
   cd build && valgrind --leak-check=full ./tests/test_weblib
   ```

3. **Verify no compiler warnings:**
   ```bash
   docker run modern-c-weblib:dev /bin/bash -c "cd build && cmake .. && make 2>&1 | grep -i warning"
   ```

4. **Test on fresh environment:**
   Docker ensures a clean build environment every time, catching dependency issues.

5. **Use volume mounts for development:**
   Edit locally, build in container - best of both worlds.

## Why Docker for This Project?

1. **Zero Setup**: Contributors don't need to install GCC, CMake, or other tools
2. **Consistent Environment**: Same build environment for everyone
3. **Platform Independent**: Works on Windows, macOS, Linux
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
