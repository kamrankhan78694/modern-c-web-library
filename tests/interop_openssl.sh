#!/usr/bin/env bash
#
# Real-client interoperability test: drive the pure-C TLS 1.3 server with a genuine
# `openssl s_client` handshake and a plain HTTP GET, and check the encrypted response.
#
# Skips (exit 0) when the environment can't run it — no openssl, or an openssl too old
# for Ed25519 / TLS 1.3 — so it never produces a false failure on such machines.
#
# Usage: interop_openssl.sh <path-to-tls_server-binary>
set -u

SERVER="${1:?usage: interop_openssl.sh <tls_server binary>}"
SRV_PID=""
TMP=""

skip() { echo "SKIP: $*"; exit 0; }
fail() { echo "FAIL: $*"; [ -n "${TMP}" ] && [ -f "$TMP/srv.log" ] && { echo "--- server log ---"; cat "$TMP/srv.log"; }; exit 1; }

cleanup() { [ -n "$SRV_PID" ] && kill "$SRV_PID" 2>/dev/null; wait "$SRV_PID" 2>/dev/null; [ -n "$TMP" ] && rm -rf "$TMP"; }
trap cleanup EXIT

command -v openssl >/dev/null 2>&1 || skip "openssl not found"
[ -x "$SERVER" ] || skip "server binary '$SERVER' not found"

TO=""
command -v timeout >/dev/null 2>&1 && TO="timeout 10"

TMP="$(mktemp -d)" || skip "mktemp failed"

# Self-signed Ed25519 cert — if this openssl has no Ed25519, it can't talk to an
# Ed25519-only server anyway, so skip rather than fail.
openssl genpkey -algorithm ed25519 -out "$TMP/key.pem" >/dev/null 2>&1 || skip "openssl lacks Ed25519"
openssl req -x509 -new -key "$TMP/key.pem" -out "$TMP/cert.pem" -days 2 \
    -subj "/CN=localhost" >/dev/null 2>&1 || skip "openssl req failed"

# Start the server, retrying a few ports if one is taken.
PORT=$(( ($$ % 2000) + 45000 ))
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

# Genuine TLS 1.3 handshake + HTTP GET via openssl s_client.
RESP="$(printf 'GET / HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n' \
    | $TO openssl s_client -quiet -connect "127.0.0.1:$PORT" -tls1_3 2>/dev/null)"

echo "--- openssl s_client response ---"
printf '%s\n' "$RESP" | head -8

if printf '%s' "$RESP" | grep -q "HTTP/1.1 200" \
   && printf '%s' "$RESP" | grep -q "Hello over pure-C TLS 1.3"; then
    echo "PASS: openssl s_client completed a TLS 1.3 + HTTPS round-trip against the pure-C server"
    exit 0
fi
fail "no valid HTTPS 200 response received over TLS"
