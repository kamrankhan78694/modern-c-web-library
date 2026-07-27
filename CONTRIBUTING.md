# Contributing to Modern C Web Library

Thank you for your interest in contributing to the Modern C Web Library! This document provides guidelines and instructions for contributing to the project.

## Table of Contents

- [Code of Conduct](#code-of-conduct)
- [How Can I Contribute?](#how-can-i-contribute)
- [Development Setup](#development-setup)
- [Coding Standards](#coding-standards)
- [Branching Strategy](#branching-strategy)
- [Pull Request Process](#pull-request-process)
- [Testing Guidelines](#testing-guidelines)
- [Documentation](#documentation)

## Code of Conduct

This project adheres to a Code of Conduct that all contributors are expected to follow. Please read [CODE_OF_CONDUCT.md](CODE_OF_CONDUCT.md) before contributing.

## How Can I Contribute?

### Understanding Project Philosophy

Before contributing, please understand that this project follows a **strict pure C philosophy**:

- **No External Dependencies**: This project does not use any third-party libraries or frameworks
- **C Language Only**: All code must be written in standard ISO C (C99 or newer)
- **No Foreign Languages in the Library**: the C library itself contains no Python or JavaScript components and links no non-C code. Helper scripts for build, packaging, and test harnesses are allowed, but they are never compiled into or shipped with the library (see [Language Requirements](#language-requirements))
- **Self-Sufficient Implementation**: We implement all functionality from scratch in C

**Dependency Proposals**: Suggestions to add external libraries, frameworks, or non-C code will not be accepted. This is a fundamental design principle, not a limitation to be worked around. If you believe a dependency is necessary, first consider:
1. Can this be implemented in pure C within the project?
2. Does it truly align with the project's mission of being a foundational, self-sufficient C library?
3. Would it compromise portability, transparency, or educational value?

In nearly all cases, the answer will be to implement the feature in C rather than adding a dependency.

### Reporting Bugs

Before creating bug reports, please check existing issues to avoid duplicates. When creating a bug report, include:

- **Clear title and description** of the issue
- **Steps to reproduce** the behavior
- **Expected behavior** vs actual behavior
- **Environment details** (OS, compiler version, CMake version)
- **Code samples** or test cases if applicable

### Suggesting Enhancements

Enhancement suggestions are welcome! Please provide:

- **Clear description** of the enhancement
- **Use cases** and examples
- **Pure C implementation approach** - explain how it can be implemented without external dependencies
- **Impact on existing functionality**

**Important**: Enhancement suggestions that involve external libraries, frameworks, or non-C code will be closed. All enhancements must be implementable in pure C using only standard library functions and platform-specific system APIs.

### Contributing Code

1. Check the [TODO.md](TODO.md) file for planned features
2. Look for issues labeled `good first issue` or `help wanted`
3. Comment on the issue to express your interest before starting work
4. Fork the repository and create your branch
5. Make your changes following our coding standards
6. Write or update tests as needed
7. Update documentation
8. Submit a pull request

## Development Setup

### Prerequisites

- C11 compatible compiler (GCC 7+, Clang 5+, or MSVC 2019+)
- CMake 3.10 or higher
- Git
- **No other dependencies** - this project uses only standard C and platform APIs

### Building the Project

```bash
# Clone your fork
git clone https://github.com/YOUR_USERNAME/modern-c-web-library.git
cd modern-c-web-library

# Create build directory
mkdir build && cd build

# Configure
cmake ..

# Build
make

# Run tests
make test
```

That default configure leaves the TLS layer **off**, and runs 6 test suites: `WebLibTests`,
`KamranHeaderTests`, `AsyncWebSocketTests`, `StressTests`, `WorkerTests`, `WasmTests`.

#### Building the experimental TLS 1.3 layer

The hand-written pure-C TLS 1.3 server under `src/tls/` is native-only and **OFF by default** —
with `WEBLIB_ENABLE_TLS` off, none of `src/tls/` is compiled. Turn it on if you are changing
anything under `src/tls/`, `examples/tls_server.c`, or the `http_server_enable_tls()` path:

```bash
cmake -S . -B build-tls -DCMAKE_BUILD_TYPE=RelWithDebInfo \
      -DWEBLIB_ENABLE_TLS=ON -DWEBLIB_TLS_TEST_HOOKS=ON
cmake --build build-tls --parallel
cd build-tls && ctest --output-on-failure
```

Use a separate build directory so your default (TLS-off) `build/` stays intact — you will want
both. (`-S`/`-B` and `--parallel` need CMake 3.13+; on an older CMake use the equivalent
`mkdir build-tls && cd build-tls && cmake .. <options>` form.)

This turns the 6 default suites into 13 by adding `TlsTests`, `TlsCryptoTests`, `TlsParseTests`,
`TlsTransportTests`, `TlsFuzzTests`, `TlsHttpTests`, and `TlsInteropOpenssl`. Both options are
required for all 13: `TlsHttpTests` is built only when `WEBLIB_TLS_TEST_HOOKS` is on. Count the
suites rather than trusting a green run — two of them can go missing quietly. `TlsInteropOpenssl`
is registered only if CMake finds a `bash` on `PATH`, and even when registered it self-skips
(reporting a pass) if there is no TLS 1.3-capable `openssl s_client`, so confirm it actually ran.

`WEBLIB_TLS_TEST_HOOKS` exposes a deterministic-RNG seam used only by `TlsHttpTests`. **Never
enable it in a production build** — a deterministic RNG removes all TLS security.

Read [`src/tls/README.md`](src/tls/README.md) before touching this code. The layer is
**EXPERIMENTAL and UNAUDITED** and is not for production use without an external cryptographic
audit. Reports against it are explicitly in scope — see [SECURITY.md](SECURITY.md).

### Running Examples

```bash
# From build directory
./examples/simple_server

# Or the async server
./examples/async_server

# HTTPS example — built only when configured with -DWEBLIB_ENABLE_TLS=ON
# Arguments: <cert.pem> <key.pem> <port>  (defaults: cert.pem key.pem 8443)
./examples/tls_server cert.pem key.pem 8443
```

### Using Docker for Development (Optional)

If you prefer not to install build tools locally, you can use Docker for a consistent development environment:

```bash
# Build the Docker image
docker build -t modern-c-web-library .

# Run verification (builds, tests, and validates)
docker run --rm modern-c-web-library

# Start development environment with docker-compose
docker-compose run --rm weblib-dev
```

**First-time setup inside the container:**

```bash
mkdir -p build && cd build
cmake ..
make
make test
```

**For subsequent rebuilds after code changes:**

```bash
cd build
make
make test
```

Docker automatically mounts your source code, so you can edit files locally and rebuild inside the container. See the [Docker Development Environment](README.md#docker-development-environment) section in README.md for more details.

## Coding Standards

### Project Philosophy

This project is committed to being a **pure C implementation** with zero external dependencies:

- **Pure ISO C**: Use only C99 or newer standard C features
- **No External Libraries**: Do not suggest or use third-party libraries (e.g., no OpenSSL, no libcurl, no external JSON libraries). This is about what the library *links*: the `TlsInteropOpenssl` test drives the `openssl` command-line tool as an independent interop oracle, but nothing in the build links against OpenSSL and the test skips when it is absent
- **Platform APIs Only**: System calls and platform-specific APIs (POSIX, Windows API) are acceptable where necessary for functionality
- **Self-Contained**: Implement all features within the project using C
- **Educational Code**: Write clear, understandable C that serves as a learning resource

### C Style Guide

- **C Standard**: Use C99 or newer features
- **Indentation**: 4 spaces (no tabs)
- **Line Length**: Keep lines under 100 characters when possible
- **Braces**: K&R style (opening brace on same line)
- **Naming Conventions**:
  - Functions: `snake_case` (e.g., `http_server_create`)
  - Types: `snake_case_t` suffix (e.g., `http_server_t`)
  - Constants/Macros: `UPPER_SNAKE_CASE` (e.g., `HTTP_OK`)
  - Private functions: Prefix with `_` (e.g., `_internal_helper`)

### Language Requirements

- **C Only**: All source files must be `.c` and `.h` files written in C
- **No Scripts in the Library**: `src/`, `include/`, and `examples/` are pure C. The one exception is `examples/worker.js`, the Cloudflare Worker glue that loads the WASM build. Helper scripts (shell or Python) are permitted **outside** the library for build, packaging, and test-harness plumbing only — for example `tests/interop_openssl.sh`, which drives a real `openssl s_client` handshake against the pure-C TLS server and is registered as the `TlsInteropOpenssl` ctest suite. Such a script must never become a runtime or link-time dependency of the library, and must skip cleanly when the external tool it needs is unavailable
- **No Wrappers**: Do not create language bindings or wrappers for other languages
- **No Code Generators**: Do not use tools that generate C code from other languages
- **Build System**: CMake is acceptable as a build system (not considered a dependency)

### Example Code Style

```c
// Good
typedef struct {
    int fd;
    char *buffer;
} connection_t;

int http_server_create(uint16_t port) {
    if (port == 0) {
        return -1;
    }
    
    // Implementation
    return 0;
}

// Bad
typedef struct {
  int fd;
  char* buffer; // Incorrect: asterisk should be adjacent to variable name (should be 'char *buffer;')
}Connection;

int HTTPServerCreate(uint16_t Port){
  if(Port==0) return -1;
  return 0;
}
```

### Code Quality

- **No warnings**: the build adds `-Wall -Wextra -pedantic` on GCC/Clang (`CMakeLists.txt`), and CI compiles with those flags under both GCC and Clang. A clean build means zero warnings, not "no errors" — warnings introduced by a PR are treated as review blockers
- **Memory safety**: All allocated memory must be freed
- **Error handling**: Check return values and handle errors properly
- **Thread safety**: Document thread-safety guarantees
- **Comments**: Use comments for complex logic, not obvious code
- **Header guards**: Use `#ifndef` guards in header files
- **No External Dependencies**: Never introduce dependencies on third-party libraries
- **Pure C Implementation**: Implement features in C, not through external tools or languages

### Common Bug Patterns to Avoid

This section documents recurring bug patterns found during code review. **All contributors
and reviewers should check for these patterns before merging.**

#### 1. Do Not Duplicate Public Security APIs

The library provides public security utility functions in `security_utils.c`. **Always use
these instead of reimplementing the same logic privately:**

| Need | Public API | Do NOT reimplement |
|------|-----------|-------------------|
| Wipe sensitive memory | `secure_zero(ptr, len)` | Private `_secure_wipe()`, `volatile` loops |
| Constant-time comparison | `secure_compare(a, b, len)` | Private `_ct_strcmp()`, XOR loops |
| Cryptographic random bytes | `secure_random_bytes(buf, len)` | Private `/dev/urandom` readers |

```c
// ✗ BAD — reimplements secure_zero() privately
static void _secure_wipe(void *ptr, size_t len) {
    volatile unsigned char *p = (volatile unsigned char *)ptr;
    while (len--) *p++ = 0;
}

// ✓ GOOD — uses the public API
#include "kamran.k"
secure_zero(buffer, sizeof(buffer));
```

**Why this matters:** If a bug is found in one private copy, the other copies may not be
updated. All security-critical operations should have a single implementation.

#### 2. Deep-Copy Strings in Middleware Config

When a middleware `*_create()` function receives a config struct containing `const char *`
string fields, it **must deep-copy them with `strdup()`** — never shallow-copy with
`memcpy()`. The caller may free or reuse the original strings after `*_create()` returns.

```c
// ✗ BAD — shallow copy stores caller's pointers (dangling pointer risk)
g_config = malloc(sizeof(*g_config));
memcpy(g_config, user_config, sizeof(*g_config));

// ✓ GOOD — deep copy owns its own strings
g_config = calloc(1, sizeof(*g_config));
if (user_config->policy) {
    g_config->policy = strdup(user_config->policy);
    if (!g_config->policy) { /* handle failure */ }
}
```

The corresponding `*_destroy()` function must free each deep-copied string:

```c
void my_middleware_destroy(void) {
    if (g_config) {
        free((void *)g_config->policy);
        free(g_config);
        g_config = NULL;
    }
}
```

**Reference implementations:** `middleware_cors.c`, `middleware_auth.c`, `middleware_static.c`.

#### 3. Use `secure_zero()` Instead of `memset()` for Sensitive Data

The C standard allows compilers to optimize away `memset()` calls when the buffer is not
read afterward ("dead store elimination"). This means `memset(secret, 0, len)` before
`free(secret)` may be silently removed by the compiler, leaving sensitive data in memory.

```c
// ✗ BAD — compiler may optimize this away
memset(decoded_password, 0, sizeof(decoded_password));
free(jwt_secret);

// ✓ GOOD — volatile pointer prevents optimization
secure_zero(decoded_password, sizeof(decoded_password));
secure_zero(jwt_secret, secret_len);
free(jwt_secret);
```

**Applies to:** passwords, API keys, JWT secrets, CSRF tokens, session IDs, decoded
credentials, and any buffer that held plaintext secrets.

#### 4. Handle `strdup()` / `malloc()` Failures in `*_create()` Functions

When a middleware `*_create()` function makes multiple allocations (e.g., `strdup()` for
several string fields), each allocation must be checked. On failure, all previously
successful allocations must be cleaned up — typically by calling the corresponding
`*_destroy()` function.

```c
// ✗ BAD — ignores strdup failure, leaks earlier allocations
g_config->field_a = strdup(config->field_a);
g_config->field_b = strdup(config->field_b); // What if this fails?

// ✓ GOOD — checks each allocation, cleans up on failure
if (config->field_a) {
    g_config->field_a = strdup(config->field_a);
    if (!g_config->field_a) {
        my_middleware_destroy();  // Frees g_config + any earlier strdup'd fields
        return NULL;
    }
}
```

#### 5. Pull Request Checklist Additions for Security Code

When your PR touches middleware configs or security-sensitive code, verify:

- [ ] No private reimplementation of `secure_zero`, `secure_compare`, or `secure_random_bytes`
- [ ] All `const char *` config fields are deep-copied with `strdup()` in `*_create()`
- [ ] All deep-copied strings are freed in `*_destroy()`
- [ ] All `strdup()` / `malloc()` calls are checked for `NULL` return
- [ ] Sensitive data is wiped with `secure_zero()`, not `memset()`
- [ ] No dangling pointers to caller-owned or stack-allocated strings

#### 6. Additional Requirements for `src/tls/` Changes

`src/tls/` is a hand-written, **experimental and unaudited** TLS 1.3 server (see
[`src/tls/README.md`](src/tls/README.md)). It is gated behind `WEBLIB_ENABLE_TLS`, which defaults
to **OFF** — a plain `cmake ..` compiles none of it, so the default build and test run described
above will not exercise a single line you changed. The extra flags below are not optional polish:
without them you have not tested your patch.

- [ ] Built and tested with `-DWEBLIB_ENABLE_TLS=ON -DWEBLIB_TLS_TEST_HOOKS=ON` and all 13 ctest
      suites pass. Both flags are needed: `WEBLIB_ENABLE_TLS` alone omits `TlsHttpTests`.
- [ ] Confirmed `TlsInteropOpenssl` actually ran. It self-skips without a TLS 1.3-capable
      `openssl s_client`, and a skip reads as a pass.
- [ ] Ran the 7 TLS suites under ASan + UBSan and they are green (see the command below). This
      is a CI gate (`tls-check` in `.github/workflows/ci.yml`), not a suggestion.
- [ ] Any new or changed cryptographic primitive has a known-answer test against the RFC vectors
      in `tests/test_tls_crypto.c` — not just a round-trip test.
- [ ] Comparisons of secrets, MACs, or verify_data are constant-time (`secure_compare()`); no
      secret-dependent branching, indexing, or early return.
- [ ] Every new error path in the handshake fails closed: latch a terminal state, wipe secrets,
      and never continue into a state that assumes authentication succeeded.
- [ ] Secrets and key material are wiped with `secure_zero()` on every exit path, including
      error paths.

The sanitizer run CI performs, reproduced locally:

```bash
cmake -S . -B build-tls-san -DCMAKE_C_COMPILER=clang -DCMAKE_BUILD_TYPE=Debug \
      -DWEBLIB_ENABLE_TLS=ON -DWEBLIB_TLS_TEST_HOOKS=ON \
      -DCMAKE_C_FLAGS="-Wall -Wextra -pedantic -fsanitize=address,undefined -fno-omit-frame-pointer -g"
cmake --build build-tls-san --parallel
cd build-tls-san && ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
  ctest --output-on-failure --timeout 300 --no-tests=error -R '^Tls'
```

Note `ASAN_OPTIONS=detect_leaks=0`: a green run shows memory safety and absence of undefined
behavior, **not** absence of leaks — check allocation ownership by hand. `--no-tests=error`
matters with `-R`, because plain `ctest -R` exits 0 when the filter matches nothing.

## Versioning Policy

This project follows [Semantic Versioning 2.0.0](https://semver.org/spec/v2.0.0.html).

Given a version number **MAJOR.MINOR.PATCH**:

- **MAJOR** is incremented for incompatible public API changes (e.g., removing a function,
  changing a function signature, renaming a type in `include/kamran.k`)
- **MINOR** is incremented for backwards-compatible new functionality (e.g., adding a new
  middleware, a new route helper, or a new JSON utility)
- **PATCH** is incremented for backwards-compatible bug fixes (e.g., fixing a memory leak,
  correcting parser behavior, resolving a race condition)

### Where Version Is Defined

The version is declared in **three places** that must stay in sync (plus `publish-package.sh`, whose CLI default should be bumped alongside them):

| Location | What to update |
|----------|---------------|
| `CMakeLists.txt` | `project(ModernCWebLibrary VERSION X.Y.Z ...)` |
| `include/kamran.k` | `WEBLIB_VERSION_MAJOR`, `WEBLIB_VERSION_MINOR`, `WEBLIB_VERSION_PATCH`, and `WEBLIB_VERSION` |
| `Dockerfile.release` | the two `LABEL org.opencontainers.image.version="X.Y.Z"` lines |

A compile-time static assertion in `src/http_server.c` will **fail the build** if the first two
sources disagree, so that mismatch is caught immediately. The `Dockerfile.release` labels are
**not** covered by that assertion — update them by hand.

### When Bumping the Version

1. Update `CMakeLists.txt`, `include/kamran.k`, and `Dockerfile.release` in the same commit
2. Add a new section to `CHANGELOG.md` following Keep a Changelog format
3. Tag the release commit with `vMAJOR.MINOR.PATCH` (e.g., `v1.1.0`)

### Pre-release and Build Metadata

Pre-release versions may use a hyphen suffix per semver (e.g., `2.0.0-alpha.1`).
Build metadata may use a plus suffix (e.g., `1.0.0+build.42`). These are currently
not used but are reserved for future use.

## Branching Strategy

- **main**: Stable, released code. The one carve-out is the experimental TLS layer in `src/tls/`,
  which lives on `main` but is off by default and unaudited — being merged here does not make it
  production-ready (see [SECURITY.md](SECURITY.md))
- **develop**: Integration branch for features (if used)
- **feature/**: Feature branches (e.g., `feature/websocket-support`)
- **bugfix/**: Bug fix branches (e.g., `bugfix/memory-leak-fix`)
- **docs/**: Documentation updates (e.g., `docs/api-reference`)

### Branch Naming

Use descriptive names with prefixes:

```
feature/add-websocket-support
bugfix/fix-json-parser-crash
docs/update-api-documentation
```

## Pull Request Process

1. **Update your fork**: Sync with the main repository before starting
   ```bash
   git remote add upstream https://github.com/kamrankhan78694/modern-c-web-library.git
   git fetch upstream
   git merge upstream/main
   ```

2. **Create a feature branch**: 
   ```bash
   git checkout -b feature/your-feature-name
   ```

3. **Make your changes**: Follow coding standards and commit logically
   ```bash
   git add .
   git commit -m "Add feature: brief description"
   ```

4. **Write/update tests**: Ensure your changes are tested

5. **Update documentation**: Update README.md, API docs, or comments

6. **Run tests locally**: 
   ```bash
   cd build
   make test
   ```
   If your change touches `src/tls/` or `http_server_enable_tls()`, that run proves nothing —
   see [Testing the experimental TLS layer](#testing-the-experimental-tls-layer) for the
   configuration you also have to run.

7. **Push to your fork**:
   ```bash
   git push origin feature/your-feature-name
   ```

8. **Create Pull Request**: 
   - Use a clear title describing the change
   - Reference any related issues (e.g., "Fixes #123")
   - Describe what changed and why
   - List any breaking changes
   - Add screenshots for UI/output changes

### Pull Request Checklist

- [ ] Code follows project style guidelines
- [ ] Code compiles without warnings (`-Wall -Wextra -pedantic`)
- [ ] Tests pass locally — and if the change touches `src/tls/`, in the TLS-on configuration too
- [ ] New tests added for new features
- [ ] Documentation updated
- [ ] Commit messages are clear and descriptive
- [ ] No unnecessary changes or reformatting
- [ ] Branch is up to date with main

### Code Review

- Be responsive to feedback
- Be respectful and constructive
- Address all review comments
- Update PR based on feedback
- Request re-review when ready

## Testing Guidelines

### Writing Tests

- Add tests for all new features
- Add tests for bug fixes to prevent regression
- Use descriptive test names
- Keep tests focused and simple
- Test edge cases and error conditions

### Test Structure

```c
// tests/test_feature.c
#include "kamran.k"
#include <assert.h>
#include <stdio.h>

void test_feature_basic(void) {
    // Setup
    feature_t *f = feature_create();
    assert(f != NULL);
    
    // Test
    int result = feature_do_something(f);
    assert(result == 0);
    
    // Cleanup
    feature_destroy(f);
}

int main(void) {
    printf("Running feature tests...\n");
    test_feature_basic();
    printf("All tests passed!\n");
    return 0;
}
```

### Running Tests

```bash
# Run all tests
cd build
make test

# Run specific test
./tests/test_weblib

# Run with verbose output
ctest --verbose
```

In a default configure this is 6 suites, and it compiles **none** of `src/tls/`.

### Testing the experimental TLS layer

If you change anything under `src/tls/`, `examples/tls_server.c`, or the
`http_server_enable_tls()` path, you must also run the TLS-on configuration — otherwise a green
local run has tested nothing you wrote:

```bash
cmake -S . -B build-tls -DCMAKE_BUILD_TYPE=RelWithDebInfo \
      -DWEBLIB_ENABLE_TLS=ON -DWEBLIB_TLS_TEST_HOOKS=ON
cmake --build build-tls --parallel
cd build-tls && ctest --output-on-failure          # 13 suites: the 6 above + 7 TLS suites

# Just the TLS suites. Use --no-tests=error (CTest 3.20+): with -R alone,
# ctest exits 0 if the filter matches nothing.
ctest --output-on-failure --no-tests=error -R '^Tls'
```

`WEBLIB_TLS_TEST_HOOKS` is **test-only** — it exposes a deterministic-RNG seam that `TlsHttpTests`
needs. Never enable it in a production or library build. The TLS layer itself is **experimental
and unaudited**; see [`src/tls/README.md`](src/tls/README.md).

### The two-configuration CI gate

CI (`.github/workflows/ci.yml`) runs both configurations, and a PR has to be green in both:

| Job | What it runs |
|-----|--------------|
| `primary-checks` | GCC build in Docker, the full default (TLS-off) suite, plus a Valgrind memory check |
| `clang-check` | Same default configuration built with Clang |
| `tls-check` | A `RelWithDebInfo` TLS build running all 13 suites, then a Clang ASan/UBSan build running the 7 TLS suites |
| `macos-check` | macOS compatibility build and tests (pull requests only) |
| `docker-image-check` | Builds and verifies the production Docker image |

The practical consequence: TLS-off and TLS-on are two different compilations of this repository,
and a change can break one while leaving the other green. Build both locally before you push.

## Documentation

### Code Documentation

- Document all public APIs in header files
- Use clear, concise comments
- Include usage examples for complex features
- Document thread-safety requirements
- Note any platform-specific behavior

### API Documentation Format

```c
/**
 * Create a new HTTP server instance.
 * 
 * @return Pointer to the server, or NULL on failure
 * 
 * @note The returned server must be destroyed with http_server_destroy()
 * @note This function is thread-safe
 */
http_server_t *http_server_create(void);
```

### README Updates

When adding new features, update:
- Feature list
- Usage examples
- API reference
- Roadmap (if feature was planned)

## Getting Help

- **Questions**: Open a GitHub issue with the `question` label
- **Discussions**: Use GitHub Discussions for general topics
- **Security Issues**: Do not open a public issue. Report privately via a [GitHub security advisory](https://github.com/kamrankhan78694/modern-c-web-library/security/advisories/new), or the "Report a vulnerability" button in the repository Security tab. See [SECURITY.md](SECURITY.md) for scope, timelines, and safe harbor

## Why Pure C?

This project demonstrates that modern web functionality—including async I/O, routing, middleware, and JSON handling—can be elegantly implemented in pure C without external dependencies. By maintaining this discipline, we:

- **Ensure Portability**: Code runs anywhere with a C compiler
- **Maintain Transparency**: Every line of code is visible and auditable
- **Provide Educational Value**: Developers can study foundational implementations
- **Avoid Dependency Hell**: No version conflicts, no supply chain risks
- **Emphasize Craftsmanship**: Show what's possible with well-designed C code

This is not a limitation—it's a commitment to excellence in C programming.

## Recognition

Contributors will be recognized in:
- Git commit history
- Release notes

Thank you for contributing to Modern C Web Library! 🚀

---

**Maintainer**: [@kamrankhan78694](https://github.com/kamrankhan78694)
