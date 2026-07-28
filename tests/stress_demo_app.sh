#!/usr/bin/env bash
#
# stress_demo_app.sh — end-to-end stress test against a REAL running server.
#
# WHY THIS EXISTS
#
# Every other suite tests units in-process. That is why a run of the demo app in
# a browser found three defects in ten minutes that 166 unit tests, 37 stress
# tests, Valgrind and ASan had all missed:
#
#   * /metrics status counters were always zero — metrics_record_status() was
#     public, unit-tested, and called by nothing (#136). The unit test passed
#     throughout because it called the function directly.
#   * /healthz reported time since the FIRST PROBE, not uptime, because its
#     start time is initialised by pthread_once on first request.
#   * http_response_send_text() appends Content-Type instead of replacing it,
#     so a handler setting text/html emits two Content-Type headers.
#
# All three are wiring defects: the unit works, the assembly does not, and no
# test spanned the gap. This one does — it starts the real binary, talks HTTP to
# it over a real socket, and asserts on what comes back.
#
# WHAT IT CHECKS
#   1. functional  — every endpoint answers correctly
#   2. validation  — boundaries, malformed bodies, unknown routes
#   3. adversarial — CRLF injection, traversal, oversized URL/body, junk HTTP
#   4. concurrency — N parallel clients, asserting ZERO lost requests
#   5. resources   — no FD leak, no unbounded RSS growth
#
# It asserts CORRECTNESS, never throughput. Request rate depends on the machine,
# and a perf number that fails on a slow CI runner gets muted, and then protects
# nothing. Load here exists to catch loss and leaks, not to measure speed.
#
# Usage: stress_demo_app.sh <path-to-demo_app-binary>
#   STRESS_CONCURRENCY=20  parallel clients per round (default 20)
#   STRESS_ROUNDS=10       rounds (default 10) -> 200 requests total
set -u

BIN="${1:?usage: stress_demo_app.sh <demo_app binary>}"
CONC="${STRESS_CONCURRENCY:-20}"
ROUNDS="${STRESS_ROUNDS:-10}"

SRV_PID=""
TMP=""
fails=0
checks=0

skip() { echo "SKIP: $*"; exit 0; }
cleanup() {
    [ -n "$SRV_PID" ] && kill "$SRV_PID" 2>/dev/null
    wait "$SRV_PID" 2>/dev/null
    [ -n "$TMP" ] && rm -rf "$TMP"
}
trap cleanup EXIT

ok()   { checks=$((checks+1)); printf '  ok    %s\n' "$1"; }
bad()  { checks=$((checks+1)); fails=$((fails+1)); printf '  FAIL  %s\n' "$1"; }
is()   { # is <label> <actual> <expected>
    if [ "$2" = "$3" ]; then ok "$1"; else bad "$1 — got '$2', expected '$3'"; fi
}

command -v curl >/dev/null 2>&1 || skip "curl not found"
[ -x "$BIN" ] || skip "demo_app binary '$BIN' not found"

TMP="$(mktemp -d)" || skip "mktemp failed"

# Start on a pid-derived port, retrying if taken.
PORT=$(( ($$ % 2000) + 41000 ))
started=0
for _try in 1 2 3 4 5; do
    "$BIN" "$PORT" >"$TMP/srv.log" 2>&1 &
    SRV_PID=$!
    for _i in $(seq 1 50); do
        curl -s -m 1 -o /dev/null "http://127.0.0.1:$PORT/api/info" 2>/dev/null && { started=1; break; }
        kill -0 "$SRV_PID" 2>/dev/null || break
        sleep 0.1
    done
    [ "$started" = 1 ] && break
    kill "$SRV_PID" 2>/dev/null; wait "$SRV_PID" 2>/dev/null; SRV_PID=""
    PORT=$((PORT + 1))
done
[ "$started" = 1 ] || { echo "FAIL: server did not become ready"; cat "$TMP/srv.log"; exit 1; }

B="http://127.0.0.1:$PORT"
code() { curl -s -m 5 -o /dev/null -w '%{http_code}' "$@"; }

echo "=== end-to-end stress: real server on port $PORT ==="
echo

echo "[1] functional"
is "GET /                     -> 200" "$(code "$B/")" "200"
is "GET /  Content-Type html"  "$(curl -s -m5 -o /dev/null -w '%{content_type}' "$B/")" "text/html; charset=utf-8"
# Exactly one Content-Type. Two is invalid per RFC 9110 and is what
# send_text-then-set_header produced before the ordering was corrected.
is "GET /  exactly 1 Content-Type" \
   "$(curl -s -m5 -D- -o /dev/null "$B/" | grep -ci '^content-type:' | tr -d ' ')" "1"
