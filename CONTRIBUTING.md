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
- **Potential implementation approach** if you have ideas
- **Impact on existing functionality**

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

## Coding Standards

### C Style Guide

- **C Standard**: Use C11 features
- **Indentation**: 4 spaces (no tabs)
- **Line Length**: Keep lines under 100 characters when possible
- **Braces**: K&R style (opening brace on same line)
- **Naming Conventions**:
  - Functions: `snake_case` (e.g., `http_server_create`)
  - Types: `snake_case_t` suffix (e.g., `http_server_t`)
  - Constants/Macros: `UPPER_SNAKE_CASE` (e.g., `HTTP_OK`)
  - Private functions: Prefix with `_` (e.g., `_internal_helper`)

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
  char* buffer;
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
- **Security Issues**: Email maintainer directly (see SECURITY.md)

## Recognition

Contributors will be recognized in:
- Git commit history
- Release notes
- Contributors section (if we add one)

Thank you for contributing to Modern C Web Library! 🚀

---

**Maintainer**: [@kamrankhan78694](https://github.com/kamrankhan78694)
