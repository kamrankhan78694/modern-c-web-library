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
#
# But accepting ANY of those numbers for EVERY claim is too weak, and it let a
# real regression through: adding StressDemoApp took the default build from 6
# suites to 7, and "6 ctest suites in a default build" stayed green because 6
# was still valid as the TLS subtotal. The number was checked; the sentence it
# appeared in was not. So when the claim names its configuration, hold it to
# that configuration's count, and stay permissive only when it names none.
#
# Version-scoped history is exempt. A changelog entry or a "v2.0.0 baseline"
# line is a record of what was true at a point in time; it is SUPPOSED to keep
# saying 6 after the seventh suite lands. Rewriting those to match today would
# falsify the history, and flagging them would train everyone to ignore this
# check. Only present-tense claims about the current tree are held to account.
bad=0
while IFS= read -r hit; do
    [ -z "$hit" ] && continue
    case "$hit" in
        CHANGELOG.md:*)      continue ;;   # release-scoped by definition
        *baseline*|*20[0-9][0-9]-[0-9][0-9]-[0-9][0-9]*) continue ;;
    esac
    # One sentence often, and legitimately, states counts for MORE THAN ONE
    # configuration: "13 suites rather than 14", "all 14 suites, plus the 7 TLS
    # suites". Reading only the first number and holding it to only one
    # configuration flagged several such correct lines. A checker that flags
    # correct text gets switched off, so: take EVERY count on the line, and
    # accept the union of the configurations the line actually names. That still
    # closes the original hole — "6 ctest suites in a default build" names one
    # configuration, so its 6 is measured against 7 and fails.
    expected=""; label=""
    case "$hit" in *default*)
        expected="$expected $DEFAULT_N"; label="$label a default build," ;; esac
    case "$hit" in *hooks*|*TLS_TEST_HOOKS*)
        expected="$expected $WITH_HOOKS $HOOKS_N"; label="$label a TLS+hooks build," ;; esac
    # "a TLS build" covers BOTH TLS configurations in prose: most lines saying
    # "build with TLS on and run all N suites" refer to a configure that also
    # passed -DWEBLIB_TLS_TEST_HOOKS=ON, without saying the word "hooks". The
    # checker cannot know which configure a sentence means, so it accepts either
    # total here. The precision that matters is not lost: a line naming the
    # DEFAULT build is still held to exactly one number, which is the case that
    # actually went stale.
    case "$hit" in *TLS*|*tls*)
        expected="$expected $WITH_TLS $WITH_HOOKS $TLS_N $TLS_ONLY $HOOKS_N"; label="$label a TLS build," ;; esac
    if [ -z "$expected" ]; then
        expected="$DEFAULT_N $WITH_TLS $WITH_HOOKS $TLS_N $TLS_ONLY"; label=" any configuration,"
    fi

    for n in $(printf '%s' "$hit" | grep -oE '[0-9]+ (ctest |test )?suites' | grep -oE '^[0-9]+'); do
        hit_ok=0
        for e in $expected; do [ "$n" = "$e" ] && hit_ok=1; done
        if [ "$hit_ok" -eq 0 ]; then
            bad=$((bad + 1))
            printf '          %s\n' "$hit"
            printf '            ^ says %s suites;%s has:%s\n' "$n" "${label% }" "$expected"
        fi
    done
done <<EOF
$(git ls-files '*.md' | tr '\n' '\0' | xargs -0 grep -nE '[0-9]+ (ctest |test )?suites|\*\*[0-9]+ (ctest |test )?suites\*\*' 2>/dev/null || true)
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

# ---------------------------------------------------------------------------
# 5. Documented unit-test counts agree WITH EACH OTHER.
#    The suite-count check above says nothing about the number of tests inside
#    WebLibTests, so that figure drifted freely: it sat at 166 in four files
#    while the binary reported 172, and every check here still passed. The true
#    value needs a build, which this script deliberately does not do — but
#    disagreement between documents needs no build to detect, and every drift so
#    far has shown up as exactly that.
# ---------------------------------------------------------------------------
echo "[5] documented unit-test counts agree with each other"

TC_SEEN=""
while IFS= read -r hit; do
    [ -z "$hit" ] && continue
    case "$hit" in
        CHANGELOG.md:*) continue ;;                       # release-scoped history
        *baseline*|*20[0-9][0-9]-[0-9][0-9]-[0-9][0-9]*) continue ;;
        *stress_demo_app.sh*) continue ;;                 # narrative in a file header
    esac
    # Only claims ABOUT WebLibTests. A bare "N tests" matches unrelated prose —
    # "the 37 added since", "129 tests present at the time" — and a checker that
    # reports those as disagreement is noise, which is how checkers get disabled.
    case "$hit" in
        *WebLibTests*|*test_weblib*) ;;
        *) continue ;;
    esac
    n="$(printf '%s' "$hit" | grep -oE '[0-9]+ (unit )?tests?' | grep -oE '^[0-9]+' | head -1)"
    [ -z "$n" ] && continue
    TC_SEEN="$TC_SEEN$n
