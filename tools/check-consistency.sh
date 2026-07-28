#!/usr/bin/env bash
#
# check-consistency.sh — mechanically verify claims that have repeatedly drifted.
#
# WHY THIS EXISTS
#
# Three times in one release cycle, a fix landed that corrected the instance a
# reviewer named while leaving the same claim wrong elsewhere:
#
#   * v2.0.1's version bump updated CMakeLists, the header, the Dockerfile and
#     the markdown banners — and missed `docker push ...:2.0.0` in PUBLISH_GUIDE.md
#     and "currently 2.0.0" in DOCKER_PACKAGE.md. Publishing 2.0.1 alongside
#     instructions that push 2.0.0 was one review comment away from shipping.
#   * examples/simple_server.c reported a hardcoded "1.0.0" for two major
#     versions. It is the release image's entrypoint, so the shipped container
#     answered {"version":"1.0.0"}.
#   * src/tls/README.md said `-DWEBLIB_ENABLE_TLS=ON` builds seven TLS suites.
#     It builds six; the seventh needs -DWEBLIB_TLS_TEST_HOOKS=ON.
#
# Each was found by a human or a review bot reading carefully. That does not
# scale and it did not hold. These checks derive the truth from the build files
# and fail the build on divergence, so the class cannot recur silently.
#
# Deliberately narrow: only claims with a single machine-readable source of
# truth. A check that needs judgement belongs in review, not here — a checker
# that cries wolf gets disabled, and then protects nothing.
#
# Usage: tools/check-consistency.sh   (from the repository root)
set -u

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO_ROOT" || exit 1

fails=0
checks=0

pass() { checks=$((checks + 1)); printf '  ok    %s\n' "$1"; }
fail() { checks=$((checks + 1)); fails=$((fails + 1)); printf '  FAIL  %s\n' "$1"; }

echo "=== repository consistency checks ==="
echo

# ---------------------------------------------------------------------------
# 1. Version agreement.
#    CMakeLists.txt is the source of truth; everything else must match it.
# ---------------------------------------------------------------------------
echo "[1] version declarations agree"

CMAKE_VER="$(sed -n 's/^project(.*VERSION \([0-9][0-9.]*\).*/\1/p' CMakeLists.txt | head -1)"
if [ -z "$CMAKE_VER" ]; then
    fail "could not read the version from CMakeLists.txt"
else
    pass "CMakeLists.txt declares $CMAKE_VER"

    MAJ="${CMAKE_VER%%.*}"
    REST="${CMAKE_VER#*.}"
    MIN="${REST%%.*}"
    PAT="${REST#*.}"

    for pair in "WEBLIB_VERSION_MAJOR:$MAJ" "WEBLIB_VERSION_MINOR:$MIN" "WEBLIB_VERSION_PATCH:$PAT"; do
        macro="${pair%%:*}"; want="${pair##*:}"
        got="$(sed -n "s/^#define $macro \([0-9][0-9]*\).*/\1/p" include/kamran.k | head -1)"
        if [ "$got" = "$want" ]; then pass "include/kamran.k $macro = $got"
        else fail "include/kamran.k $macro = '${got:-unset}', expected '$want'"; fi
    done

    got="$(sed -n 's/^#define WEBLIB_VERSION "\([0-9][0-9.]*\)".*/\1/p' include/kamran.k | head -1)"
    if [ "$got" = "$CMAKE_VER" ]; then pass "include/kamran.k WEBLIB_VERSION = \"$got\""
    else fail "include/kamran.k WEBLIB_VERSION = \"${got:-unset}\", expected \"$CMAKE_VER\""; fi

    # Every image.version LABEL must match, not merely the first — Dockerfile.release
    # carries one per build stage, and updating only one is an easy miss.
    bad=0; n=0
    while IFS= read -r v; do
        n=$((n + 1))
        [ "$v" = "$CMAKE_VER" ] || bad=$((bad + 1))
    done <<EOF
$(sed -n 's/.*org\.opencontainers\.image\.version="\([0-9][0-9.]*\)".*/\1/p' Dockerfile.release)
EOF
    if [ "$n" -eq 0 ]; then fail "Dockerfile.release has no image.version LABEL"
    elif [ "$bad" -eq 0 ]; then pass "Dockerfile.release: all $n image.version LABELs = $CMAKE_VER"
    else fail "Dockerfile.release: $bad of $n image.version LABELs differ from $CMAKE_VER"; fi

    got="$(sed -n 's/^VERSION=\${1:-"\([0-9][0-9.]*\)"}.*/\1/p' publish-package.sh | head -1)"
    if [ "$got" = "$CMAKE_VER" ]; then pass "publish-package.sh default = $got"
    else fail "publish-package.sh default = '${got:-unset}', expected '$CMAKE_VER'"; fi
fi
echo

# ---------------------------------------------------------------------------
# 2. No hardcoded version literals in shipped C.
#    simple_server.c reported "1.0.0" from a string literal for two major
#    versions; WEBLIB_VERSION exists precisely so that cannot happen.
# ---------------------------------------------------------------------------
echo "[2] no hardcoded x.y.z version literals in src/ or examples/"

hits="$(grep -rn '"[0-9][0-9]*\.[0-9][0-9]*\.[0-9][0-9]*"' src examples 2>/dev/null \
        | grep -v 'WEBLIB_VERSION' || true)"
if [ -z "$hits" ]; then
    pass "none found (use WEBLIB_VERSION)"
