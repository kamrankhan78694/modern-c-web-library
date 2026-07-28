# Changelog

All notable changes to the Modern C Web Library will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

_Nothing yet._

## [2.1.0] - 2026-07-29

A dev server drove this release: running the library in a browser surfaced wiring
defects that 166 unit tests, Valgrind and ASan had all missed, and fixing them
produced a post-handler router phase, an end-to-end test suite, four bug fixes
with regression coverage, and five new public APIs. Additive only — no breaking
changes; the one observable behaviour change is `send_text` replacing (not
appending) `Content-Type`, which no correct caller could have relied on.

### Added

- **`http_response_send_html()`** — sends with `Content-Type: text/html;
  charset=utf-8`. Serving HTML previously meant `send_text` followed by
  correcting the header, which was exactly the sequence that triggered BUG-11
  (see Fixed). `http_response_send_template()` now sends `text/html` too —
  rendered templates are HTML, and callers had to correct that header as well.
- **`http_request_clear_params()`** — frees a request's route parameters
  (BUG-12). The HTTP server does this as part of the request lifecycle; code
  driving `router_route()` directly — a Cloudflare Worker handler, an embedder,
  a test — leaked three allocations per parameterised request with no public
  way to free them. The unit tests deleted their private mirror of the internal
  param-node layout and call the real API.
- **`http_server_port()`, and `http_server_listen(server, 0)` binds an
  ephemeral port** — the kernel picks a free port and the accessor reports it.
  Exists for BUG-14 (see Fixed), and for any embedder that wants
  parallel-safe listening.

- **`router_add_response_hook()` — a post-handler phase for the router.**
  Middleware runs *before* the handler, so nothing in the request pipeline could
  observe the status code. Anything needing the outcome of a request — status
  metrics, access logs carrying the status, timing, tracing — had nowhere to
  live. Hooks run after the handler, after the built-in 404, and when a
  middleware short-circuits having sent a response; they do not run when
  `router_route()` rejects its arguments, because no response exists to describe.
- **`tests/stress_demo_app.sh`, registered as the `StressDemoApp` ctest suite** —
  the first suite that exercises the *assembled* system. It starts the real
  server binary and talks HTTP to it over a socket: every endpoint, validation
  boundaries, adversarial input (CRLF injection, traversal, oversized URL and
  body, malformed request lines), a concurrency phase asserting **zero lost
  requests**, and FD/RSS growth checks. It asserts correctness, never
  throughput — a perf number that fails on a slow runner gets muted, and then
  protects nothing.

  This exists because a browser session found three defects in ten minutes that
  166 unit tests, 37 stress tests, Valgrind and ASan had all missed. Every one
  was a wiring defect: the unit worked, the assembly did not, and nothing
  spanned the gap. Verified it catches regressions by reintroducing the
  duplicate-`Content-Type` bug — the suite reports `got '2', expected '1'` and
  fails.

  The `/healthz` check spins up a throwaway server rather than probing the main
  one, because that endpoint's bug was precisely that its clock started at the
  first probe: any earlier check in the suite would have started it, and the
  later assertion would then have reported the bug as fixed. A dedicated server
  left untouched for three seconds is the only way to observe it.

- **`examples/demo_app` + `tools/dev-server.sh` + `.claude/launch.json`** — a
  minimal dev server whose page drives its own API from the browser, so opening
  it exercises HTML out, JSON out and JSON in against the real request path.
  Two library bugs surfaced within minutes of running it; see below.