"
done <<EOF
$(git ls-files '*.md' | tr '\n' '\0' | xargs -0 grep -nE '[0-9]+ unit tests|\([0-9]+ tests\)|runs \*\*[0-9]+ tests\*\*' 2>/dev/null || true)
EOF

TC_UNIQ="$(printf '%s\n' "$TC_SEEN" | sed '/^$/d' | sort -u)"
# printf '%s' (no \n) leaves no trailing newline, so `wc -l` reported 0 for a
# single value and this check announced success having compared nothing — the
# very failure mode it exists to catch. Count with a terminated line.
TC_N="$(printf '%s\n' "$TC_UNIQ" | sed '/^$/d' | wc -l | tr -d ' ')"
if [ "$TC_N" -eq 0 ]; then
    pass "no documented unit-test counts to cross-check"
elif [ "$TC_N" -eq 1 ]; then
    pass "all documented unit-test counts agree ($TC_UNIQ)"
else
    fail "documented unit-test counts disagree: $(printf '%s' "$TC_UNIQ" | tr '\n' ' ')"
    git ls-files '*.md' | tr '\n' '\0' | xargs -0 grep -nE '[0-9]+ unit tests|\([0-9]+ tests\)|runs \*\*[0-9]+ tests\*\*' 2>/dev/null \
        | grep -v '^CHANGELOG.md:' | sed 's/^/          /'
fi
echo

# ---------------------------------------------------------------------------
# 6. Present-tense "the current version is X" claims in markdown must match.
#    Check [1] binds the four BUILD files to CMakeLists.txt, which is why the
#    header above cites `"currently 2.0.0" in DOCKER_PACKAGE.md` as a motivating
#    failure — and yet that file's version BADGE then shipped 2.0.0 through both
#    2.0.1 and 2.1.0, because no check ever read markdown. Checking the build
#    files and calling the class closed is how it survived: the instance named in
#    review was fixed, the class was not.
#
#    Only forms that can ONLY mean "this is the current version" are matched:
#    a shields.io version badge, a release-tag badge link, "currently X.Y.Z",
#    and a `**Version X.Y.Z**` banner. Historical prose ("shipped in 2.0.0",
#    changelog entries, "fixed in 2.0.1") uses none of these, so it is not
#    flagged — a checker that cries wolf gets disabled, and then protects
#    nothing.
#
#    USE vs MENTION. Its first run flagged a correct line in
#    copilot-instructions.md that QUOTES the historical bad strings while
#    explaining this very failure: `docker push ...:2.0.0` and "currently
#    2.0.0". Quoting a stale claim is not making one. Backticked and
#    double-quoted spans are markdown's mention markers, so they are stripped
#    before version literals are read. The cost is honest and small: a claim
#    written entirely inside a code span — PUBLISH_GUIDE.md's
#    "(currently `2.1.0`)" — is not checked here. Every claim that actually went
#    stale (the badge, five `**Version X.Y.Z**` banners) is bare text and is.
# ---------------------------------------------------------------------------
echo "[6] present-tense version claims in markdown match CMakeLists.txt"

if [ -z "$CMAKE_VER" ]; then
    fail "no CMakeLists.txt version to check markdown against"
else
    bad=0
    while IFS= read -r hit; do
        [ -z "$hit" ] && continue
        case "$hit" in CHANGELOG.md:*) continue ;; esac   # release-scoped by definition
        # Strip mentions (see USE vs MENTION above) before reading versions.
        used="$(printf '%s' "$hit" | sed 's/`[^`]*`//g; s/"[^"]*"//g')"
        # Every version literal that survives must be the current one. These
        # forms are current-version claims by construction, so a different
        # number is stale, not history.
        for v in $(printf '%s' "$used" | grep -oE '[0-9]+\.[0-9]+\.[0-9]+'); do
            if [ "$v" != "$CMAKE_VER" ]; then
                bad=$((bad + 1))
                printf '          %s\n' "$hit"
                printf '            ^ claims %s is current; CMakeLists.txt says %s\n' "$v" "$CMAKE_VER"
                break
            fi
        done
    done <<EOF
$(git ls-files '*.md' | tr '\n' '\0' | xargs -0 grep -nE \
    'img\.shields\.io/badge/version-[0-9]+\.[0-9]+\.[0-9]+|releases/tag/v[0-9]+\.[0-9]+\.[0-9]+\)|[Cc]urrently \`?[0-9]+\.[0-9]+\.[0-9]+|\*\*Version [0-9]+\.[0-9]+\.[0-9]+\*\*' 2>/dev/null || true)
EOF
    if [ "$bad" -eq 0 ]; then
        pass "every current-version claim in markdown says $CMAKE_VER"
    else
        fail "$bad markdown claim(s) name a version other than $CMAKE_VER as current (listed above)"
    fi
fi
echo

echo "=== $((checks - fails))/$checks checks passed ==="
if [ "$fails" -gt 0 ]; then
    echo "FAILED: $fails check(s). These are mechanical facts, not style opinions —" >&2
    echo "a failure here means a documented claim disagrees with the build files." >&2
    exit 1
fi
exit 0