else
    fail "hardcoded version literal(s) — use WEBLIB_VERSION instead:"
    printf '%s\n' "$hits" | sed 's/^/          /'
fi
echo

# ---------------------------------------------------------------------------
# 3. Documented ctest suite counts match tests/CMakeLists.txt.
#    src/tls/README.md claimed seven TLS suites from -DWEBLIB_ENABLE_TLS=ON
#    alone; it is six, because TlsHttpTests is gated on the test-hooks flag.
# ---------------------------------------------------------------------------
echo "[3] documented suite counts match tests/CMakeLists.txt"

# Count registrations by the flag that gates them.
#
# Real nesting matters: the TLS section contains nested if() blocks
# (WEBLIB_TLS_TEST_HOOKS, and TARGET tls_server AND BASH_EXECUTABLE). A parser
# that treats any endif() as closing the TLS block drops out of TLS context at
# the first nested endif and miscounts everything after it as a default suite.
# The first version of this script did exactly that; it produced correct numbers
# only because TlsInteropOpenssl happens to be the last registration in the
# block. Track depth properly, and remember the depth each guard opened at.
DEFAULT_N=0; TLS_N=0; HOOKS_N=0
depth=0; tls_at=-1; hooks_at=-1
while IFS= read -r line; do
    line="${line%%#*}"                       # ignore comments
    case "$line" in
        *endif\(\)*)
            [ "$hooks_at" -eq "$depth" ] && hooks_at=-1
            [ "$tls_at"   -eq "$depth" ] && tls_at=-1
            depth=$((depth - 1))
            ;;
        *if\(*)
            depth=$((depth + 1))
            case "$line" in
                *WEBLIB_TLS_TEST_HOOKS*) hooks_at=$depth ;;
                *WEBLIB_ENABLE_TLS*)     tls_at=$depth ;;
            esac
            ;;
        *add_test\(NAME*)
            if   [ "$hooks_at" -ge 0 ]; then HOOKS_N=$((HOOKS_N + 1))
            elif [ "$tls_at"   -ge 0 ]; then TLS_N=$((TLS_N + 1))
            else                             DEFAULT_N=$((DEFAULT_N + 1)); fi
            ;;
    esac
done < tests/CMakeLists.txt

WITH_TLS=$((DEFAULT_N + TLS_N))
WITH_HOOKS=$((WITH_TLS + HOOKS_N))
TLS_ONLY=$((TLS_N + HOOKS_N))
pass "registered: $DEFAULT_N default, $WITH_TLS with TLS, $WITH_HOOKS with TLS+hooks ($TLS_ONLY of them TLS)"

# Any doc stating "N suites" must use a number that describes a real
# configuration: the repo totals, or the TLS-only subtotals (docs legitimately
# say "7 TLS suites"). A figure matching none of them is stale by construction.
#
# NOTE: this list started as the three repo totals and immediately produced a
# false positive on a correct "7 ctest suites" (the TLS subtotal). A checker
# that flags correct text gets switched off, so the subtotals are valid here.
bad=0
while IFS= read -r hit; do
    [ -z "$hit" ] && continue
    n="$(printf '%s' "$hit" | sed 's/.*[^0-9]\([0-9][0-9]*\)[^0-9]*suites.*/\1/')"
    case "$n" in
        "$DEFAULT_N"|"$WITH_TLS"|"$WITH_HOOKS"|"$TLS_N"|"$TLS_ONLY") ;;
        *) bad=$((bad + 1)); printf '          %s\n' "$hit" ;;
    esac
done <<EOF
$(git ls-files '*.md' | tr '\n' '\0' | xargs -0 grep -nE '\*\*[0-9]+ (ctest )?suites\*\*|[0-9]+ ctest suites' 2>/dev/null || true)
EOF
if [ "$bad" -eq 0 ]; then
    pass "every documented suite count matches a real configuration ($DEFAULT_N/$WITH_TLS/$WITH_HOOKS total, $TLS_N/$TLS_ONLY TLS)"
else
    fail "$bad documented suite count(s) match no real configuration (listed above)"
fi
echo

# ---------------------------------------------------------------------------
# 4. The Valgrind CI step must gate on every binary and prove it ran some.
#    It previously reported all six and gated on one, and could pass having
#    tested nothing at all.
# ---------------------------------------------------------------------------
echo "[4] the Valgrind CI step still gates"

CI=.github/workflows/ci.yml
if ! grep -q "valgrind" "$CI" 2>/dev/null; then
    fail "no valgrind step found in $CI"
else
    if grep -q '|| rc=1' "$CI"; then pass "accumulates per-binary status (|| rc=1)"
    else fail "no per-binary status accumulation — a failure in any but the last binary would be discarded"; fi

    if grep -q 'ran=$((ran+1))' "$CI" && grep -q '"$ran" -eq 0' "$CI"; then
        pass "fails when zero binaries matched"
    else
        fail "nothing asserts at least one binary ran — the step could pass having tested nothing"
    fi

    if grep -q 'errors-for-leak-kinds' "$CI"; then pass "leak kinds stated explicitly"
    else fail "--errors-for-leak-kinds not stated; the gate relies on Valgrind's default"; fi
fi
echo

echo "=== $((checks - fails))/$checks checks passed ==="
if [ "$fails" -gt 0 ]; then
    echo "FAILED: $fails check(s). These are mechanical facts, not style opinions —" >&2
    echo "a failure here means a documented claim disagrees with the build files." >&2
    exit 1
fi
exit 0