is "GET /api/info             -> 200" "$(code "$B/api/info")" "200"
is "GET /healthz              -> 200" "$(code "$B/healthz")" "200"
is "GET /metrics              -> 200" "$(code "$B/metrics")" "200"
is "GET /api/greet/:name body" \
   "$(curl -s -m5 "$B/api/greet/kamran")" '{"name":"kamran","greeting":"hello"}'
is "POST /api/echo round-trip" \
   "$(curl -s -m5 -X POST -d '{"message":"hi"}' "$B/api/echo")" \
   '{"message":"hi","received_bytes":16}'
echo

echo "[2] validation"
is "greet 64 chars (boundary, allowed)"  "$(code "$B/api/greet/$(printf 'a%.0s' $(seq 1 64))")" "200"
is "greet 65 chars (boundary, rejected)" "$(code "$B/api/greet/$(printf 'a%.0s' $(seq 1 65))")" "400"
is "echo with no body        -> 400"     "$(code -X POST "$B/api/echo")" "400"
is "echo with malformed JSON -> 400"     "$(code -X POST -d 'not-json' "$B/api/echo")" "400"
is "unknown path             -> 404"     "$(code "$B/no-such-route")" "404"
# RFC 9110 §9.3.2: HEAD behaves as GET without a body. Was 404 before the router
# learned to fall back to the GET route.
is "HEAD /                  -> 200"     "$(curl -s -m5 -I -o /dev/null -w '%{http_code}' "$B/")" "200"
is "HEAD / sends no body"                "$(curl -s -m5 -I "$B/" | awk 'f{c+=length($0)} /^\r?$/{f=1} END{print c+0}')" "0"
is "HEAD / keeps Content-Length"         "$(curl -s -m5 -I "$B/" | grep -ci '^content-length:' | tr -d ' ')" "1"
is "HEAD /nope stays        -> 404"     "$(curl -s -m5 -I -o /dev/null -w '%{http_code}' "$B/nope")" "404"
# Path params are percent-decoded before the handler sees them.
is "path param percent-decoded" \
   "$(curl -s -m5 "$B/api/greet/hello%20world")" '{"name":"hello world","greeting":"hello"}'
# An encoded NUL would truncate the C string after validation ran on the full
# length, so it is refused rather than decoded.
is "encoded NUL refused    -> 400"      "$(code "$B/api/greet/a%00b")" "400"
echo

echo "[3] adversarial input"
# The response must not carry a header the attacker put in the URL. This is the
# header-injection guard in header_list_add(); if it ever regresses, an attacker
# controls response headers.
is "CRLF in path injects no header" \
   "$(curl -s -m5 -D- -o /dev/null "$B/api/greet/x%0d%0aX-Injected:%20evil" | grep -ci 'x-injected' | tr -d ' ')" "0"
is "path traversal          -> 404"  "$(code "$B/../../etc/passwd")" "404"
is "8KB URL                 -> 414"  "$(code "$B/api/greet/$(printf 'a%.0s' $(seq 1 8000))")" "414"
is "body over MAX_BODY_BYTES-> 413" \
   "$(python3 -c 'import sys;sys.stdout.write("{\"m\":\""+"x"*1200000+"\"}")' 2>/dev/null | code -X POST --data-binary @- "$B/api/echo")" "413"
for junk in 'GARBAGE\r\n\r\n' 'GET\r\n\r\n' 'GET / HTTP/9.9\r\n\r\n'; do
    if command -v nc >/dev/null 2>&1; then
        line="$(printf "$junk" | timeout 3 nc 127.0.0.1 "$PORT" 2>/dev/null | head -1 | tr -d '\r')"
        case "$line" in
            "HTTP/1.1 400"*) ok "malformed request rejected with 400" ;;
            *)               bad "malformed request: got '${line:-<none>}', expected 400" ;;
        esac
    fi
done
# Surviving junk matters more than the status code it returns.
kill -0 "$SRV_PID" 2>/dev/null && ok "server alive after malformed input" \
                               || bad "server DIED on malformed input"
echo

# NOTE: a bare `wait` would block forever here. The server is itself a
# background job of this shell, so `wait` with no arguments waits for it too and
# it never exits. Collect the client PIDs and wait only on those.
load_round() {
    local pids=""
    local _c
    for _c in $(seq 1 "$CONC"); do
        curl -s -m 10 -o /dev/null "$B/api/info" &
        pids="$pids $!"
    done
    # shellcheck disable=SC2086
    wait $pids 2>/dev/null
}

