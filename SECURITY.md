# Security Policy

This document explains how to report security vulnerabilities and how we handle them for the
Modern C Web Library.

The project’s philosophy is pure ISO C (C99/C11) with zero external runtime dependencies. Most
security issues will involve memory safety, parser correctness, concurrency, or denial‑of‑service.

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

Out of scope:
- Vulnerabilities in third‑party operating system packages or toolchains used to build/run locally
- Misconfigurations in consumer applications built on top of this library
- Unreleased or disabled features (e.g., future TLS/WebSocket items listed in `TODO.md`)

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
