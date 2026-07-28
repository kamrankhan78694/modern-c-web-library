#!/usr/bin/env bash
#
# TLS handshake throughput benchmark for the experimental pure-C TLS 1.3 server.
#
# WHY THIS IS A SHELL SCRIPT AND NOT PART OF examples/benchmark_server.c
#
# The in-process benchmark client (src/benchmark.c) speaks plain HTTP over a raw
# socket, and it cannot be extended to HTTPS: this library implements TLS 1.3
# *server-side only* - there is no TLS client anywhere in the tree, and writing
# one is explicitly out of scope (src/tls/README.md). So the only way to measure
# the TLS path is to drive it with a real external client, exactly as
# tests/interop_openssl.sh does for correctness.
#
# WHAT IS MEASURED
#
# Full TLS 1.3 handshakes per second: each iteration is a fresh `openssl
# s_client` process doing X25519 key exchange, Ed25519 signature, the key
# schedule, and one HTTP GET over the encrypted channel. That per-connection
# cost is the number that matters for the server's crypto path.
#
# The figure INCLUDES openssl process spawn (fork+exec+init), which on most
# hosts dominates. So read it as a conservative floor for handshakes/sec, and
# as a regression signal - a large drop means the server's handshake got
# slower - not as an absolute measure of the C code's speed. There is no
# in-process TLS client available to remove that overhead.
#
# Skips (exit 0) when the environment cannot run it, matching interop_openssl.sh,
# so it never produces a false failure.
#
# Usage: benchmark_tls.sh <path-to-tls_server-binary> [handshake-count]
set -u

SERVER="${1:?usage: benchmark_tls.sh <tls_server binary> [handshake-count]}"
COUNT="${2:-50}"

# Validate COUNT before anything else. Without this, a non-integer makes
# `seq 1 "$COUNT"` expand to nothing: the loop runs zero times, fails stays 0,
# and the script prints PASS having performed no handshakes at all - a gate that
# passes without testing anything, which is precisely the defect PR #131 fixed
# in the Valgrind step. Refuse rather than silently measure nothing.
case "$COUNT" in
    ''|*[!0-9]*) echo "FAIL: handshake-count must be a positive integer, got '$COUNT'" >&2; exit 1 ;;
esac
[ "$COUNT" -ge 1 ] 2>/dev/null || { echo "FAIL: handshake-count must be >= 1, got '$COUNT'" >&2; exit 1; }
SRV_PID=""
TMP=""

skip() { echo "SKIP: $*"; exit 0; }
fail() { echo "FAIL: $*"; [ -n "${TMP}" ] && [ -f "$TMP/srv.log" ] && { echo "--- server log ---"; cat "$TMP/srv.log"; }; exit 1; }

cleanup() { [ -n "$SRV_PID" ] && kill "$SRV_PID" 2>/dev/null; wait "$SRV_PID" 2>/dev/null; [ -n "$TMP" ] && rm -rf "$TMP"; }
trap cleanup EXIT

command -v openssl >/dev/null 2>&1 || skip "openssl not found"
[ -x "$SERVER" ] || skip "server binary '$SERVER' not found"
openssl version 2>/dev/null | grep -qi "^OpenSSL " || skip "not OpenSSL (e.g. LibreSSL); TLS 1.3 / Ed25519 support differs"
openssl s_client -help 2>&1 | grep -q -- "-tls1_3" || skip "this openssl s_client has no -tls1_3"

TO=""
command -v timeout >/dev/null 2>&1 && TO="timeout 10"

TMP="$(mktemp -d)" || skip "mktemp failed"

openssl genpkey -algorithm ed25519 -out "$TMP/key.pem" >/dev/null 2>&1 || skip "openssl lacks Ed25519"
openssl req -x509 -new -key "$TMP/key.pem" -out "$TMP/cert.pem" -days 2 \
    -subj "/CN=localhost" >/dev/null 2>&1 || skip "openssl req failed"

# Start the server, retrying a few ports if one is taken.
PORT=$(( ($$ % 2000) + 47000 ))
started=0
for _try in 1 2 3 4 5; do
    "$SERVER" "$TMP/cert.pem" "$TMP/key.pem" "$PORT" >"$TMP/srv.log" 2>&1 &
    SRV_PID=$!
    for _i in $(seq 1 50); do
        if grep -q "HTTPS server" "$TMP/srv.log" 2>/dev/null; then started=1; break; fi
        kill -0 "$SRV_PID" 2>/dev/null || break
        sleep 0.1
    done
    [ "$started" = 1 ] && break
    kill "$SRV_PID" 2>/dev/null; wait "$SRV_PID" 2>/dev/null; SRV_PID=""
    PORT=$((PORT + 1))
done
[ "$started" = 1 ] || fail "server did not become ready"

# NOTE: the request must be piped from printf directly, never stashed in a
# variable first. Command substitution strips trailing newlines, which would eat
# the blank line that terminates the header block - the server would then wait
# for more headers until its deadline, and every "handshake" would time out.
send_request() {
    printf 'GET / HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n' \
        | $TO openssl s_client -quiet -connect "127.0.0.1:$PORT" -tls1_3 2>/dev/null
}

# Warm-up: the first handshake pays one-off costs on both sides.
send_request >/dev/null 2>&1

echo "=== pure-C TLS 1.3 handshake benchmark ==="
echo "TLS_CHACHA20_POLY1305_SHA256 / X25519 / Ed25519 - EXPERIMENTAL, UNAUDITED"
echo "$COUNT full handshakes, each a fresh openssl s_client process."
echo "Includes process-spawn overhead: a conservative floor, and a regression signal."
echo

ok=0
fails=0
START_NS=$( { date +%s%N; } 2>/dev/null )
case "$START_NS" in *N*) START_NS=""; esac   # macOS date has no %N
[ -n "$START_NS" ] || START_S=$(date +%s)

for _i in $(seq 1 "$COUNT"); do
    RESP="$(send_request)"
    case "$RESP" in
        *"HTTP/1.1 200"*) ok=$((ok + 1)) ;;
        *)                fails=$((fails + 1)) ;;
    esac
done

if [ -n "$START_NS" ]; then
    END_NS=$(date +%s%N)
    ELAPSED_MS=$(( (END_NS - START_NS) / 1000000 ))
else
    ELAPSED_MS=$(( ($(date +%s) - START_S) * 1000 ))
fi
[ "$ELAPSED_MS" -gt 0 ] || ELAPSED_MS=1

echo "Handshakes attempted: $COUNT"
echo "Succeeded (HTTP 200): $ok"
echo "Failed:               $fails"
echo "Elapsed:              ${ELAPSED_MS} ms"
echo "Rate:                 $(( ok * 1000 / ELAPSED_MS )) handshakes/s"
echo "Mean per handshake:   $(( ELAPSED_MS / (ok > 0 ? ok : 1) )) ms (incl. process spawn)"

# Any failed handshake means the numbers describe a partly-broken run.
[ "$fails" -eq 0 ] || fail "$fails of $COUNT handshakes did not return HTTP 200"
# ...and a run that performed no handshakes is not a pass, whatever the counters say.
[ "$ok" -ge 1 ] || fail "no handshakes were performed - nothing was measured"
echo
echo "PASS"
