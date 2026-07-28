#!/usr/bin/env bash
#
# dev-server.sh — build (if needed) and run the demo app.
#
# Wired to .claude/launch.json so `preview_start` can bring the server up with
# no manual build step. Safe to run directly too:
#
#     tools/dev-server.sh            # port 8080
#     tools/dev-server.sh 9000       # a different port
#
# Incremental: cmake --build is a no-op when nothing changed, so restarting the
# preview after a source edit rebuilds only what moved.
set -eu

PORT="${1:-8080}"
REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO_ROOT"

BUILD_DIR="build"

if [ ! -f "$BUILD_DIR/CMakeCache.txt" ]; then
    echo "[dev-server] configuring ($BUILD_DIR)..." >&2
    cmake -S . -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=RelWithDebInfo \
          -DCMAKE_C_FLAGS="-Wall -Wextra -pedantic" >&2
fi

echo "[dev-server] building demo_app..." >&2
cmake --build "$BUILD_DIR" --target demo_app --parallel >&2

BIN="$BUILD_DIR/examples/demo_app"
[ -x "$BIN" ] || { echo "[dev-server] build produced no $BIN" >&2; exit 1; }

# exec so signals from the preview harness reach the server directly rather
# than this wrapper — otherwise stopping the preview leaves the port bound.
echo "[dev-server] starting on port $PORT" >&2
exec "$BIN" "$PORT"
