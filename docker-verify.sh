#!/bin/bash
# Docker Verification Script for Modern C Web Library
# This script verifies that the library was built correctly and tests work

set -e

echo "========================================="
echo "Modern C Web Library - Docker Verification"
echo "========================================="
echo ""

# Change to build directory
cd /workspace/build

echo "1. Verifying build artifacts..."
if [ ! -f "libweblib.a" ]; then
    echo "ERROR: Static library not found!"
    exit 1
fi
echo "   ✓ Static library (libweblib.a) exists"

if [ ! -f "libweblib.so" ]; then
    echo "ERROR: Shared library not found!"
    exit 1
fi
echo "   ✓ Shared library (libweblib.so) exists"

if [ ! -f "examples/simple_server" ]; then
    echo "ERROR: simple_server example not found!"
    exit 1
fi
echo "   ✓ Example binary (simple_server) exists"

if [ ! -f "examples/async_server" ]; then
    echo "ERROR: async_server example not found!"
    exit 1
fi
echo "   ✓ Example binary (async_server) exists"

if [ ! -f "tests/test_weblib" ]; then
    echo "ERROR: Test binary not found!"
    exit 1
fi
echo "   ✓ Test binary (test_weblib) exists"

echo ""
echo "2. Running tests..."
./tests/test_weblib
if [ $? -eq 0 ]; then
    echo "   ✓ All tests passed"
else
    echo "ERROR: Tests failed!"
    exit 1
fi

echo ""
echo "3. Library information..."
echo "   Size of libweblib.a: $(du -h libweblib.a | cut -f1)"
echo "   Size of libweblib.so: $(du -h libweblib.so | cut -f1)"

echo ""
echo "========================================="
echo "✓ Verification Complete!"
echo "========================================="
echo ""
echo "The Modern C Web Library has been successfully built and tested."
echo ""
echo "You can now:"
echo "  - Run the simple server: ./build/examples/simple_server"
echo "  - Run the async server:  ./build/examples/async_server"
echo "  - Run tests:             ./build/tests/test_weblib"
echo ""
