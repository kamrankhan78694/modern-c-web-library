# Multi-stage Dockerfile for Modern C Web Library

# Stage 1: Builder
FROM gcc:11 as builder

# Install build dependencies
RUN apt-get update && apt-get install -y \
    cmake \
    make \
    && rm -rf /var/lib/apt/lists/*

# Set working directory
WORKDIR /build

# Copy source code
COPY . .

# Build the library and examples
RUN mkdir -p build && cd build && \
    cmake .. && \
    make

# Stage 2: Runtime
FROM debian:bullseye-slim

# Install runtime dependencies
RUN apt-get update && apt-get install -y \
    libpthread-stubs0-dev \
    && rm -rf /var/lib/apt/lists/*

# Create app directory
WORKDIR /app

# Copy built executables from builder stage
COPY --from=builder /build/build/examples/simple_server /app/simple_server
COPY --from=builder /build/build/examples/async_server /app/async_server

# Expose default port
EXPOSE 8080

# Default to running the async server (better for production)
CMD ["/app/async_server", "8080"]
