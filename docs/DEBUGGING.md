# Debugging Guide

This guide explains how to debug the Modern C Web Library using VS Code, command-line tools, and Docker.

## Prerequisites

### macOS/Linux
- **CMake** (3.10+): `brew install cmake` (macOS) or `apt-get install cmake` (Linux)
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

### 2. Debug Tests
1. Open the **Run and Debug** panel (Cmd+Shift+D / Ctrl+Shift+D)
2. Select **"Debug Tests (LLDB)"** from the dropdown
3. Press **F5** to start debugging
4. Set breakpoints in test code (`tests/test_weblib.c`) or library code (`src/*.c`)

The debugger will:
- Build the tests automatically (via `preLaunchTask`)
- Run `./build/tests/test_weblib` under LLDB
- Enable AddressSanitizer leak detection (`ASAN_OPTIONS=detect_leaks=1`)

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

# Debug tests
lldb ./build/tests/test_weblib
(lldb) breakpoint set --name test_router_params_single
(lldb) run

# Debug server
lldb ./build/examples/simple_server
(lldb) breakpoint set --file router.c --line 120
(lldb) run 8080
```

### Using GDB (Linux)
```bash
gdb ./build/tests/test_weblib
(gdb) break test_router_params_single
(gdb) run

# Or attach to running server
gdb -p $(pgrep simple_server)
```

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
```bash
# Configure with ASan
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_C_FLAGS="-fsanitize=address -fno-omit-frame-pointer"
cmake --build build -j

# Run tests (ASan will report leaks/overflows automatically)
./build/tests/test_weblib
```

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
# Set breakpoints in router.c
lldb ./build/tests/test_weblib
(lldb) breakpoint set --file router.c --line 213  # extract_params
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
# Build with debug symbols
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j

# Run with leak detection
ASAN_OPTIONS=detect_leaks=1 ./build/tests/test_weblib

# Or use Valgrind on Linux
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
