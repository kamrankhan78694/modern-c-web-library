# Security Policy

This document explains how to report security vulnerabilities and how we handle them for the
Modern C Web Library.

The project’s philosophy is pure ISO C (C99/C11) with zero external runtime dependencies. Most
security issues will involve memory safety, parser correctness, concurrency, or denial‑of‑service.
As of v2.0.0 the repository also contains hand-written cryptography — please read the next
section before you evaluate or report against it.

## Experimental Cryptography (`src/tls/`)

v2.0.0 adds a TLS 1.3 **server** written from scratch in pure C (`src/tls/`, 5,481 lines). Every
primitive it uses is this project's own code: SHA-512, HKDF, ChaCha20, Poly1305, the
ChaCha20-Poly1305 AEAD, X25519 and Ed25519 live in `src/tls/`, and it builds on the SHA-256 and
HMAC-SHA256 the library already had in `src/crypto/sha256.c`. On top of those sit a
bounds-checked DER/ASN.1 reader and a PEM reader — both of which parse untrusted input — the
PKCS#8 and SubjectPublicKeyInfo key parsing built on them, a TLS 1.3 key schedule, a record
layer, and a server handshake state machine. Note that the server certificate itself is decoded
from PEM to DER and then sent opaquely: there is no X.509 certificate-field parsing and no chain
validation.

**It is EXPERIMENTAL and UNAUDITED.** No external cryptographic audit has been performed. Do not
use it to protect real secrets in production until one has.

What it is, precisely:

- One profile only: cipher suite `TLS_CHACHA20_POLY1305_SHA256`, key exchange X25519, signature
  Ed25519. No TLS 1.2, no AES-GCM, no RSA, no ECDSA.
- Server-side only (there is no TLS client), native builds only (not WASM or Cloudflare Workers),
  and threaded mode only — `http_server_enable_tls()` returns `-1` when the server is in async
  mode. A WebSocket upgrade on a TLS connection is refused.
- **Off by default.** The CMake option `WEBLIB_ENABLE_TLS` defaults to `OFF`; with it off not one
  line of `src/tls/` is compiled, and `http_server_enable_tls()` remains only as a stub that
  returns `-1`. You opt in at build time with `-DWEBLIB_ENABLE_TLS=ON`.
- `WEBLIB_TLS_TEST_HOOKS` (also `OFF` by default) exposes a deterministic-RNG seam used only by
  the test suite. A build with that flag on provides no TLS security whatsoever and must never be
  shipped.

What this means if you are reporting: cryptographic bugs and TLS handshake-state-machine bugs are
a first-class category of report here, alongside memory safety and parser correctness. The
handshake state machine (`src/tls/server_handshake.c`, `src/tls/handshake*.c`) is where TLS
implementation bugs historically concentrate — state confusion, transcript-hash manipulation,
skipped or bypassable authentication steps — and findings there are the most valuable reports
this project can receive. Experimental status is not a reason for us to close a report; see the
scope list below. `src/tls/README.md` documents the exact security scope, the deliberate
non-goals (no TLS 1.2, no AES-GCM, no RSA/ECDSA, no client-certificate verification, no
resumption or 0-RTT), and the known interop bound of the Ed25519-only profile.

## Reporting a Vulnerability (Private)

Please use GitHub’s private reporting channel so details are not publicly disclosed before a fix is
available:

- Preferred: Open a private security advisory
  https://github.com/kamrankhan78694/modern-c-web-library/security/advisories/new
- Alternative: Use the "Report a vulnerability" button in the repository Security tab
- Please do not open public issues for security topics

Include the following information in your report when possible:
- Affected component(s) and file paths (e.g., `src/http_server.c`)
- Version(s) or commit SHA(s) tested, and platform (Linux/macOS/Windows)
- Reproduction steps and a minimal proof of concept (PoC)
- Expected vs actual behavior and security impact
- Crash logs, sanitizer output, or valgrind traces if available
- Suggested remediation ideas (optional)
- CVSS v3.1 vector or severity rationale (optional)

