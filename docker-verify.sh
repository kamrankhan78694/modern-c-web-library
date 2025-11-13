#!/bin/bash
# Verification script for Docker setup
# Run this after building the Docker image to verify everything works

set -e

echo "=========================================="
echo "Modern C Web Library - Docker Verification"
echo "=========================================="
echo ""

# Check if Docker is running
if ! docker info > /dev/null 2>&1; then
    echo "❌ Error: Docker is not running"
    echo "Please start Docker Desktop and try again"
    exit 1
fi

echo "✓ Docker is running"
echo ""

# Build development image
echo "Building development Docker image..."
docker build -f Dockerfile.dev -t modern-c-weblib:dev . > /dev/null 2>&1
echo "✓ Development image built successfully"
echo ""

# Run tests in container
echo "Running tests in Docker container..."
TEST_OUTPUT=$(docker run --rm modern-c-weblib:dev /bin/bash -c "cd build && ./tests/test_weblib" 2>&1)
echo "$TEST_OUTPUT"

if echo "$TEST_OUTPUT" | grep -q "Tests failed: 0"; then
    echo ""
    echo "✓ All tests passed in Docker environment"
else
    echo ""
    echo "❌ Tests failed - see output above"
    exit 1
fi

echo ""

# Verify examples can be built and started
echo "Verifying examples are built..."
docker run --rm modern-c-weblib:dev /bin/bash -c "test -f build/examples/simple_server && test -f build/examples/async_server"
echo "✓ Example executables are present"
echo ""

# Build production image
echo "Building production Docker image..."
docker build -t modern-c-weblib:latest . > /dev/null 2>&1
echo "✓ Production image built successfully"
echo ""

# Verify production image contains executables
echo "Verifying production image..."
docker run --rm modern-c-weblib:latest /bin/bash -c "test -f /app/simple_server && test -f /app/async_server"
echo "✓ Production image verified"
echo ""

# Summary
echo "=========================================="
echo "✓ Docker Setup Verification Complete!"
echo "=========================================="
echo ""
echo "Your Docker environment is ready for:"
echo "  • Building the library"
echo "  • Running tests"
echo "  • Development with volume mounts"
echo "  • Production deployment"
echo ""
echo "Next steps:"
echo "  ./docker-run.sh dev     # Start development environment"
echo "  ./docker-run.sh test    # Run tests"
echo "  ./docker-run.sh async   # Run server"
echo ""
echo "See DOCKER.md for complete documentation."
echo ""
