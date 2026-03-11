# Next Phase Development Plan — v1.1.0 Security Hardening

## 1) Idea Intake

**Core problem (one sentence):** The library needs production-grade transport security and security-verification tooling implemented in pure C so users can deploy safely without external dependencies.

## 2) Crystallized Brief

| Dimension | Definition |
|---|---|
| Target users | C backend teams using this library for internet-facing APIs and WebSocket services |
| Desired outcomes | TLS-enabled server mode, stronger credential handling, request-traceability, and repeatable security verification in CI |
| Non-goals | HTTP/3, non-C dependencies, framework abstractions, database/ORM features, UI tooling |

## 3) Grounded First-Principles Design (from fundamentals)

### Fundamental constraints
1. Data on untrusted networks must be encrypted in transit.
2. Secrets in process memory must be minimized, bounded, and wiped deterministically.
3. Authentication primitives must be verifiable by standards-based test vectors.
4. Every security control must have an executable validation gate.
5. All implementations must remain pure C + platform APIs.

### Architecture + reasoning

| Module | Why it must exist | Core design |
|---|---|---|
| Crypto primitives (`sha256`, `hmac`, `aes-gcm`) | TLS and password/key features require trustworthy primitives | Deterministic APIs, explicit buffer sizes, vector-based tests |
| KDF/password (`pbkdf2`, `hkdf`, password hash API) | Avoid raw password storage and support key expansion | Salted hashes, iteration control, constant-time verification |
| TLS transport (`tls_record`, `tls_handshake`, PEM loader) | Prevent credential/session disclosure on network | TLS 1.2-only state machine, strict parse/serialize boundaries |
| Server integration (`http_server_enable_tls`) | Make TLS usable through existing server API | Opt-in TLS mode with cert/key load at startup |
| Security middleware (`request_id`, `ip_access`) | Improve traceability and reduce exposure surface | Request correlation header + CIDR allow/deny checks |
| Security verification pipeline (fuzz + sanitizers + valgrind gates) | Catch parser and memory-safety defects before release | CI jobs with fail-fast thresholds and artifact logs |

## 4) Adversarial Review

| Failure mode / attack | Likely impact | Exposure if unmitigated | Required mitigation |
|---|---|---|---|
| Passive traffic capture | Credential/session compromise | Critical | TLS 1.2 transport integration + protocol floor enforcement |
| TLS parser/state bug | Remote crash or logic bypass | High | Strict state machine transitions + malformed handshake tests |
| Secret lingering in memory | Post-compromise key theft | High | Central secure zeroization + bounded key lifetime |
| Weak password hashing defaults | Offline brute-force acceleration | High | PBKDF2-HMAC-SHA256 with high default iterations + salt |
| Header spoof / poor traceability | Incident triage failure | Medium | Request ID middleware with propagation rules |
| Unauthorized network access | API abuse from untrusted IPs | High | IP allow/deny middleware with CIDR parsing |
| Parser memory bug | RCE/DoS risk | Critical | Fuzzing harness + ASan/MSan + valgrind gates |

## 5) Design Iteration (after critique)

Refinements from adversarial review:
1. **Ship order changed to reduce blast radius first:** crypto → TLS core → server integration → middleware → audit tooling.
2. **Mandatory security utility reuse:** all secret-bearing modules must use one shared secure-zero utility (no ad-hoc wipes).
3. **Protocol hard limits:** reject TLS versions below 1.2 and reject malformed handshake transitions early.
4. **Verification-as-gate:** no module is “done” until vector tests/sanitizers for that module pass in CI.

## 6) Atomic Planning (smallest verifiable tasks)

### A. Crypto foundation
- [ ] A1. Add SHA-256 implementation file + header declarations.
- [ ] A2. Add HMAC-SHA256 implementation using SHA-256 module.
- [ ] A3. Add AES-GCM encrypt/decrypt APIs with explicit nonce/tag lengths.
- [ ] A4. Add unit tests with NIST/RFC vectors for A1-A3.

### B. Password + key derivation
- [ ] B1. Add PBKDF2-HMAC-SHA256 implementation.
- [ ] B2. Add HKDF extract/expand implementation.
- [ ] B3. Add `password_hash_create` and `password_hash_verify` public APIs.
- [ ] B4. Add constant-time compare usage in verify path.
- [ ] B5. Add vector and round-trip tests for B1-B4.

### C. TLS transport
- [ ] C1. Add TLS record parse/serialize module.
- [ ] C2. Add TLS handshake state machine skeleton with explicit transition table.
- [ ] C3. Add PEM cert/key loader with error-checked parsing.
- [ ] C4. Add key material lifecycle hooks (load, use, secure zero, free).
- [ ] C5. Add negative tests for malformed records/handshake messages.

### D. Server integration
- [ ] D1. Add `http_server_enable_tls(server, cert_path, key_path)` API.
- [ ] D2. Integrate TLS handshake into accepted connection path.
- [ ] D3. Add HTTPS example and integration smoke test.