We are happy to coordinate on a secure channel within the advisory thread.

## Scope

In scope:
- Source under `src/`, public headers in `include/`, examples in `examples/`, and tests in `tests/`
- Build and container artifacts maintained here (`Dockerfile`, `Dockerfile.dev`, scripts)
- Parser, routing, event‑loop, and JSON logic implemented by this project
- The experimental pure-C TLS 1.3 layer under `src/tls/`, built with `-DWEBLIB_ENABLE_TLS=ON`.
  Its EXPERIMENTAL / UNAUDITED status does **not** narrow this scope: reports against the crypto
  primitives, the DER/ASN.1 and PEM readers and the key parsing built on them, the record layer,
  the key schedule, and the server handshake state machine are all in scope, and being off by
  default does not exclude them

Out of scope:
- Vulnerabilities in third‑party operating system packages or toolchains used to build/run locally
- Misconfigurations in consumer applications built on top of this library
- Features that are genuinely unimplemented — roadmap entries in `TODO.md` / `NEXT_PHASE.md` with
  no code behind them. The TLS 1.3 layer is *not* in this category: it is implemented, merged, and
  exercised by CI, and it is in scope even though it is disabled by default
- Documented non-goals of the TLS layer (see `src/tls/README.md`) — for example the absence of
  TLS 1.2, AES-GCM, RSA/ECDSA, client-certificate verification, session resumption or 0-RTT. A
  report that one of these is missing is a feature request; a report that one of the *implemented*
  paths can be tricked into behaving as if it existed is a vulnerability

## Supported Versions

We provide security fixes for the following:
- Main branch (HEAD)
- Latest tagged release

Backports to older releases may be considered for Critical/High severity issues on a
case‑by‑case basis.

## Our Process and Timelines

- Acknowledgement: within 72 hours
- Initial triage and severity assessment: within 7 calendar days
- Fix development: target 30 days for High/Critical; 60–90 days for Medium/Low
- Coordinated disclosure: we will coordinate a public advisory and release notes with you

Actual timelines can vary depending on complexity and availability, but we aim to communicate
regularly via the advisory thread.

## Disclosure and Credit

We follow coordinated disclosure. Once a fix is available, we will:
- Publish a security advisory with details, impact, and mitigation
- Credit reporters who wish to be acknowledged (please specify your preferred name/handle)
- When applicable, request a CVE or accept a GHSA identifier through GitHub Advisories

## Severity and Assessment

We generally use CVSS v3.1 as a guideline for severity, considering impact and likelihood in the
context of typical deployments. Memory safety bugs (e.g., buffer overflows, use‑after‑free), request
smuggling, parser inconsistencies, and concurrency issues receive particular attention.

The same particular attention applies to the hand-written TLS 1.3 layer in `src/tls/`, which is
in scope despite being experimental and off by default. In that code we especially want to hear
about: handshake state-machine flaws (state confusion, transcript-hash manipulation, skipped or
bypassable authentication steps, failure to fail closed on error), non-constant-time comparison
or secret-dependent branching and indexing, nonce or key-schedule reuse, and malformed-input
handling in the DER/ASN.1 reader, the PEM reader, and the Ed25519 key parsing — all three of
which are reached with attacker- or operator-supplied bytes.

## Safe Harbor

We support good‑faith security research. As long as you:
- Make a good‑faith effort to avoid privacy violations, data destruction, and service disruption
- Report vulnerabilities privately via the channels above
- Do not exploit a vulnerability beyond what is necessary to establish proof of concept

…we will not initiate legal action against you. Please do not run automated scans or exploits
against other users’ deployments without permission.

## Contact

Primary channel: GitHub Security Advisories (private) at the link above. If you cannot use that
mechanism, open a minimal issue asking for a private contact method for security reporting
(without including sensitive details), and we will follow up.
