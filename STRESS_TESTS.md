# Stress Test Suite Documentation

## Overview

The `tests/test_stress.c` file contains a comprehensive production-level stress test suite designed to push the Modern C Web Library to its limits and verify proper handling of edge cases, resource limits, and high-load scenarios.

## Running the Tests

### Standard Execution
```bash
cd build
./tests/test_stress
```

### Via CTest
```bash
cd build
ctest -R StressTests
```

### Skip Server Integration Tests
If running in a constrained environment, you can skip the HTTP server integration tests:
```bash
SKIP_SERVER_TESTS=1 ./tests/test_stress
```

## Test Categories

### 1. Router Stress Tests (4 tests)
- **Max Routes Limit**: Verifies that the router correctly handles MAX_ROUTES (256) routes and rejects the 257th
- **Max Middlewares Limit**: Verifies that MAX_MIDDLEWARES (32) are allowed and the 33rd is rejected
- **Long Paths**: Tests routes with paths close to 4096 characters
- **Many Parameters**: Tests routes with 20 path parameters

### 2. JSON Parser Stress Tests (6 tests)
- **Deep Nesting Limit**: Tests JSON with depth close to JSON_MAX_DEPTH (512) and verifies parser rejects depth 513
- **Large Object**: Creates and parses JSON objects with 1000+ key-value pairs
- **Large Array**: Tests arrays with 10,000 elements
- **Large String**: Tests JSON strings with 100KB of data
- **Malformed Fuzzing**: Tries 20+ malformed JSON inputs to ensure no crashes
- **Repeated Parse/Free**: Parses and frees the same JSON 10,000 times to detect memory leaks

### 3. Cache Stress Tests (3 tests)
- **Fill and Eviction**: Fills cache beyond max_entries and verifies proper LRU eviction
- **Rapid Set/Get**: Performs 10,000 rapid cache operations
- **TTL Accuracy**: Tests time-to-live expiration with 1-second TTLs

### 4. Session Stress Tests (3 tests)
- **Mass Creation**: Creates MAX_SESSIONS (1024) sessions and verifies the limit
- **Data Operations**: Sets and retrieves 100 key-value pairs on a single session
- **Cleanup**: Tests session expiration and cleanup

### 5. HTTP Server Integration Stress Tests (6 tests)
- **Rapid Sequential Connections**: Sends 100 sequential HTTP requests rapidly
- **Concurrent Connections**: Launches 5 threads each sending 4 requests concurrently
- **Large Request Body**: Tests handling of 100KB request body
- **Oversized Request**: Verifies server rejects requests > 1MB
- **Many Headers**: Sends requests with 90 headers (close to MAX_HEADER_COUNT of 100)
- **Slow Client**: Tests partial request handling with delays

### 6. Input Validation Stress Tests (2 tests)
- **Long Strings**: Validates 100KB strings
- **HTML Sanitization**: Sanitizes strings with thousands of `<script>` tags

### 7. Compression Stress Tests (1 test)
- **Large Payload**: Computes CRC32 of 1MB payload

### 8. Memory Lifecycle Stress Tests (3 tests)
- **Server Create/Destroy**: Creates and destroys 100 HTTP servers in a loop
- **Router Create/Destroy**: Creates and destroys 100 routers with routes
- **Event Loop Create/Destroy**: Creates and destroys 100 event loops

## Test Results

All 28 stress tests pass successfully:
```
Tests run: 28
Tests passed: 28
Tests failed: 0
```

## Implementation Details

- Uses the same TEST/ASSERT/PASS macro pattern as `test_weblib.c`
- Server integration tests use ports 19000-19005 to avoid conflicts
- Socket timeouts set to 2 seconds to prevent hanging
- Connection: close headers used to ensure proper connection cleanup
- Proper cleanup and resource management in all tests
- No memory leaks detected across repeated operations

## Known Limits Tested

| Component | Limit | Value |
|-----------|-------|-------|
| Router | MAX_ROUTES | 256 |
| Router | MAX_MIDDLEWARES | 32 |
| Sessions | MAX_SESSIONS | 1024 |
| JSON Parser | JSON_MAX_DEPTH | 512 |
| HTTP | MAX_HEADER_COUNT | 100 |
| HTTP | MAX_BODY_BYTES | 1 MB |

## Security

CodeQL analysis shows 0 security alerts in the stress test code.