### E. Security middleware
- [ ] E1. Add request ID middleware (generate or propagate `X-Request-Id`).
- [ ] E2. Add IP allow/deny middleware with CIDR matching.
- [ ] E3. Add middleware tests (ID uniqueness, deny hits 403, allow passes).

### F. Security verification tooling
- [ ] F1. Add HTTP parser fuzz harness target.
- [ ] F2. Add ASan CI job.
- [ ] F3. Add MSan CI job (where supported).
- [ ] F4. Add valgrind memory gate for relevant tests.

## 7) Parallel Build Strategy

| Parallel track | Tasks | Constraint |
|---|---|---|
| Track P1 | A1-A4 (crypto primitives) | Independent; start immediately |
| Track P2 | E1-E3 (middleware) | Independent of TLS internals |
| Track P3 | F1 + F4 (fuzz harness + valgrind gate scaffolding) | Can begin with existing parser |
| Sequential S1 | B1-B5 after A1-A2 | PBKDF2/HKDF depend on HMAC/SHA |
| Sequential S2 | C1-C5 after A1-A3 | TLS depends on crypto primitives |
| Sequential S3 | D1-D3 after C1-C5 | Server TLS integration depends on TLS core |
| Sequential S4 | F2-F3 after module tests stable | Sanitizer jobs should gate stable test targets |

## 8) Milestone Roadmap (1–2 week slices)

| Slice | Scope | Exit criteria |
|---|---|---|
| Week 1 | A1-A4 + E1 groundwork | Crypto vectors passing; request ID middleware compiles and unit tests pass |
| Week 2 | B1-B5 + E2-E3 | KDF/password APIs tested; IP access middleware behavior validated |
| Week 3 | C1-C3 | TLS record + handshake skeleton + PEM loader with negative tests |
| Week 4 | C4-C5 + D1 | Key lifecycle hardening complete; TLS API exposed in public header |
| Week 5 | D2-D3 + F1 | HTTPS connection path works in integration test; fuzz target building |
| Week 6 | F2-F4 + stabilization | ASan/MSan/valgrind gates integrated; release candidate security checklist green |

## 9) Build Validation (success criteria per module)

| Module | Required checks | Done when |
|---|---|---|
| Crypto | Standards vectors + boundary tests | 100% vectors pass, no UB/sanitizer errors |
| KDF/password | RFC vectors + wrong-password negative tests | Verify path constant-time and deterministic outcomes |
| TLS core | Handshake positive/negative tests | Valid handshake succeeds; malformed sequences fail safely |
| Server TLS integration | HTTPS smoke + regression tests | Existing HTTP tests still pass; HTTPS returns expected responses |
| Middleware | Unit + integration tests | Request IDs consistent; IP rules enforced correctly |
| Fuzz/sanitizers | Fuzz run + ASan/MSan/valgrind | No crashes, no sanitizer findings, no leaks |

## 10) QA Pipeline

### Automated before merge
- Build matrix: Linux (gcc), macOS (clang).
- Unit tests for all new modules.
- TLS integration tests (HTTP + HTTPS paths).
- Fuzz harness execution budgeted run.
- ASan/MSan jobs where toolchain supports them.
- Valgrind leak/error gate on Linux.

### Manual before release candidate
- Verify HTTPS with local cert and `curl --tlsv1.2`.
- Validate graceful startup/shutdown in TLS-enabled mode.
- Exercise request ID propagation via proxied requests.
- Verify IP denylist and allowlist behavior from distinct source addresses.
- Review logs for secret leakage (no key material, no raw credentials).

## 11) Security Review (threat model, access control, data safety)

### Assets
- TLS private keys (highest sensitivity)
- Password-derived hashes and salts
- Session/auth headers in request memory
- Security logs and request identifiers

### Access control model
- TLS private key bytes: readable only within TLS module internals.
- Password verify path: exposed via API return values only; no raw secret output.
- Middleware decisions: route-level evaluation, default-deny if config parse fails.
- CI security artifacts: accessible only in repository Actions context.

### Risk mitigation notes
1. **Memory safety risk:** enforce bounded copies, allocator checks, and sanitizer gates.
2. **Crypto correctness risk:** block merge if any vector set fails.
3. **Operational misconfiguration risk:** fail TLS startup with explicit errors for invalid cert/key.
4. **Regression risk:** require all existing tests + new module tests green before merge.
5. **Performance risk:** track handshake latency and request throughput deltas against baseline.

---

## Deliverable Summary (execution-ready)

- **Architecture + reasoning:** Sections 3 and 5
- **Milestone roadmap (1–2 week slices):** Section 8
- **Task breakdown (atomic, assignable):** Section 6
- **Validation + QA checklist:** Sections 9 and 10
- **Security + risk mitigation notes:** Section 11