echo "[4] concurrency — $((CONC * ROUNDS)) requests, $CONC at a time"
m2xx() { curl -s -m5 "$B/metrics" \
         | sed -n 's/.*"2xx":\([0-9][0-9]*\).*/\1/p'; }
before="$(m2xx)"
for _r in $(seq 1 "$ROUNDS"); do
    load_round
done
sleep 0.5
after="$(m2xx)"
# +1 for the /metrics read that produced $after.
expected=$(( CONC * ROUNDS + 1 ))
delta=$(( after - before ))
if [ "$delta" -eq "$expected" ]; then
    ok "every request counted ($delta) — no loss under load"
else
    bad "request loss under load: counted $delta, expected $expected"
fi
kill -0 "$SRV_PID" 2>/dev/null && ok "server alive after load" \
                               || bad "server DIED under load"
is "still serving after load -> 200" "$(code "$B/")" "200"
echo

echo "[5] resources"
# A per-request FD or memory leak shows up as unbounded growth. Generous
# thresholds: this catches a leak, not normal allocator behaviour.
fds() { lsof -p "$SRV_PID" 2>/dev/null | wc -l | tr -d ' '; }
rssk() { ps -o rss= -p "$SRV_PID" 2>/dev/null | tr -d ' '; }
fd_before="$(fds)"; rss_before="$(rssk)"
for _r in $(seq 1 "$ROUNDS"); do
    load_round
done
sleep 1
fd_after="$(fds)"; rss_after="$(rssk)"
if [ -n "$fd_before" ] && [ -n "$fd_after" ] && [ "$fd_before" -gt 0 ] 2>/dev/null; then
    if [ "$fd_after" -le $(( fd_before + 16 )) ]; then
        ok "no FD leak (${fd_before} -> ${fd_after})"
    else
        bad "FD leak: ${fd_before} -> ${fd_after} after $((CONC*ROUNDS)) more requests"
    fi
fi
if [ -n "$rss_before" ] && [ -n "$rss_after" ] && [ "$rss_before" -gt 0 ] 2>/dev/null; then
    if [ "$rss_after" -le $(( rss_before * 2 + 4096 )) ]; then
        ok "no runaway RSS (${rss_before}K -> ${rss_after}K)"
    else
        bad "RSS grew ${rss_before}K -> ${rss_after}K after $((CONC*ROUNDS)) more requests"
    fi
fi
echo

# ---------------------------------------------------------------------------
# Known defects, reported but NOT fatal.
#
# These are real and filed. They are listed rather than asserted so the suite
# does not go red on a bug we have already agreed to fix — but they are printed
# on every run so they cannot be quietly forgotten. Turn each into a real `is`
# check in the block above as its fix lands, and delete it from here.
# ---------------------------------------------------------------------------
# The /healthz uptime defect needed a throwaway server to observe: probing the
# main server early masks it. Now that health_check_register() stamps the start
# time at wiring time, the same probe asserts the fix instead of reporting it.
echo "[6] /healthz reports real uptime, not time-since-first-probe"
HZPORT=$((PORT + 500))
"$BIN" "$HZPORT" >"$TMP/hz.log" 2>&1 &
HZ_PID=$!
for _i in $(seq 1 50); do
    curl -s -m 1 -o /dev/null "http://127.0.0.1:$HZPORT/api/info" 2>/dev/null && break
    kill -0 "$HZ_PID" 2>/dev/null || break
    sleep 0.1
done
sleep 3                                   # accrue real uptime, /healthz untouched
hz="$(curl -s -m5 "http://127.0.0.1:$HZPORT/healthz" | sed -n 's/.*"uptime_seconds":\([0-9][0-9]*\).*/\1/p')"
inf="$(curl -s -m5 "http://127.0.0.1:$HZPORT/api/info" | sed -n 's/.*"uptime_seconds":\([0-9][0-9]*\).*/\1/p')"
kill "$HZ_PID" 2>/dev/null; wait "$HZ_PID" 2>/dev/null
if [ -z "$hz" ] || [ -z "$inf" ]; then
    bad "could not read uptime from the throwaway server"
elif [ "$hz" -ge $(( inf - 1 )) ] 2>/dev/null; then
    ok "/healthz uptime=${hz}s tracks real uptime=${inf}s"
else
    bad "/healthz uptime=${hz}s but real uptime=${inf}s — counting from first probe again"
fi
echo

echo "=== $((checks - fails))/$checks checks passed ==="
if [ "$fails" -gt 0 ]; then
    echo "--- server log ---" >&2
    cat "$TMP/srv.log" >&2
    exit 1
fi
exit 0