- **`tools/check-consistency.sh`, run in CI as the `consistency` job.** Three
  times in one release cycle a fix corrected the instance a reviewer named and
  left the same claim wrong elsewhere — and twice the defect was introduced *by
  the change that was fixing an earlier instance of it*. The checks derive truth
  from the build files rather than from prose, so the class cannot recur
  silently: version declarations must agree across `CMakeLists.txt`,
  `include/kamran.k`, `Dockerfile.release` and `publish-package.sh` (every
  macro and every LABEL, not just the first of each), `src/` and `examples/`
  must contain no hardcoded `x.y.z` literal
  (`examples/simple_server.c` reported "1.0.0" for two major versions, and it is
  the release image's entrypoint), documented ctest suite counts must match
  `tests/CMakeLists.txt`, and the Valgrind step must still accumulate per-binary
  status and refuse to pass having checked nothing.

  Each check was verified by reintroducing the real bug and confirming it fails.
  The suite-count check produced a false positive on its first run — a correct
  "7 ctest suites" referring to the TLS subtotal — which is recorded in the
  script, because a checker that flags correct text gets disabled and then
  protects nothing.

  `.github/copilot-instructions.md` gains a section naming both failure modes
  (fixing the instance instead of the class; writing a gate that passes without
  testing anything) with the concrete cases, so the lesson reaches the next
  agent rather than living in one session's memory.

### Fixed

- **`http_response_send_text()` appended `Content-Type` instead of replacing it
  (BUG-11).** A handler that sent text and then set its own content type
  emitted two `Content-Type` headers — invalid per RFC 9110, and resolved by
  browsers as plain text. It now replaces. The demo app dropped its
  set-header-after-send workaround; `src/health_check.c` keeps the same
  ordering with a comment that now describes why it is merely conventional
  rather than load-bearing. The regression test asserts exactly one
  `Content-Type` and fails against the appending behaviour.
- **`http_response_send_compressed()`'s uncompressed path ignored the caller's
  `content_type`.** The compressed path set it; the fallback sent everything
  as `text/plain` — so whether a response was labelled `text/html` depended on
  whether it happened to compress. Found while fixing BUG-11: same header, same
  function family, one path honouring the argument and one not. Both fallback
  sites now set it.
- **The accept thread could run `accept()` on a recycled descriptor during
  shutdown (BUG-13).** `http_server_stop()` closed `socket_fd` from the
  caller's thread to unblock `accept()`; between that `close()` and the accept
  loop's next iteration, another thread's `open()` could be handed the same fd
  number, and `accept()` would run on an unrelated file. The accept thread now
  polls the listen socket alongside a wake pipe (the pattern the keep-alive
  dispatcher already used): the stopper writes a byte and joins **before**
  anything is closed. The listen socket is non-blocking so a connection reset
  between `poll()` and `accept()` cannot strand the thread where the pipe
  never reaches; accepted sockets are restored to blocking, because BSD-family
  kernels inherit the flag and the threaded request path relies on
  `SO_RCVTIMEO`. `http_server_shutdown()` had the same race and got the same
  reordering — which also ends its habit of spinning `perror("accept failed")`
  in a tight loop for the whole drain window.
- **The stress suite failed ~1 run in 3 when re-run back to back (BUG-14).**
  Fifteen hardcoded ports (19000–19016) accumulated `TIME_WAIT` sockets across
  runs until a bind failed, reporting a product failure for an environment
  condition. Every site now binds port 0 and reads the assigned port back via
  `http_server_port()` — no shared namespace, no collision to make rarer.
  Verified with three consecutive full-suite runs.
- **`/healthz` reported time since the first probe, not uptime.** Its start
  time was initialised by `pthread_once` on the first handler call, so a server
  up for an hour but never probed answered `0`, then counted from that probe.
  For an endpoint whose stated purpose is load-balancer and Kubernetes probes
  this is worse than an obvious break: once probes arrive on a schedule the
  number grows plausibly, so nothing looks wrong. `health_check_register()` now
  stamps the start time at wiring time; the `pthread_once` stays as a fallback
  so calling the handler directly still yields a sane value.
- **Path parameters are now percent-decoded.** `/api/greet/hello%20world` gave
  handlers `hello%20world`. Every handler had to decode by hand, and length and
  charset validation ran against the encoded form. Decoding happens *after*
  route matching, on the extracted value only, so a `%2F` can never become a
  separator the router already acted on. Malformed escapes are left verbatim
  rather than guessed at, `+` stays literal (it means space in a query string,
  not a path), and an encoded NUL is **refused** rather than decoded — it would
  truncate the C string after validation had accepted the full length.
- **`HEAD` now behaves as `GET` without a body** (RFC 9110 §9.3.2). `HEAD /`
  returned 404 on a path where `GET /` returned 200. The router falls back to
  the GET route when no explicit HEAD route exists, and the send path drops the
  body while keeping `Content-Length`, so the response describes the resource
  exactly as GET would.
- **`/metrics` status-class counters were always zero (#136).** `2xx`/`3xx`/
  `4xx`/`5xx` reported 0 no matter how much traffic was served, because
  `metrics_record_status()` — public, and unit-tested in isolation — was never
  called by anything in the library. The metrics middleware could not call it:
  middleware runs before the handler and never sees the status. `metrics_register()`
  now installs a response hook, so the counters work with no application changes.

  The existing unit test called `metrics_record_status()` directly and passed
  throughout, which is exactly how this shipped: a unit that works, a wiring that
  does not, and no test spanning the gap. The new integration test drives a real
  route through `router_route()` and asserts the counter moved — verified to
  fail when the hook registration is removed.
- **`/metrics` no longer contradicts itself.** `total_requests` and the
  per-method counts were incremented on the way in, the status classes on the
  way out, so the two halves of every scrape described different sets of
  requests: `2xx + 3xx + 4xx + 5xx` never equalled `total_requests`, and the gap
  was permanent rather than transient because the `/metrics` request itself was
  counted in the total before its own status could be recorded. All counting now
  happens once per completed request, under one lock, in the response hook. The
  new test asserts that identity — see the `1xx`/`other` entry below for its
  final form — and fails against the previous code.
- **`metrics_register()` works on its own.** The counter state was allocated
  only by `metrics_middleware_create()`, so registering the endpoint without
  also installing the middleware served a `/metrics` full of zeroes. It now
  allocates the state if nothing else has. `metrics_middleware_destroy()`
  releases it either way.
- **The stress suite reported phantom failures on macOS.** Its malformed-request
  probes ran `timeout 3 nc`, but `timeout` is GNU coreutils and is absent from a
  stock macOS runner — so the command was never found, the reply was empty, and
  three checks failed against a server that was in fact answering `400`
  correctly. The deadline now rides on `nc -w`, which both BSD and GNU `nc`
  support, and a missing `nc` announces itself as a skip instead of silently
  contributing zero checks. Verified by running the suite with `timeout` removed
  from `PATH`: 35/35.

- **`HEAD` sent a body in async mode.** The RFC 9110 §9.3.2 fix landed only in
  the threaded `send_response()`; the event-loop writer has its own send loop and
  kept putting `Content-Length` bytes on the wire after a HEAD response's headers.
  This is **not** new in this release, and an earlier draft of this entry wrongly
  said it was. At v2.0.1 the async writer had no body suppression at all, so in
  async mode `HEAD /anything` already returned 404 headers followed by the 9-byte
  `Not Found` body, and an application with an explicit `HTTP_HEAD` route got the
  full handler body. The GET fallback widened the exposure to every path rather
  than creating it. It is worse than cosmetic either way: on a keep-alive
  connection the client reads those bytes as the start of the next response,
  which is a response-queue desync — the request-smuggling family. Reproduced against the shipped `async_server` example, and the
  regression test pipelines `HEAD` then `GET` on one connection and asserts two
  cleanly framed responses.
- **`/metrics` broke its own identity on the first WebSocket upgrade.**
  `total_requests` counted every request while only `2xx`–`5xx` had a bucket, so
  a `101` was counted once and classified nowhere; the gap then grew for the life
  of the process. Added `1xx` and `other` classes so every request lands
  somewhere, making the identity exact:
  `total_requests == 1xx + 2xx + 3xx + 4xx + 5xx + other`.
- **Middleware-only metrics wiring reported all zeroes.** Consolidating the
  counting into the response hook emptied the middleware, which silently broke
  applications that install it without calling `metrics_register()` — while the
  header still promised existing wiring kept working. The middleware now counts
  request entry whenever no hook is installed, so that promise is true again.
- **`metrics_record_status()` documented a contract that now double-counts.**
  The header and API reference still said "call after sending a response", which
  with the hook installed added a second increment to every status class. It is
  now a no-op once the hook is registered, and documented as being for responses
  served outside the router.
- **The `StressDemoApp` suite reported a pass when it asserted nothing.** `skip()`
  exited 0, so a missing prerequisite — `curl` absent from the CI image, the
  binary not where CMake said, `mktemp` failing — turned the repo's only
  end-to-end suite into a green tick. It now exits 77 with `SKIP_RETURN_CODE`
  set, and ctest reports `Skipped`.
- **The `#138` keep-alive gate could not fail.** It asserted only that `ab`
  exited, but `ab` exits 0 even when every response failed — measured here:
  40 requests, `Non-2xx responses: 40`, exit status 0. It now asserts the
  completed count, zero failed requests and zero non-2xx, and reports
  throughput without asserting on it.

- **An undecodable path parameter now fails the request.** `%00` was refused by
  the decoder, but the caller then simply did not set the parameter and ran the
  handler anyway — so an attacker could make a declared `:param` vanish, and a
  handler that assumed it was present would dereference NULL or fall back to a
  default. Both the header and this changelog already called that "refused",
  which it was not. `router_route()` now returns a 400 and does not run the
  handler. The end-to-end suite could not have caught this: `demo_app` validates
  the parameter itself, so its check passed while the library did nothing.
- **Registering the same response hook twice double-counted everything.** Hooks
  run once per response and exist to have side effects, so a duplicate entry
  doubles whatever the hook records — `metrics_register()` called twice on one
  router reported 6 requests for 3, with both calls returning success.
  `router_add_response_hook()` now absorbs an exact `(hook, user_data)` duplicate.
- **Three more end-to-end checks that could not fail.** The path-traversal check
  never sent a traversal, because curl removes dot-segments client-side and the
  server only ever saw `GET /etc/passwd` (it now also sends the raw form with
  `--path-as-is`). The CRLF-injection check was a tautology: nothing in the demo
  routed a path parameter into a header, so the grep could not have found an
  injected one whatever the guard did — `demo_app` now echoes the decoded `:name`
  into `X-Greeted-Name` deliberately, giving the check a real path to defeat. And
  the FD-leak and RSS checks were wrapped in guards that silently contributed
  zero checks when `lsof`/`ps` were missing; they now announce a skip.

- **A second router silenced the first's metrics.** The "has the other half
  already counted this request?" decision was one process-global flag, but
  middleware and hooks belong to a router. So a router carrying only the metrics
  middleware stopped counting the moment any *other* router in the process called
  `metrics_register()`, and its traffic was counted nowhere — while the header
  promised middleware-only wiring kept working. Measured: six requests through
  such a router, `total_requests: 0`. The question is per-router and is now asked
  per-router, via `router_has_middleware()`.
- **Three checks in the HEAD tests could not fail.** `HEAD / sends no body`
  counted bytes in `curl -I` output, and curl never writes a HEAD response's body
  to stdout — measured 0 for a resource whose body is 3232 bytes, so the check
  read 0 whatever the server sent. It now reads the response off a socket, and
  catches 3233 bytes when body suppression is disabled. In the async test, the
  `Content-Length` assertion searched the whole buffer, where the pipelined GET
  response always supplied a match, and nothing asserted the GET body was
  *present* — so the inverse desync, headers advertising a length with no body
  behind them, passed silently. Both assertions are now scoped to one response,
  and the test is verified to fail in both directions.

### Changed

- **CI's "Production Docker Image" job now builds the image that is actually
  released.** It built `./Dockerfile`, while the published image comes from
  `./Dockerfile.release` — so a green check proved nothing about the release
  artifact, and the first build of `Dockerfile.release` happened inside the
  publish workflow, *after* the tag was pushed. The two differ materially
  (`-DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=OFF`, the `websocket_echo_server`
  entrypoint, a non-root user, the version LABELs). The verify step now also
  checks `websocket_echo_server`, which the old one never looked for, and a new
  step asserts the image's `org.opencontainers.image.version` LABEL equals the
  version in `CMakeLists.txt`. A comment claiming the job was path-filtered to
  Dockerfile/source changes was removed: no `paths:` filter or `if:` ever
  implemented it.
- **`tools/check-consistency.sh` checks present-tense version claims in
  markdown (check [6]).** Check [1] binds the four *build* files to
  `CMakeLists.txt`, and the script's own header cites `"currently 2.0.0" in
  DOCKER_PACKAGE.md` as a motivating failure — yet that file's version **badge**
  then shipped `2.0.0` through both 2.0.1 and 2.1.0, because nothing ever read
  markdown. The instance named in review was fixed; the class was not. The new
  check caught six more stale claims a manual sweep for this release had missed,
  including `**Version 2.0.1**` banners in `docs/api/README.md` and all three
  tutorials. It matches only forms that can only mean "this is current" (a
  shields.io version badge, a release-tag badge link, `currently X.Y.Z`, a
  `**Version X.Y.Z**` banner), and strips backticked and double-quoted spans
  first — its first run flagged a *correct* line that quotes the historical bad
  strings while explaining this exact failure, and a checker that cries wolf
  gets disabled. Verified to fail on a reintroduced stale badge.
- **The examples wire metrics with `metrics_register()` alone.** Adding the
  middleware as well is supported and does not double-count, but it counts the
  total on the way *in*, so a `/metrics` scrape includes itself in
  `total_requests` while its own status lands after the JSON is rendered. The
  hook alone counts everything once, at completion, and the identity is exact.
- **`tools/check-consistency.sh` cross-checks documented unit-test counts.** The
  suite-count check said nothing about the number of tests inside `WebLibTests`,
  so that figure sat at 166 in four files while the binary reported 172 and every
  check still passed. The true value needs a build, which this script does not
  do — but disagreement *between* documents needs no build, and every drift so
  far has taken exactly that form. Its first implementation reported success
  having compared nothing (`printf '%s'` leaves no trailing newline, so `wc -l`
  returned 0 for a single value); it is verified in both directions now.
- **The end-to-end suite asserts a floor on how many checks ran.** Guarded checks
  that quietly evaluate false subtract an assertion while the total still prints
  green — 36/36 and 38/38 look equally healthy. The count is now itself an
  assertion.
- **`examples/demo_app` states its real exposure.** It printed a `localhost` URL
  while `http_server_listen()` binds `INADDR_ANY`, so the demo — unauthenticated,
  with `Access-Control-Allow-Origin: *` — was reachable from the network.
  Verified: it answered on the machine's LAN address. The banner now says so.
- **`tools/check-consistency.sh` reads more phrasings, and reads whole
  sentences.** It only matched "N ctest suites" and `**N suites**`, so the same
  stale claim written "N test suites" or "N/N suites" drifted undetected — one
  such claim was stale in `CONTRIBUTING.md`. It also now takes every count on a
  line and accepts the union of the configurations that line names, because
  sentences legitimately state two ("13 suites rather than 14"), and flagging
  those correct lines is how a checker gets switched off. Widening it immediately
  found four genuinely stale counts.
- **`tools/dev-server.sh` builds into `build-devserver/`, not `build/`.** It
  configures with its own build type and flags, so sharing the conventional
  directory meant either adopting a contributor's cache and silently ignoring
  those settings, or creating one they did not ask for. Starting the preview can
  no longer disturb an existing build tree.

## [2.0.1] - 2026-07-28

A maintenance release. No library API change — the fixes are in CI, the test
suite and the examples. Notably, this is the first release in which the Valgrind
leak gate actually gates.

### Added

- **The benchmarking suite now produces numbers.** `src/benchmark.c` has shipped
  since v0.9.0 with nothing calling it — no example, no test, no CI job — so the
  advertised "Benchmarking Suite" had never yielded a figure and no baseline
  existed to detect a regression against. `examples/benchmark_server` starts a
  server, drives `benchmark_run()` against a plain-text and a JSON route, and
  prints throughput plus p50/p95/p99. `tests/benchmark_tls.sh` measures the TLS
  path with a real `openssl s_client`, which is the only option: the library is
  TLS **server-side only**, so there is no in-process TLS client to benchmark
  with. First baseline (Apple M5, loopback) recorded in
  [`docs/BENCHMARKS.md`](docs/BENCHMARKS.md), along with what the numbers
  explicitly do *not* claim — they are a single-client latency floor, not a
  capacity or concurrency measurement. Neither tool is a ctest suite: they are
  measurement tools, not gates, and an ungated performance number in CI is noise
  that also costs wall time.

### Fixed

- **`examples/worker_example.c` now defines the Worker lifecycle exports**
  (`worker_init`, `worker_fetch`, `worker_cleanup`), so the `emcc` command
  printed in `examples/worker.js` links as written — at 2.0.0 that command
  failed because the example had only `main()` (see the 2.0.0 notes below).
  The native demo is unchanged; `main()` now drives the same three functions
  the JS glue calls. Also fixed a 32-byte leak the example had carried from
  the start: the `worker_d1_exec()` DDL result was discarded, the same
  owned-return class PR #75 fixed in the D1 tests.
- **`examples/worker.js` header corrected**: it pointed at a `wrangler.toml`
  "in this directory" that has never shipped (you write your own), and it now
  states its real limitation outright — the glue accepts `env` but never
  passes it into WASM, so a deployed Worker sees the library's in-memory
  binding simulations, not the services bound in the Cloudflare dashboard.
- **CI wall time halved (~6.5 → ~3.0 min)**: the four secondary jobs no longer
  wait on `primary-checks` — the staging saved nothing on a public repository
  (Actions minutes are free) and doubled every run. All five jobs run in
  parallel; the concurrency group still cancels superseded runs.
- **The Valgrind CI step now genuinely gates**: it previously reported every
  binary but only the last one's exit status could fail the job, and it could
  pass having tested nothing (empty glob, failed `cd`). It now accumulates
  every binary's status, hard-fails if zero binaries matched, and prints how
  many it checked. The `cache_get()` leaks it had been hiding (25 test call
  sites) are fixed via helpers that make the ownership impossible to miss.

## [2.0.0] - 2026-07-27

Seventy-two pull requests landed between v1.0.0 and this release (#54, #55 and
#58–#129, excluding #64, which was closed without merging, and #122, which is an
issue rather than a pull request). Three things drive the major version: HTTPS can now be terminated
inside the library instead of requiring a reverse proxy in front of it, the
library runs in two new places (WebAssembly and Cloudflare Workers), and the
public header was renamed — which is the source-level break that makes this
2.0.0 rather than 1.1.0.

The bulk of the work, however, is security hardening: an internal bug audit and
the twenty-two pull requests that followed it, listed under *Security* below. If
you are running v1.0.0, read that section before deciding when to upgrade.

**Upgrading from 1.0.0 breaks source compatibility in three places** — the header
name, the session data API, and template auto-escaping. Each is described under
*Changed*.

### Added

- **Pure-C TLS 1.3 server — EXPERIMENTAL, UNAUDITED, OFF by default** (#96–#128).
  Hand-written HTTPS termination in 5,481 lines of C under `src/tls/`, with no
  OpenSSL, mbedTLS, or any other crypto library linked. Build it with
  `-DWEBLIB_ENABLE_TLS=ON`; with the option off (the default) no `src/tls/` code
  is compiled at all and the build is byte-identical to what it was before.
  - **Do not put this in front of anything you care about without an external
    cryptographic audit.** The primitives and — more importantly — the handshake
    state machine are hand-written and have not been independently reviewed.
  - New public API (`include/kamran.k`):
    `int http_server_enable_tls(http_server_t *server, const char *cert_pem, size_t cert_len, const char *key_pem, size_t key_len)`.
    It takes PEM **buffers with explicit lengths**, not file paths — see
    `examples/tls_server.c`, which reads the files itself and passes the buffers.
    Must be called before `http_server_listen()`.
  - Scope, stated exactly: **server-side only** (there is no TLS client);
    **threaded mode only** — the call returns -1 when async mode is enabled;
    **native builds only** — not available under WASM or Cloudflare Workers; and
    **WebSocket-over-TLS is refused** with 503 rather than silently downgraded to
    plaintext frames.
  - One profile, no agility: cipher suite `TLS_CHACHA20_POLY1305_SHA256`, key
    exchange X25519, signature Ed25519. No TLS 1.2, no AES-GCM, no RSA or ECDSA,
    no client certificates, no session resumption / tickets / PSK, no 0-RTT, no
    KeyUpdate. A client that cannot offer all three is refused with
    `handshake_failure`.
  - Crypto primitives, each with RFC known-answer tests: SHA-256, SHA-512,
    HMAC-SHA256, HKDF and HKDF-Expand-Label, ChaCha20, Poly1305,
    ChaCha20-Poly1305 AEAD, X25519, Ed25519 (#97–#105). SHA-256/HMAC-SHA256 and
    Base64 were promoted to a shared `src/crypto/` module so the TLS layer and
    the JWT/CSRF code use one implementation (#97, #107).
  - Certificate and key handling, all hardened against malformed input: a
    bounds-checked DER/ASN.1 reader, a PEM (RFC 7468) reader, and Ed25519 key
    extraction from PKCS#8 private keys and X.509 SPKI (#106, #108, #109).
  - Protocol: key schedule (RFC 8446 §7.1), record layer (§5) enforcing the 2^14
    plaintext limit with fragmentation, a bounded wire codec (§3), a ClientHello
    parser (§4.1.2), server handshake message builders, transcript / Finished /
    CertificateVerify auth crypto, and the full server handshake state machine
    (§4) including HelloRetryRequest (§4.1.4) and ALPN negotiation of `http/1.1`
    (RFC 7301) (#110–#116, #123, #124).
  - A sans-IO connection engine (`src/tls/tls_khannection.c`) plus a
    blocking-socket adapter (`src/tls/tls_transport.c`) with a read deadline and
    a bound on empty-record floods (#117, #118, #126).
  - **Interop, verified in CI:** a real `openssl s_client` completes a TLS 1.3
    handshake and an encrypted HTTP round-trip against the `tls_server` example,
    including a response larger than 16 KiB fragmented across records and two
    requests on one keep-alive connection (#120, #128). **Browser page load is
    not achieved and is not claimed** — the Ed25519-only certificate profile has
    limited and inconsistent browser support (#125).
  - Seven new ctest suites — `TlsTests`, `TlsCryptoTests`, `TlsParseTests`,
    `TlsTransportTests`, `TlsFuzzTests`, `TlsHttpTests`, `TlsInteropOpenssl` —
    including a deterministic robustness fuzzer over the untrusted-input path:
    ClientHello parsing, connection framing, record deprotection (#121).
  - `WEBLIB_TLS_TEST_HOOKS` (default OFF) gates a deterministic-RNG seam used
    only by `TlsHttpTests`. It must never be enabled in a production build.
  - `examples/tls_server.c`, a runnable HTTPS server (#120), and
    `src/tls/README.md`, which documents the security scope — what is and is not
    provided — in detail (added in #96, expanded most substantially in #125).
- **WebAssembly target via Emscripten** (#69) — a WASM-safe subset builds under
  `emcmake cmake`: JSON, router, template engine, input validation, cookies, body
  parsing, and compression. The WebSocket modules, the benchmarking module, the
  shared library, and TLS are excluded there. New exports for a JavaScript host —
  `wasm_weblib_version()`, `wasm_weblib_capabilities()`,
  `wasm_weblib_has_capability()`, and thin wrappers over the JSON, router,
  validation, and template APIs — plus `examples/wasm_example.c` and a `WasmTests`
  suite that runs on both native and WASM builds.
- **Cloudflare Workers runtime** (#70) — a fetch-event compatibility layer
  (`src/worker_runtime.c`) that bridges a Worker's request/response model to the
  library's own request/response types: `worker_request_*` / `worker_response_*` /
  `worker_env_*`, `worker_set_fetch_handler()`, `worker_set_router()`, and
  `worker_handle_fetch()`, which is exported for a JavaScript host to drive.
  Note `worker_set_router()` is accepted but **not used for dispatch** — with only a
  router set, `worker_handle_fetch()` returns a 200 placeholder without matching any
  route. Routing requires a fetch handler that branches on the URL itself.
  `examples/worker_example.c` exercises the layer natively.
  `examples/worker.js` sketches the JS side but is a **template, not a working
  deployment**. Three of the six C exports it names — `_worker_init`,
  `_worker_fetch`, `_worker_cleanup` — are not library functions; you write them
  in your own C file, as the Worker Quick Start in `README.md` shows. Two things
  about it are genuinely broken, though: its own build command compiles
  `examples/worker_example.c`, which defines none of the three (it has only
  `main()`), so that command fails as printed; and its header points at a
  `wrangler.toml` that does not ship. It also never passes `env` into WASM, so
  the KV/R2/D1/Queues bindings are unreachable from it. Reconciling the file with
  the Quick Start is tracked as follow-up work.
- **Cloudflare infrastructure bindings: KV, R2, D1, and Queues** (#71) — pure-C
  APIs shaped like the corresponding Workers bindings
  (`worker_kv_get/put/delete/list`, `worker_r2_get/head/put/delete/list`,
  `worker_d1_prepare/batch/exec` with `worker_d1_stmt_bind/run/first/all`,
  `worker_queue_send/send_text/send_json/send_batch/consume`,
  `worker_queue_message_ack`). **In every build — native, test, and WASM — these
  are backed by in-memory simulations**, not by Cloudflare's services: reaching
  the real bindings needs a JS glue layer, and none ships in this repo
  (`examples/worker.js` names no binding export at all and never passes `env`
  into WASM — see above). The simulations have
  fixed capacities (1024 KV entries, 1024 R2 objects, 1024 D1 rows, 4096 queued
  messages) — see `docs/WORKER_API.md`. `worker_d1_batch()` is **not** atomic,
  unlike Cloudflare's `env.DB.batch()`.
- **Semantic-versioning enforcement** (#72) — `WEBLIB_VERSION_MAJOR/MINOR/PATCH`
  and `WEBLIB_VERSION` macros, `WEBLIB_VERSION_ENCODE()` / `WEBLIB_VERSION_NUMBER`
  for compile-time comparisons, the runtime accessors `weblib_version()` and
  `weblib_version_components()`, and `_Static_assert`s that fail the build if the
  version in `kamran.k` drifts from `project(VERSION ...)` in `CMakeLists.txt`.
  A versioning policy was added to `CONTRIBUTING.md`.
- **Environment configuration and secret handling** (#61) — `env_config_get()`,
  `env_config_get_int/bool/port()`, `env_config_require()`, `env_config_is_set()`,
  and a secure-value wrapper (`env_config_get_secure()`, `env_secure_value_get()`,
  `env_secure_value_len()`, `env_secure_value_free()`) that wipes its buffer on
  free, plus `env_config_redact()` for log-safe rendering.
- **Security headers middleware** (#61) — `security_headers_middleware_create()`
  with safe defaults: `Content-Security-Policy: default-src 'self'`,
  `X-Frame-Options: DENY`, `Referrer-Policy: strict-origin-when-cross-origin`,
  a restrictive `Permissions-Policy`, and optional HSTS.
- **Shared security primitives** (#61, #63) — `secure_zero()` (a wipe the
  compiler may not optimise away), `secure_compare()` (constant-time), and
  `secure_random_bytes()` (OS CSPRNG), now used by every caller instead of
  per-module copies.
- **`http_server_set_request_timeout()` / `http_server_get_request_timeout()`**
  (#78) — a total wall-clock deadline for reading a complete request (default 60
  seconds; 0 disables). See *Security*.
- **`session_store_set_idle_timeout()`** (#87) — reclaims session-cookie
  sessions (`max_age == 0`) after an idle period (default 1800 seconds; ≤ 0
  disables reclamation).
- **A `tls-check` CI job** (#129) — a RelWithDebInfo TLS build running all 13
  suites, plus an ASan/UBSan build running the 7 TLS suites. Before this job,
  `ci.yml` never passed `-DWEBLIB_ENABLE_TLS=ON`, so **no `src/tls/` code was
  ever compiled or run in CI** — roughly 5,500 lines of unaudited crypto and the
  handshake state machine had zero regression protection. It joins
  `primary-checks` (Docker: gcc build, full ctest, Valgrind), `clang-check`,
  `macos-check` (pull requests only), and `docker-image-check`.
- **Test suites, as ctest reports them:** a default build runs 6 suites
  (`WebLibTests`, `KamranHeaderTests`, `AsyncWebSocketTests`, `StressTests`,
  `WorkerTests`, `WasmTests`); a build configured with
  `-DWEBLIB_ENABLE_TLS=ON -DWEBLIB_TLS_TEST_HOOKS=ON` runs 13. Both configurations
  pass.
- **v2.0.0 roadmap and planning documents** (#54, #55) — a 10-phase plan
  (Phases 11–20) in `NEXT_PHASE.md`, with `TODO.md` cross-references for all
  planned features. Its version numbering has since diverged from what actually
  happened: the TLS phases shipped here in 2.0.0 rather than in v1.1.0/v1.2.0,
  and **Phases 13–20 are not implemented** — there is no HTTP/2, no storage
  engine, no multi-process mode, no Windows IOCP port, no HTTP/3, and no
  CLI/metrics-exporter tooling.
  - Phase 11 (v1.1.0): TLS Foundation — crypto primitives (SHA-256, AES-GCM, ChaCha20, X25519, HKDF) — **shipped in 2.0.0, with one deliberate change: AES-GCM was dropped. The only cipher suite is `TLS_CHACHA20_POLY1305_SHA256`; Poly1305, SHA-512 and Ed25519 shipped in addition to the list above.**
  - Phase 12 (v1.2.0): TLS 1.3 Handshake & HTTPS — record layer, handshake, certificates, ALPN — **shipped in 2.0.0, but EXPERIMENTAL and UNAUDITED: server-side only, one cipher suite / group / signature algorithm, native builds only, off by default behind the `WEBLIB_ENABLE_TLS` CMake option. Not for production use without an external cryptographic audit — see `src/tls/README.md`.**
  - Phase 13 (v1.3.0): HTTP/2 Protocol — binary framing, HPACK, stream multiplexing, server push
  - Phase 14 (v1.4.0): Persistent Storage Engine — B-tree, WAL, transactions, iterator API
  - Phase 15 (v1.5.0): Advanced Middleware — directory listing, SSE, content negotiation, route groups
  - Phase 16 (v1.6.0): Multi-Process Architecture — fork-based workers, zero-downtime reload
  - Phase 17 (v1.7.0): Cross-Platform Hardening — Windows IOCP, BSD support, platform abstraction
  - Phase 18 (v1.8.0): Developer Experience — config parser, plugins, advanced templates, debug mode
  - Phase 19 (v1.9.0): HTTP/3 & QUIC — UDP transport, QUIC protocol, connection migration
  - Phase 20 (v2.0.0): Release Engineering — CLI tools, Prometheus, OpenTelemetry, fuzz testing
- Updated `NEXT_PHASE.md` with complete v2.0.0 roadmap (first-principles design, adversarial review, atomic task breakdown, security threat model, QA pipeline)
- Updated `TODO.md` with phase cross-references for all planned features
- `BUGS.md` and a bug-pattern section in `CONTRIBUTING.md` (#62) — the defect
  classes found in review, written down so they stop recurring.
- A Mermaid architecture flowchart (#67) and a PNG banner (#60) in `README.md`;
  a Windows-IOCP roadmap note in `docs/README.md` (#58). The DOI badge and the
  post-1.0.0 documentation refresh (including `docs/README.md` itself) landed as direct
  commits on `main` after the v1.0.0 tag, not via a pull request.

### Changed

- **BREAKING — the public header is now `kamran.k`** (#65). `include/weblib.h`
  no longer exists and there is no compatibility shim: every translation unit
  that did `#include "weblib.h"` must now `#include "kamran.k"`. This is the
  change that makes this release 2.0.0. (`include/db_pool.h` is unaffected.)
- **BREAKING — session data access is keyed on `(store, session_id)`** (#83).
  `session_set_data()`, `session_get_data()` and `session_remove_data()` now take
  `(session_store_t *store, const char *session_id, ...)` instead of a
  `session_t *` handle, `session_set_data()` and `session_remove_data()` return
  `int`, and `session_get_data()` returns a freshly allocated copy the caller must
  `free()` rather than a borrowed `const char *`. This eliminates a
  use-after-free class by construction — see *Security*.
- **BREAKING — template `{{ var }}` interpolation now HTML-escapes by default**
  (#77). Use `{{{ var }}}` where you genuinely intend raw HTML. Templates that
  relied on `{{ }}` to emit markup will render it escaped after upgrading. See
  *Security*.
- Request paths are canonicalized at ingest (#88) — runs of `/` are collapsed and
  a single trailing `/` is stripped (except for root `/`), once, right after the
  request line is parsed. The router, every middleware, and every handler now see
  one canonical `req->path`. Percent-encoded slashes are left alone (no path
  decoding happens), and `..` is still handled by the static-file middleware's
  `realpath()` check.
- `Host`-header enforcement is keyed on the request's HTTP version rather than on
  keep-alive state (#86), so an HTTP/1.1 request without a `Host` header is
  rejected regardless of whether the connection is being reused.
- Reads in threaded mode are bounded by a total request deadline as well as the
  per-recv socket timeout (#78); an expired deadline produces a 408 response.
- The README architecture flowchart was corrected to match the implemented
  server, concurrency, and compression behaviour (#68).

### Fixed

- CMake `FetchContent` / `add_subdirectory` consumption (#59) — the build used
  `CMAKE_SOURCE_DIR` for include paths, which only resolved correctly when this
  project was the top-level one. Replaced with `target_include_directories`.
- JSON numbers serialize losslessly (#79) — `%g` truncated values to six
  significant digits. Output now uses the shortest representation that reparses
  to the same double, prints exact integers up to 2^53 as integers, preserves
  signed zero, and emits `null` for non-finite values instead of the invalid
  `nan`/`inf` that `%g` produced.
- JSON number parsing no longer depends on `LC_NUMERIC` (#93) — a locale whose
  decimal separator is not `.` (including multi-byte separators) used to corrupt
  parsed values.
- `Accept-Encoding` q-values are parsed without `strtod` (#95), so content
  negotiation is likewise locale-independent.
- D1 SQL handling in the Workers layer (#81) — `DELETE`/`SELECT` with a `WHERE`
  clause the in-memory engine did not recognise fell through to matching *every*
  row; positional `INSERT` bound parameters to the wrong columns; a
  parameter/column count mismatch is now rejected.
- `/healthz` emitted two conflicting `Content-Type` headers (#91).
- A failed `listen()` left the server half-started and unjoinable (#82); the
  failure path now cleans up, and the accept-thread start flag is
  `volatile sig_atomic_t` so it is safe to read from a signal handler.
- Valgrind leaks in the D1 worker tests (#75) — discarded `CREATE TABLE` results
  were never freed, which kept the memcheck gate red.
- 36 assertions were being compiled out of existence in CI (#129) —
  `test_kamran_header.c` (17) and `test_async_websocket.c` (19) used bare
  `assert()`, which `NDEBUG` removes, and CMake defines `NDEBUG` for
  `RelWithDebInfo`, the configuration CI builds. Both suites had been passing
  unconditionally; they now use an always-evaluated `CHECK()` macro, verified by
  a negative control.
- Restored a zero-warning build (#74), including an off-by-one in an HTML buffer
  size calculation in the WebSocket echo example (#66).

### Security

An internal bug audit produced 45 fixes in a single pass (#73), and the
twenty-two pull requests after it (#74–#95) each closed one defect class, with a
regression test. The security-relevant ones are listed here; the rest were
correctness fixes and appear under *Fixed*.

- **Cross-site scripting through the template engine** (#77) — `{{ }}`
  interpolation emitted variable values verbatim, so any user-controlled value
  rendered into a page was an XSS vector. `{{ }}` now HTML-escapes by default and
  `{{{ }}}` is the explicit raw opt-out (`&`, `<`, `>`, `"`, `'` are replaced
  with entities). That covers HTML text and quoted attribute values; it is *not*
  sufficient for unquoted attributes, `javascript:`/`data:` URLs, or the bodies
  of `<script>` and `<style>` elements. Breaking behaviour change — see
  *Changed*.
- **HTTP request smuggling** (#66, #85) — request framing is now parsed strictly
  per RFC 7230: `Transfer-Encoding` is token-parsed instead of substring-matched,
  a `Content-Length` + `Transfer-Encoding` conflict is detected regardless of
  header order, duplicate `Content-Length` is rejected, control bytes and
  whitespace before the colon in a header name are rejected, OWS handling is
  RFC-exact, and chunk sizes and `Content-Length` values are parsed strictly as
  numbers. The request target is validated for control bytes and whitespace.
- **Response header injection** (#66) — header values containing CR or LF are
  rejected at `header_list_add()`, so a value carrying `\r\n` can no longer
  forge additional response headers or a response body.
- **Slow loris: no total request deadline** (#78) — a client could hold a worker
  thread indefinitely by dripping a request slowly enough to keep resetting the
  per-recv socket timeout. Threaded mode now enforces a total wall-clock deadline
  for reading a complete request (default 60s, monotonic clock,
  `http_server_set_request_timeout()`) and answers 408 when it expires.
- **Async connections with no idle reaper** (#89) — the async path tracked
  connections but never closed idle or stalled ones, so the same drip pattern
  exhausted the connection table. A self-re-arming reaper now sweeps every second
  and closes connections past their deadline. A shutdown-path leak on the same
  code was fixed alongside it.
- **CSPRNG fallbacks with predictable entropy** (#76) — session ID generation
  fell back to `rand_r()` seeded from time, clock, and an address when the CSPRNG
  was unavailable, which made session IDs guessable at exactly the moment
  entropy failed. That fallback is gone: `secure_random_bytes()` now draws from
  `getrandom(2)`, `arc4random_buf()`, `BCryptGenRandom`, or `/dev/urandom` and
  returns -1 if none is available, and `session_create()` refuses to issue a
  session rather than hand out a weak one. `secure_zero()` was also fixed — its
  `memset_s` branch was mis-guarded and silently absent on some toolchains.
- **JWT verification weaknesses** (#92, #94) — the `alg` header is now matched
  exactly rather than by prefix, `nbf` is enforced, the header/claim split
  requires exactly one separator, `exp`/`nbf` are parsed strictly as integers
  (rejecting a leading `+`) and as `long long` rather than `long`, and a new
  `require_exp` option rejects tokens that carry no expiry at all.
- **CORS wildcard origin combined with credentials** (#84, CWE-942) — a `*`
  allowed-origin together with `Access-Control-Allow-Credentials: true` is a spec
  violation, and reflecting the request origin instead would have made every site
  a trusted origin. The middleware now fails closed.
- **WebSocket memory exhaustion** (#80) — a frame's declared length was trusted
  when allocating. Frame and reassembled-message sizes are now capped (16 MiB
  default, overridable with `-DWS_MAX_MESSAGE_SIZE=...`); an oversized frame
  closes the connection and frees any in-progress fragment buffer.
- **Session use-after-free** (#83) — `session_get()` returned a `session_t *`
  that expiry or another thread could free while the caller still held it. Data
  access is now keyed on `(store, session_id)` and re-resolves the session under
  the lock, returning a copy, so the borrowed-pointer lifetime bug cannot be
  written. Breaking API change — see *Changed*.
- **Route aliasing via un-normalized paths** (#88) — the router's two matchers
  disagreed about non-canonical paths. `:param` routes tokenize on `/` and
  silently drop empty segments, while literal routes use `strcmp`, so
  `/users//123`, `/users/123/` and `//users//123//` all aliased onto
  `/users/:id`, yet a literal `/users/list/` did *not* match `/users/list`. A
  proxy, cache, or ACL keying on the raw path would therefore make a different
  decision than the origin. Paths are now canonicalized at the trust boundary —
  see *Changed*.
- **Session-cookie slot exhaustion** (#87) — sessions created with
  `max_age == 0` had no absolute expiry and were never reclaimed, so an attacker
  could fill the store and lock out new sessions. Idle sessions are now reclaimed
  after 1800 seconds by default, tunable via `session_store_set_idle_timeout()`,
  with the subtraction guarded against clock skew.
- **CRLF injection through `input_validate_email()`** (#90) — the validator
  checked structure but not bytes, so `user@example.com\r\nBcc: evil@x.com`
  passed as valid and became a header/log-injection primitive wherever the caller
  put it. Bytes below 0x20, `0x7F`, and space are now rejected anywhere in the
  address; high bytes are left alone so internationalized addresses still
  validate.
- **db_pool destroy-time use-after-free** (#74) — destroying a pool while a
  connection was still checked out freed memory another thread was using. The
  lifetime is now an atomic refcount, and double-release is rejected.
- **45 bugs from the internal audit** (#73) — critical: WebSocket protocol
  handling, SIGPIPE, and a use-after-free; JSON NUL injection and unpaired
  surrogates; session thread safety. Medium and low: cache use-after-free,
  session RNG, event loop, async connection limit, auth, parser, compression,
  template, metrics, logging, body parser, router, and portability defects. The
  review round on top of it added CSRF buffer safety, Basic-auth realm injection,
  RNG failure handling, chunk parsing, and KV cursor bounds.
- **Duplicated security primitives** (#62, #63) — several modules carried their
  own wipe and comparison helpers, so a fix in one did not reach the others. They
  now share `secure_zero()` and the constant-time `secure_compare()`, which also
  closed a timing side-channel in CSRF token comparison. Security-headers config
  strings are deep-copied rather than stored by pointer.

## [1.0.0] - 2026-02-22

### Added
- **Phase 10: Release Readiness (v1.0.0)**
- **REST API example** (`examples/rest_api_server.c`) — full CRUD operations with input validation, JSON responses, and production middleware (logging, CORS, rate limiting, error handling, health check, metrics)
- **Tutorial documentation** (`docs/tutorials/`) — step-by-step guides:
  - Getting Started tutorial
  - Building a REST API tutorial
  - Real-time WebSocket Applications tutorial
- **Complete API reference** (`docs/api/README.md`) — updated to v1.0.0 with all Phase 7-9 APIs
- Updated `TODO.md` — marked HTTP parser, header/parameter storage, and connection handling as complete
- Updated `docs/TECHNICAL_DEBT.md` — resolved stale entries for keep-alive (#6) and compression (#10)

### Changed
- Version bump from 0.9.0 to 1.0.0
- Updated all documentation to reflect v1.0.0 release status
- Updated `ACHIEVEMENTS.md` with current test counts (129 tests) and complete feature list
- Updated `NEXT_PHASE.md` — all phases marked as complete
- Updated `README.md` — comprehensive project structure, features list, and roadmap

## [0.9.0] - 2026-02-20

### Added
- **Phase 9: Performance & Observability**
- **In-Memory Cache** — LRU eviction, TTL support, thread-safe hash table (`src/cache.c`)
- **Metrics Middleware** — request counting, per-method tracking, status code ranges, JSON `/metrics` endpoint (`src/middleware_metrics.c`)
- **Response Compression** — pure C gzip (RFC 1952) with DEFLATE (RFC 1951), `Accept-Encoding` negotiation (`src/compression.c`)
- **Async WebSocket** — event loop integration, non-blocking I/O, write queue, connection manager (`src/async_websocket.c`)
- **Benchmarking Suite** — high-resolution timing, throughput/latency measurement, percentile statistics (`src/benchmark.c`)
- 20 new unit tests for Phase 9 features

## [0.8.0] - 2026-02-19

### Added
- **Phase 8: Security & Observability**
- **CSRF Middleware** — double-submit cookie pattern with constant-time comparison (`src/middleware_csrf.c`)
- **Logging Middleware** — configurable log levels (DEBUG/INFO/WARN/ERROR), timestamp format (`src/middleware_log.c`)
- **Error Handler Middleware** — centralized 4xx/5xx JSON error responses (`src/middleware_error.c`)
- **Input Validation** — length, charset, integer range, email format validation, HTML sanitization (`src/input_validation.c`)
- **Health Check Endpoint** — `GET /healthz` with JSON status and uptime (`src/health_check.c`)
- 12 new unit tests for Phase 8 features

## [0.7.0] - 2026-02-18

### Added
- **Phase 7: Server Hardening & CI**
- **Socket Timeouts** — `setsockopt(SO_RCVTIMEO/SO_SNDTIMEO)` with `http_server_set_timeout()` API
- **Thread Pool** — bounded thread pool replacing thread-per-connection model (`src/thread_pool.c`)
- **Graceful Shutdown** — server state machine (STOPPED → RUNNING → DRAINING → STOPPED), `http_server_shutdown()` API
- **GitHub Actions CI** — Linux (GCC + Clang) and macOS (Clang) matrix with Valgrind memcheck gate
- **Networking Integration Tests** — raw-socket HTTP client, GET/POST/404/malformed/concurrent tests
- **Parser Hardening** — duplicate Transfer-Encoding detection, `Expect: 100-continue` handling

## [0.6.0] - 2026-02-17

### Added
- **Phase 6: Production Readiness (v0.6.0)**
- **Session Management** (Phase 6.2) - Server-side session store
  - `session_store_create()` / `session_store_destroy()` - Session store lifecycle
  - `session_create()` - Create session with configurable max_age
  - `session_get()` - Retrieve session by ID with expiration check
  - `session_destroy()` - Remove session from store
  - `session_set_data()` / `session_get_data()` / `session_remove_data()` - Key-value data storage
  - `session_get_id()` - Get session identifier
  - `session_is_expired()` - Check session expiration
  - `session_cleanup_expired()` - Remove expired sessions
  - `session_from_request()` - Extract session from request cookie
  - `session_set_cookie()` - Set session cookie on response
  - Cookie-based session transport with HttpOnly and SameSite=Lax
  - Resolves PR #13 merge conflicts
- **Template Engine** (Phase 6.3) - Dynamic HTML generation
  - `template_context_create()` / `template_context_destroy()` - Context lifecycle
  - `template_context_set()` / `template_context_get()` - Variable management
  - `template_render()` - Render templates with `{{ variable }}` syntax
  - `template_load_file()` - Load templates from files
  - `http_response_send_template()` - Send rendered template as response
  - Hash map storage (256 buckets) for O(1) variable lookups
  - Resolves PR #15 merge conflicts
- **Authentication Middleware** (Phase 6.4) - Pluggable auth
  - `basic_auth_middleware_create()` / `basic_auth_middleware_destroy()` - HTTP Basic Auth
  - `apikey_auth_middleware_create()` / `apikey_auth_middleware_destroy()` - API Key validation
  - `jwt_auth_middleware_create()` / `jwt_auth_middleware_destroy()` - JWT (HMAC-SHA256)
  - Pure C SHA-256 implementation (FIPS 180-4)
  - Pure C HMAC-SHA256 (RFC 2104)
  - Pure C Base64/Base64URL decode
  - Constant-time signature comparison for timing attack prevention
  - New types: `basic_auth_config_t`, `apikey_auth_config_t`, `jwt_auth_config_t`
- **Database Connection Pool** (Phase 6.5) - Thread-safe pooling
  - `db_pool_create()` / `db_pool_destroy()` - Pool lifecycle
  - `db_pool_acquire()` / `db_pool_release()` - Connection management
  - `db_pool_get_stats()` - Pool statistics
  - `db_pool_close_idle()` - Close idle connections
  - Configurable min/max connections, timeouts, and validation
  - Pluggable backend callbacks for custom database types
  - New header: `include/db_pool.h`
  - Resolves PR #17 merge conflicts
- **API Documentation** (Phase 6.6) - `docs/api/README.md` comprehensive reference
- **17 new unit tests** for Phase 6 features (60/60 total passing)
- **Resolved merge conflicts** from PRs #13, #15, #17
- **Request Body Parsing** (Phase 5.1) - Parse HTTP request bodies
  - `http_request_parse_body()` - Auto-detect and parse body based on Content-Type
  - `http_request_get_form_field()` - Get URL-encoded or multipart form field value
  - `http_request_get_file()` - Get uploaded file from multipart form data
  - `body_parser_data_free()` - Free body parser resources
  - URL-encoded form data parsing with percent-decoding
  - Multipart form data parsing (RFC 7578) with boundary detection
  - File upload handling with size limits and filename sanitization
  - New types: `http_uploaded_file_t`, `http_form_field_t`, `body_parser_data_t`
- **Cookie Handling** (Phase 5.2) - RFC 6265 cookie support
  - `http_request_get_cookie()` - Parse and retrieve cookies from request
  - `http_response_set_cookie()` - Set cookies with full attribute support
  - `http_response_delete_cookie()` - Delete cookies via Max-Age=0
  - New type: `cookie_options_t` with Domain, Path, Max-Age, Secure, HttpOnly, SameSite
- **CORS Middleware** (Phase 5.3) - Cross-Origin Resource Sharing
  - `cors_middleware_create()` - Create configurable CORS middleware
  - `cors_middleware_destroy()` - Free CORS middleware resources
  - Preflight OPTIONS request handling with 204 No Content
  - Configurable allowed origins, methods, headers, credentials, max-age
  - New type: `cors_options_t`
- **Rate Limiting Middleware** (Phase 5.4) - IP-based rate limiting
  - `ratelimit_middleware_create()` - Create rate limiter with token bucket algorithm
  - `ratelimit_middleware_destroy()` - Free rate limiter resources
  - IP-based tracking via hash table with automatic cleanup
  - Rate limit headers: X-RateLimit-Limit, X-RateLimit-Remaining, X-RateLimit-Reset
  - 429 Too Many Requests response with Retry-After header
  - New type: `ratelimit_config_t`
- **Static File Serving** (Phase 5.5) - Efficient static asset delivery
  - `static_file_middleware_create()` - Create static file middleware
  - `static_file_middleware_destroy()` - Free static file middleware resources
  - MIME type detection for 17 common file types
  - Path traversal prevention via realpath() validation
  - ETag generation and conditional request support (304 Not Modified)
  - Cache-Control and Last-Modified headers
  - New type: `static_file_config_t`
- **New HTTP Status Codes** - `HTTP_NOT_MODIFIED` (304) and `HTTP_TOO_MANY_REQUESTS` (429)
- **15 new unit tests** for Phase 5 features (43/43 total passing)
- **Complete JSON Array Support** (Phase 4.4) - Full array parsing, serialization, and manipulation
  - `json_array_create()` - Create empty JSON array
  - `json_array_append()` - Append element to JSON array
  - `json_array_get()` - Get element by index
  - `json_array_length()` - Get number of elements
  - Full array parsing from JSON strings (nested arrays, mixed types)
  - Full array serialization via `json_stringify()`
  - 7 new unit tests (28/28 total passing)
- **`HTTP_SWITCHING_PROTOCOLS` enum value** - Added 101 status to `http_status_t` enum
- **Next Phase Roadmap** (NEXT_PHASE.md) - Detailed phased plan for v0.4.0–v0.6.0

### Fixed
- Compiler warning for `case 101` not in enumerated type `http_status_t`
- Replaced all magic number `101` references with `HTTP_SWITCHING_PROTOCOLS` enum value

### Changed
- Updated TODO.md with cross-references to NEXT_PHASE.md roadmap

## [0.3.0] - 2025-11-14

### Added
- **WebSocket frame processing in threaded mode** - Production-ready real-time bidirectional communication
  - Automatic ping/pong handling with <0.001s latency
  - Text and binary message echo support
  - Multiple concurrent WebSocket connections
  - Graceful connection close with status codes (1000-1015)
  - Persistent connections after HTTP upgrade
- Socket fd exposure to route handlers via `http_request_t.socket_fd`
- `handle_websocket_connection()` function for WebSocket frame processing loop
- Comprehensive WebSocket test suite (test_ping.py, test_handshake.py, test_basic_ws.py)
- Complete CHANGELOG.md following Keep a Changelog format

### Fixed
- WebSocket handshake response not being sent (removed `res->sent = true` in `websocket_handle_upgrade`)
- HTTP status 101 returning "OK" instead of "Switching Protocols"
- Connection header being overridden for WebSocket upgrade responses
- Example server not maintaining connections after WebSocket upgrade
- Server threading issue causing immediate shutdown after handshake

### Changed
- Updated `websocket_echo_server.c` example with working frame processing
- Simplified WebSocket connection management (removed manual tracking)
- Improved signal handling in example server
- Enhanced documentation with production-ready status indicators

## [0.2.0] - 2025-01-12

### Added
- Complete RFC 6455 WebSocket protocol implementation
  - WebSocket handshake (SHA-1 + Base64 accept key generation)
  - Frame encoding/decoding with masking/unmasking
  - Text and binary messages
  - Control frames (ping, pong, close)
  - Message fragmentation and reassembly
  - Close frames with status codes (1000-1015)
- WebSocket API with 14 functions in public header
- `websocket_echo_server.c` example with interactive HTML client
- Comprehensive WebSocket documentation (docs/WEBSOCKET.md)
- 3 WebSocket unit tests (21/21 tests passing)

### Documentation
- Added `WEBSOCKET.md` with complete usage guide (527 lines)
- Updated README with WebSocket usage examples
- Updated TODO.md marking WebSocket support as completed

## [0.1.0] - 2024-12-XX

### Added
- HTTP server with threaded and async I/O modes
- Event loop with epoll (Linux), kqueue (macOS/BSD), and poll (fallback)
- Flexible routing system with parameter support (`/users/:id`)
- Middleware chain support
- JSON parser and serializer
- Cross-platform build system (CMake)
- Example servers (simple_server, async_server)
- Basic unit test infrastructure
- Docker support with multi-stage builds
- Comprehensive documentation

### Core Features
- Pure C implementation with zero external dependencies
- ISO C99/C11 compliance
- Platform support: Linux, macOS, Windows
- Connection limits: 128 connections, 256 routes, 32 middlewares
- Buffer size: 8KB read buffer

---

## Release Notes

### v2.0.0 — HTTPS, New Targets, and a Security Pass (Current)

A major release built from 72 pull requests. The public header rename
(`weblib.h` → `kamran.k`) is the source-level break behind the version number;
the substance is HTTPS, two new runtime targets, and a long hardening run.

**Highlights:**
- Experimental pure-C TLS 1.3 server — 5,481 lines under `src/tls/`, zero
  external crypto dependencies, off by default (`-DWEBLIB_ENABLE_TLS=ON`)
- WebAssembly builds via Emscripten, and a Cloudflare Workers runtime with
  KV / R2 / D1 / Queues bindings
- Semantic versioning enforced in code: version macros, runtime accessors, and
  a static assertion tying the header to `CMakeLists.txt`
- A security pass covering XSS, request smuggling, slow loris, CSPRNG
  fail-closed behaviour, JWT verification, CORS, WebSocket memory exhaustion,
  and several use-after-free classes
- 6 ctest suites in a default build, 13 with TLS and its test hooks enabled;
  both configurations pass, and CI now actually compiles and runs the TLS code

**Honest limits:**
- The TLS layer is **EXPERIMENTAL and UNAUDITED**. It interoperates with
  `openssl s_client`, but it has had no external cryptographic review — do not
  use it to protect real secrets. Browser page load is not supported.
- TLS is server-side only, threaded mode only, native builds only, and refuses
  WebSocket upgrades.
- The Cloudflare KV / R2 / D1 / Queues bindings are in-memory simulations in
  every build. Nothing here talks to the real Cloudflare services — that would
  need a JS glue layer, and none ships in this repo.
- Roadmap Phases 13–20 (HTTP/2, storage engine, multi-process, HTTP/3, and the
  release-engineering tooling) are **not** implemented.

**Building with TLS:**

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo -DWEBLIB_ENABLE_TLS=ON
cmake --build build --parallel
cd build && ctest --output-on-failure
```

Add `-DWEBLIB_TLS_TEST_HOOKS=ON` to the configure step to also build
`TlsHttpTests`, which needs a deterministic-RNG seam — that is the 13-suite
configuration, and it is the one CI runs. The hook is test-only; never enable it
in a build you deploy.

---

### v1.0.0 — Production Release (superseded by v2.0.0)

This release marks the first production-ready version of the Modern C Web Library.
All planned phases (4-10) are complete with comprehensive documentation and tutorials.

**Highlights:**
- ✅ 129 unit tests passing with 100% success rate
- ✅ 25 source modules covering HTTP, WebSocket, JSON, middleware, and more
- ✅ 5 example servers including REST API and WebSocket echo
- ✅ Complete tutorial documentation (Getting Started, REST API, WebSocket)
- ✅ Comprehensive API reference for all modules
- ✅ GitHub Actions CI with Linux (GCC, Clang) and macOS (Clang)
- ✅ Zero compiler warnings, Valgrind-clean

**Architecture:**
- Threaded mode: Bounded thread pool with configurable worker count
- Async mode: Event loop with epoll/kqueue/poll backends
- Both modes are production-ready for deployment

> *Correction added in 2.0.0:* the "both modes are production-ready" line above
> did not hold for async mode. #89 later found the async path tracked
> connections but never closed idle or stalled ones, so a slow-drip client could
> exhaust the connection table. Fixed in 2.0.0 — see *Security* above.

---

## Version History

- **2.0.x**: HTTPS & new targets — experimental pure-C TLS 1.3, WASM, Cloudflare Workers, header renamed to `kamran.k`, broad security hardening
- **1.0.x**: Production release — all phases complete, tutorials, full documentation
- **0.9.x**: Performance & Observability — compression, caching, metrics, async WebSocket, benchmarking
- **0.8.x**: Security & Observability — CSRF, logging, error handler, input validation, health check
- **0.7.x**: Server Hardening & CI — timeouts, thread pool, graceful shutdown, CI, integration tests
- **0.6.x**: Production Readiness — sessions, templates, auth, DB pooling, API docs
- **0.5.x**: Request Processing & Security — body parsing, cookies, CORS, rate limiting, static files
- **0.4.x**: HTTP Foundation Hardening — parser, headers, JSON arrays, connections
- **0.3.x**: WebSocket frame processing (Production-ready threaded mode)
- **0.2.x**: WebSocket support (RFC 6455 compliant)
- **0.1.x**: Initial HTTP server implementation with event loop

[Unreleased]: https://github.com/kamrankhan78694/modern-c-web-library/compare/v2.1.0...HEAD
[2.1.0]: https://github.com/kamrankhan78694/modern-c-web-library/compare/v2.0.1...v2.1.0
[2.0.1]: https://github.com/kamrankhan78694/modern-c-web-library/compare/v2.0.0...v2.0.1
[2.0.0]: https://github.com/kamrankhan78694/modern-c-web-library/compare/v1.0.0...v2.0.0
[1.0.0]: https://github.com/kamrankhan78694/modern-c-web-library/compare/v0.9.0...v1.0.0
[0.9.0]: https://github.com/kamrankhan78694/modern-c-web-library/compare/v0.8.0...v0.9.0
[0.8.0]: https://github.com/kamrankhan78694/modern-c-web-library/compare/v0.7.0...v0.8.0
[0.7.0]: https://github.com/kamrankhan78694/modern-c-web-library/compare/v0.6.0...v0.7.0
[0.6.0]: https://github.com/kamrankhan78694/modern-c-web-library/compare/v0.3.0...v0.6.0
[0.3.0]: https://github.com/kamrankhan78694/modern-c-web-library/releases/tag/v0.3.0
[0.2.0]: https://github.com/kamrankhan78694/modern-c-web-library/releases/tag/v0.2.0
[0.1.0]: https://github.com/kamrankhan78694/modern-c-web-library/releases/tag/v0.1.0
