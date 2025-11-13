#!/bin/bash
# Helper script to run Modern C Web Library in Docker

set -e

# Colors for output
GREEN='\033[0;32m'
BLUE='\033[0;34m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

print_usage() {
    echo "Usage: $0 [MODE] [PORT]"
    echo ""
    echo "MODE:"
    echo "  async      - Run async server (default)"
    echo "  threaded   - Run threaded server"
    echo "  dev        - Run development container with shell"
    echo "  build      - Build Docker image only"
    echo "  test       - Build and run tests in container"
    echo ""
    echo "PORT: Port to expose (default: 8080)"
    echo ""
    echo "Examples:"
    echo "  $0                    # Run async server on port 8080"
    echo "  $0 async 3000         # Run async server on port 3000"
    echo "  $0 dev                # Start development container"
    echo "  $0 test               # Run tests in container"
}

MODE="${1:-async}"
PORT="${2:-8080}"

case "$MODE" in
    async)
        echo -e "${BLUE}Building and running async server...${NC}"
        docker build -t modern-c-weblib:latest .
        echo -e "${GREEN}Starting async server on port $PORT${NC}"
        docker run --rm -p "$PORT:8080" --name modern-c-weblib-async modern-c-weblib:latest /app/async_server 8080
        ;;
    
    threaded)
        echo -e "${BLUE}Building and running threaded server...${NC}"
        docker build -t modern-c-weblib:latest .
        echo -e "${GREEN}Starting threaded server on port $PORT${NC}"
        docker run --rm -p "$PORT:8080" --name modern-c-weblib-threaded modern-c-weblib:latest /app/simple_server 8080
        ;;
    
    dev)
        echo -e "${BLUE}Building development container...${NC}"
        docker build -f Dockerfile.dev -t modern-c-weblib:dev .
        echo -e "${GREEN}Starting development container (access at localhost:$PORT)${NC}"
        echo -e "${YELLOW}Container includes: cmake, make, gdb, valgrind${NC}"
        docker run --rm -it -v "$(pwd):/workspace" -p "$PORT:8080" --name modern-c-weblib-dev modern-c-weblib:dev
        ;;
    
    build)
        echo -e "${BLUE}Building Docker images...${NC}"
        docker build -t modern-c-weblib:latest .
        docker build -f Dockerfile.dev -t modern-c-weblib:dev .
        echo -e "${GREEN}Build complete!${NC}"
        ;;
    
    test)
        echo -e "${BLUE}Building and running tests...${NC}"
        docker build -f Dockerfile.dev -t modern-c-weblib:dev .
        echo -e "${GREEN}Running tests...${NC}"
        docker run --rm modern-c-weblib:dev /bin/bash -c "cd build && make test && ./tests/test_weblib"
        ;;
    
    -h|--help|help)
        print_usage
        exit 0
        ;;
    
    *)
        echo -e "${YELLOW}Unknown mode: $MODE${NC}"
        print_usage
        exit 1
        ;;
esac
