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
- **No Foreign Languages**: Python scripts, JavaScript, or any other language integrations are prohibited
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

### Running Examples

```bash
# From build directory
./examples/simple_server

# Or the async server
./examples/async_server
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

Docker automatically mounts your source code, so you can edit files locally and rebuild inside the container. See the [Docker Development Environment](#docker-development-environment) section in README.md for more details.

## Coding Standards

### Project Philosophy

This project is committed to being a **pure C implementation** with zero external dependencies:

- **Pure ISO C**: Use only C99 or newer standard C features
- **No External Libraries**: Do not suggest or use third-party libraries (e.g., no OpenSSL, no libcurl, no external JSON libraries)
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
- **No Scripts**: Do not add Python, shell, JavaScript, or any other language scripts
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

- **No warnings**: Code must compile without warnings
- **Memory safety**: All allocated memory must be freed
- **Error handling**: Check return values and handle errors properly
- **Thread safety**: Document thread-safety guarantees
- **Comments**: Use comments for complex logic, not obvious code
- **Header guards**: Use `#ifndef` guards in header files
- **No External Dependencies**: Never introduce dependencies on third-party libraries
- **Pure C Implementation**: Implement features in C, not through external tools or languages

## Branching Strategy

- **main**: Stable production-ready code
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
- [ ] Code compiles without warnings
- [ ] Tests pass locally
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
#include "weblib.h"
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
- **Security Issues**: Contact the maintainer directly via GitHub ([@kamrankhan78694](https://github.com/kamrankhan78694))

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
