# Debugging Guide

This guide explains how to debug the Modern C Web Library using VS Code, command-line tools, and Docker.

## Prerequisites

### macOS/Linux
- **CMake**: `brew install cmake` (macOS) or `apt-get install cmake` (Linux). The project itself builds with CMake 3.10+, but the `cmake -S . -B <dir>` form used throughout this guide needs 3.13+, and `ctest --no-tests=error` (in the TLS section) needs 3.20+. On an older CMake, use `mkdir build && cd build && cmake ..` instead, and drop `--no-tests=error`.
- **LLDB** (included with Xcode on macOS) or **GDB** on Linux
- **VS Code** with recommended extensions:
  - C/C++ (`ms-vscode.cpptools`)
  - CodeLLDB (`vadimcn.vscode-lldb`)
  - CMake Tools (`ms-vscode.cmake-tools`)

### Docker (Alternative)
If you don't want to install CMake locally, use the Docker environment:
```bash
./docker-run.sh dev
```

## Quick Start: VS Code Debugging

### 1. Build the Project
Press **Cmd+Shift+B** (macOS) or **Ctrl+Shift+B** (Linux/Windows) and select:
- `cmake-build-debug` - Builds with debug symbols

Or run manually:
```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --config Debug -j
```

This is a default build, which does **not** compile `src/tls/`. If you need to step
through the TLS layer, see [Debugging the experimental TLS layer](#debugging-the-experimental-tls-layer).

### 2. Debug Tests
1. Open the **Run and Debug** panel (Cmd+Shift+D / Ctrl+Shift+D)
2. Select **"Debug Tests (LLDB)"** from the dropdown
3. Press **F5** to start debugging
4. Set breakpoints in test code (`tests/test_weblib.c`) or library code (`src/*.c`)

The debugger will:
- Build the tests automatically (via `preLaunchTask`)
- Run `./build/tests/test_weblib` under LLDB
- Set `ASAN_OPTIONS=detect_leaks=1`, which takes effect **only if** you configured the build with `-DCMAKE_C_FLAGS="-fsanitize=address -fno-omit-frame-pointer"` (see [AddressSanitizer](#addresssanitizer-all-platforms) below). The default `cmake-build-debug` task does not enable ASan, so on its own this variable does nothing.

### 3. Debug the Server
1. Select **"Debug Simple Server (LLDB)"** from the Run and Debug dropdown
2. Press **F5**
3. The server will start on port 8080
4. Set breakpoints in `examples/simple_server.c` or routing code
5. Send requests with `curl` or your browser to trigger breakpoints:
   ```bash
   curl http://localhost:8080/
   curl http://localhost:8080/api/users/42
   ```

## Command-Line Debugging

### Using LLDB (macOS/Linux)
```bash
# Build with debug symbols
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j

# Debug tests -- break on a test case, or on the code under test
lldb ./build/tests/test_weblib
(lldb) breakpoint set --name test_router_add_route   # the test case
(lldb) breakpoint set --name router_route            # the function it exercises
(lldb) run

# Debug server
lldb ./build/examples/simple_server
(lldb) breakpoint set --name router_route
(lldb) run 8080
```

### Using GDB (Linux)
```bash
gdb ./build/tests/test_weblib
(gdb) break test_router_add_route
(gdb) run

# Or attach to running server
gdb -p $(pgrep simple_server)
```

## Running the Test Suite

`./build/tests/test_weblib` is one suite out of several, not "the tests". Running only
that binary silently skips the header-alias, async-WebSocket, stress/timeout, worker
and WASM suites. Use `ctest`, which is what CI does:

```bash
# Run every registered suite
cd build && ctest --output-on-failure --timeout 120

# Run one suite by name
ctest -R WebLibTests --output-on-failure

# The individual binaries live in build/tests/ and can still be run
# directly or under a debugger (see above)
```

A default build registers **7 suites**: `WebLibTests` (which by itself runs 172 unit
tests), `KamranHeaderTests`, `AsyncWebSocketTests`, `StressTests`, `WorkerTests`,
`StressDemoApp` and `WasmTests`.

Configuring with `-DWEBLIB_ENABLE_TLS=ON` adds 6 more — `TlsTests`, `TlsCryptoTests`,
`TlsParseTests`, `TlsTransportTests`, `TlsFuzzTests` and `TlsInteropOpenssl` — for
**13 total**. Adding `-DWEBLIB_TLS_TEST_HOOKS=ON` registers `TlsHttpTests` as well,
for **14**. (`TlsHttpTests` needs the
test hooks; `TlsInteropOpenssl` is registered only when `bash` and the `tls_server`
target are both available, and self-skips if `openssl` is missing or too old to speak
TLS 1.3.) The TLS layer is EXPERIMENTAL and UNAUDITED and is off by default — see
[`../src/tls/README.md`](../src/tls/README.md).

## Memory Debugging

### Valgrind (Linux)
```bash
# Install valgrind
sudo apt-get install valgrind

# Check for memory leaks
valgrind --leak-check=full --show-leak-kinds=all ./build/tests/test_weblib

# Check server memory
valgrind --leak-check=full ./build/examples/simple_server 8080
```

### AddressSanitizer (All Platforms)

ASan has to be configured into the build — a plain `-DCMAKE_BUILD_TYPE=Debug` build links
no sanitizer. Use a separate build directory so you keep an uninstrumented `build/` around:

```bash
# Configure with ASan
cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_C_FLAGS="-fsanitize=address -fno-omit-frame-pointer -g"
cmake --build build-asan -j

# Run every suite under ASan (overflows and use-after-free are reported automatically)
cd build-asan && ctest --output-on-failure --timeout 120
```

## Debugging the experimental TLS layer

`src/tls/` is **not compiled by default**. `WEBLIB_ENABLE_TLS` defaults to `OFF`
(`CMakeLists.txt`), so in a default build none of the TLS sources exist and no
breakpoint in them will ever bind — LLDB will just report a pending breakpoint with no
locations. TLS is native-only; Emscripten/WASM builds ignore the option. The layer is
EXPERIMENTAL and UNAUDITED — see [`../src/tls/README.md`](../src/tls/README.md).

```bash
# Debug build with TLS on
cmake -S . -B build-tls -DCMAKE_BUILD_TYPE=Debug \
  -DWEBLIB_ENABLE_TLS=ON -DWEBLIB_TLS_TEST_HOOKS=ON
cmake --build build-tls -j
cd build-tls && ctest --output-on-failure --timeout 300

# Breakpoints now bind in the TLS sources
lldb ./tests/test_tls_transport
(lldb) breakpoint set --name tls_server_hs_read_client_hello
(lldb) breakpoint set --name tls_server_hs_read_client_finished
(lldb) run
```

Useful entry points when you are chasing a handshake failure: `tls_server_hs_init`,
`tls_server_hs_read_client_hello` and `tls_server_hs_read_client_finished`
(`src/tls/server_handshake.c`). Every handshake error latches a terminal state and wipes
secrets, so break *before* the failing step — by the time the error surfaces the state is
already gone.

`WEBLIB_TLS_TEST_HOOKS` (default `OFF`) compiles a deterministic-RNG seam that replaces
the system CSPRNG, so that `TlsHttpTests` can reproduce a handshake byte for byte. That
is exactly as dangerous as it sounds — **never enable it in a production build.**

### TLS under sanitizers (matching CI)

CI's `tls-check` job (`.github/workflows/ci.yml`) is the only place TLS code is compiled,
and it builds a second, sanitized configuration. Reproduce it locally:

```bash
cmake -S . -B build-tls-san -DCMAKE_C_COMPILER=clang -DCMAKE_BUILD_TYPE=Debug \
  -DWEBLIB_ENABLE_TLS=ON -DWEBLIB_TLS_TEST_HOOKS=ON \
  -DCMAKE_C_FLAGS="-Wall -Wextra -pedantic -fsanitize=address,undefined -fno-omit-frame-pointer -g"
cmake --build build-tls-san -j

# Only the TLS suites: the signal worth chasing here is in the crypto,
# parser and state-machine code. halt_on_error makes UB fatal rather than logged.
cd build-tls-san && \
  ASAN_OPTIONS=detect_leaks=0 \
  UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
  ctest --output-on-failure --timeout 300 --no-tests=error -R '^Tls'
```

`--no-tests=error` matters with `-R`: without it `ctest` exits 0 when the filter matches
nothing, so a renamed or dropped suite would look like a pass.

## Docker Debugging

### Run Tests in Docker
```bash
./docker-run.sh test
```

### Interactive Debugging Session
```bash
# Start development container
./docker-run.sh dev

# Inside container
cd build
cmake .. -DCMAKE_BUILD_TYPE=Debug
make -j

# Debug with GDB
gdb ./tests/test_weblib
(gdb) break http_parser_execute
(gdb) run
```

## Common Debugging Scenarios

### Debugging Route Parameter Extraction
```bash
# Break on the extraction itself. Prefer symbol names over line numbers --
# `extract_params` is static, but LLDB resolves it in a Debug build and the
# breakpoint survives edits to router.c.
lldb ./build/tests/test_weblib
(lldb) breakpoint set --name extract_params
(lldb) run
```

### Debugging HTTP Parser Issues
```bash
lldb ./build/examples/simple_server
(lldb) breakpoint set --name http_parser_execute
(lldb) run 8080

# In another terminal
curl -X POST http://localhost:8080/api/data -d '{"key":"value"}'
```

### Debugging Memory Leaks
```bash
# Leak/overflow detection needs an instrumented build -- a plain
# -DCMAKE_BUILD_TYPE=Debug build links no sanitizer, so ASAN_OPTIONS is ignored.
cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_C_FLAGS="-fsanitize=address -fno-omit-frame-pointer -g"
cmake --build build-asan -j

# Overflows and use-after-free are reported on all platforms:
./build-asan/tests/test_weblib

# LeakSanitizer is Linux-only. On macOS `detect_leaks=1` aborts with
# "detect_leaks is not supported on this platform" -- run leak checks on Linux,
# or in the Docker dev container (./docker-run.sh dev):
ASAN_OPTIONS=detect_leaks=1 ./build-asan/tests/test_weblib

# Valgrind is also Linux-only (no support for modern/arm64 macOS):
valgrind --leak-check=full ./build/tests/test_weblib
```

### Debugging Async/Event Loop Code
```bash
lldb ./build/examples/async_server
(lldb) breakpoint set --name event_loop_run
(lldb) breakpoint set --name async_read_handler
(lldb) run 8080
```

## VS Code Tasks

Run tasks from the **Terminal** menu → **Run Task**:

- **cmake-configure-debug** - Configure CMake with Debug build type
- **cmake-build-debug** - Build project with debug symbols
- **run-tests** - Build and run tests locally
- **docker-run-tests** - Run tests in Docker container

## Troubleshooting

### CMake Not Found
```bash
# macOS
brew install cmake

# Ubuntu/Debian
sudo apt-get install cmake

# Or use Docker
./docker-run.sh dev
```

### Debugger Won't Start
1. Ensure you've built with `-DCMAKE_BUILD_TYPE=Debug`
2. Check that the executable exists: `ls -la build/tests/test_weblib`
3. Install CodeLLDB extension in VS Code
4. On Linux, you may need to set ptrace permissions:
   ```bash
   echo 0 | sudo tee /proc/sys/kernel/yama/ptrace_scope
   ```

### Breakpoints Not Hitting
- Verify debug symbols: `file build/tests/test_weblib` should show "not stripped"
- Rebuild with debug flags: `cmake -DCMAKE_BUILD_TYPE=Debug ..`
- Check that the source path matches the compiled path

### AddressSanitizer Errors
ASan reports will show:
- **Heap-use-after-free**: Accessing freed memory
- **Heap-buffer-overflow**: Writing past allocated buffer
- **Memory leak**: Allocated memory not freed

Fix by reviewing the stack trace and ensuring proper cleanup in destructors.

## Best Practices

1. **Always build with Debug symbols** when debugging (`-DCMAKE_BUILD_TYPE=Debug`)
2. **Use AddressSanitizer** to catch memory errors early
3. **Set precise breakpoints** at function entry/exit points
4. **Use conditional breakpoints** for loops: `breakpoint modify -c 'i == 42'`
5. **Inspect variables** with `print` (LLDB) or `p` (GDB)
6. **Step through code** with:
   - `next` / `n` - Step over
   - `step` / `s` - Step into
   - `finish` - Step out
7. **Check call stacks** with `backtrace` / `bt`
8. **Watch variables** with `watchpoint set variable <var>`

## Additional Resources

- [LLDB Tutorial](https://lldb.llvm.org/use/tutorial.html)
- [GDB Cheat Sheet](https://darkdust.net/files/GDB%20Cheat%20Sheet.pdf)
- [Valgrind Quick Start](https://valgrind.org/docs/manual/quick-start.html)
- [AddressSanitizer Documentation](https://github.com/google/sanitizers/wiki/AddressSanitizer)
- [VS Code C++ Debugging](https://code.visualstudio.com/docs/cpp/cpp-debug)
