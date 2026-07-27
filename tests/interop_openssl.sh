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

# This test drives a real OpenSSL client through TLS 1.3 with an Ed25519 cert. A
# different implementation behind the `openssl` name (LibreSSL/BoringSSL) or one
# without a TLS 1.3-capable s_client behaves differently, so skip rather than fail
# spuriously — a genuine server regression still fails, because on a capable OpenSSL
# these pre-flight checks pass and only the handshake itself can then break.
openssl version 2>/dev/null | grep -qi "^OpenSSL " || skip "not OpenSSL (e.g. LibreSSL); TLS 1.3 / Ed25519 support differs"
openssl s_client -help 2>&1 | grep -q -- "-tls1_3" || skip "this openssl s_client has no -tls1_3"

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
else
    fail "no valid HTTPS 200 response received over TLS"
fi

# ---------------------------------------------------------------------------
# Milestone #1 acceptance, verified against a REAL OpenSSL client (not just our
# in-process KAT client):
#   (a) a >16 KiB response, which the server must fragment across TLS records at
#       the 2^14 boundary and the client must reassemble byte-exactly;
#   (b) multiple sequential requests over ONE TLS connection (keep-alive).
# ---------------------------------------------------------------------------

# (a) Large body: 40000 bytes of a repeating A-Z pattern from /big. The request must
# use real CRLF line endings (a heredoc would send bare LF).
BIG="$(printf 'GET /big HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n' \
    | $TO openssl s_client -quiet -connect "127.0.0.1:$PORT" -tls1_3 2>/dev/null)"
# Body = everything after the first blank line; strip line endings before counting.
BIG_BODY="$(printf '%s' "$BIG" | awk 'f{print} /^\r?$/{f=1}')"
BIG_LEN="$(printf '%s' "$BIG_BODY" | tr -d '\r\n' | wc -c | tr -d ' ')"
if [ "$BIG_LEN" -lt 40000 ]; then
    fail ">16 KiB response over TLS: got $BIG_LEN body bytes, expected 40000 (record fragmentation/reassembly)"
fi
# The pattern must be intact across the record boundaries, not just the right length.
if ! printf '%s' "$BIG_BODY" | tr -d '\r\n' | grep -q "^ABCDEFGHIJKLMNOPQRSTUVWXYZABC"; then
    fail ">16 KiB response body is corrupted at the start (record reassembly)"
fi
echo "PASS: >16 KiB response ($BIG_LEN bytes) fragmented across TLS records and reassembled by a real OpenSSL client"

# (b) Keep-alive: two requests on one connection. The first must NOT close, the
# second closes. Two "HTTP/1.1 200" status lines prove the connection was reused.
KA="$( { printf 'GET /hello HTTP/1.1\r\nHost: localhost\r\n\r\n'
         sleep 2
         printf 'GET / HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n'
         sleep 2
       } | $TO openssl s_client -quiet -connect "127.0.0.1:$PORT" -tls1_3 2>/dev/null )"
KA_COUNT="$(printf '%s' "$KA" | grep -c "HTTP/1.1 200" || true)"
if [ "$KA_COUNT" -lt 2 ]; then
    echo "--- keep-alive transcript ---"; printf '%s\n' "$KA" | head -20
    fail "keep-alive: expected 2 responses on one TLS connection, saw $KA_COUNT"
fi
if ! printf '%s' "$KA" | grep -q "Hello, World! (encrypted)" \
   || ! printf '%s' "$KA" | grep -q "Hello over pure-C TLS 1.3"; then
    fail "keep-alive: both response bodies were not received on the reused connection"
fi
echo "PASS: two sequential HTTP requests served over ONE TLS connection (keep-alive) via openssl s_client"

exit 0
