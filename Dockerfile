# Modern C Web Library - Development Environment
# This Dockerfile provides a consistent environment for building, testing, and contributing
# to the Modern C Web Library.

FROM ubuntu:22.04

# Prevent interactive prompts during package installation
ENV DEBIAN_FRONTEND=noninteractive

# Install build tools and dependencies
RUN apt-get update && apt-get install -y \
    build-essential \
    gcc \
    g++ \
    make \
    cmake \
    git \
    && rm -rf /var/lib/apt/lists/*

# Set working directory
WORKDIR /workspace

# Copy project files
COPY . .

# Build the library
RUN mkdir -p build && \
    cd build && \
    cmake .. && \
    make

# Run tests to verify the build
RUN cd build && make test

# Default command: Run verification script
CMD ["/workspace/docker-verify.sh"]
