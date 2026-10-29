#!/usr/bin/env bash
# Proves the pairing protocol model (formal/pairing.spthy) with Tamarin in
# Docker, and checks that each deliberately broken variant is caught.
#
#   formal/run.sh               # everything; exits non-zero on any surprise
#   NO_DOCKER=1 formal/run.sh   # use tamarin-prover and maude from PATH
#   TIMEOUT=600 formal/run.sh   # seconds allowed per prover run (default 300)
#
# Full prover output: formal/build/<variant>.txt
# SPDX-License-Identifier: AGPL-3.0-or-later
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
IMAGE="irl2fa-tamarin:1.12.0-maude3.5.1"
TIMEOUT="${TIMEOUT:-300}"

# Every lemma in the model must be verified.
BASE_LEMMAS=(
    executable_honest_pairing
    executable_mitm_with_collision
    key_secrecy
    mitm_requires_sas_collision
    injective_agreement
    one_comparison_per_session
    commit_before_reveal
    collision_inputs_fixed
)

# Broken variants: "FLAG|extra tamarin args|lemma=result,lemma=result".
# The honest run must still be possible, and the property that the removed
# check protects must fail. NO_SAS_CHECK uses the "consecutive" heuristic:
# the default one does not find that attack within minutes.
VARIANTS=(
    "NO_COMMIT_CHECK||executable_honest_pairing=verified,commit_before_reveal=falsified,collision_inputs_fixed=falsified"
    "REVEAL_EARLY||executable_honest_pairing=verified,commit_before_reveal=falsified,collision_inputs_fixed=falsified"
    "NO_SAS_CHECK|--heuristic=c|executable_honest_pairing=verified,mitm_requires_sas_collision=falsified"
)

# prove NAME [tamarin args...] -> build/NAME.txt, build/NAME.rc, build/NAME.secs
prove() {
    local name="$1"; shift
    local rc=0 t0=$SECONDS
    timeout "$TIMEOUT" tamarin-prover "$@" "$HERE/pairing.spthy" >"$HERE/build/$name.txt" 2>&1 || rc=$?
    echo "$rc" >"$HERE/build/$name.rc"
    echo "$((SECONDS - t0))" >"$HERE/build/$name.secs"
}

# result NAME LEMMA -> "verified", "falsified", ... or "missing"
result() {
    local line
    line="$(sed -n '/^summary of summaries:/,$p' "$HERE/build/$1.txt" | grep -E "^ +$2 \((all-traces|exists-trace)\): " || true)"
    if [[ -z "$line" ]]; then echo "missing"; return; fi
    line="${line#*: }"
    echo "${line%% *}"
}

# check NAME "lemma=want ..." -> prints the results, returns 1 on a mismatch
check() {
    local name="$1" lemma want got bad=0 rc
    rc="$(cat "$HERE/build/$name.rc")"
    if [[ "$rc" != 0 ]]; then
        tail -n 20 "$HERE/build/$name.txt" >&2
        echo "FAIL [$name]: tamarin-prover exited with $rc (124 = timed out after ${TIMEOUT}s)" >&2
        return 1
    fi
    if grep -q "wellformedness check failed" "$HERE/build/$name.txt"; then
        sed -n '/WARNING: the following wellformedness checks failed/,/\*\//p' "$HERE/build/$name.txt" >&2
        echo "FAIL [$name]: wellformedness check failed" >&2
        bad=1
    fi
    for p in $2; do
        lemma="${p%%=*}"; want="${p#*=}"
        got="$(result "$name" "$lemma")"
        if [[ "$got" == "$want" ]]; then
            printf '   ok    %-32s %s\n' "$lemma" "$got"
        else
            printf '   FAIL  %-32s %s, expected %s\n' "$lemma" "$got" "$want" >&2
            bad=1
        fi
    done
    return $bad
}

inner() {
    local fail=0 spec flag extra checks lemma t0=$SECONDS
    rm -rf "$HERE/build"
    mkdir -p "$HERE/build"
    tamarin-prover --version | head -n 1
    echo "Proving the model and ${#VARIANTS[@]} broken variants in parallel (timeout ${TIMEOUT}s each)..."

    prove base --prove &
    for spec in "${VARIANTS[@]}"; do
        IFS='|' read -r flag extra checks <<<"$spec"
        local args=()
        for p in ${checks//,/ }; do args+=("--prove=${p%%=*}"); done
        # shellcheck disable=SC2086  # $extra is a deliberate word list
        prove "$flag" "-D=$flag" $extra "${args[@]}" &
    done
    wait

    echo "== base model: every lemma verified ($(cat "$HERE/build/base.secs") s)"
    local want_base=""
    for lemma in "${BASE_LEMMAS[@]}"; do want_base+="$lemma=verified "; done
    check base "$want_base" || fail=1
    local n_all
    n_all="$(sed -n '/^summary of summaries:/,$p' "$HERE/build/base.txt" | grep -cE '^ +[A-Za-z0-9_]+ \((all-traces|exists-trace)\): ' || true)"
    if [[ "$n_all" != "${#BASE_LEMMAS[@]}" ]]; then
        echo "FAIL [base]: the model has $n_all lemmas, run.sh expects ${#BASE_LEMMAS[@]}; update BASE_LEMMAS" >&2
        fail=1
    fi

    for spec in "${VARIANTS[@]}"; do
        IFS='|' read -r flag extra checks <<<"$spec"
        echo "== variant $flag: broken on purpose, must be caught ($(cat "$HERE/build/$flag.secs") s)"
        check "$flag" "${checks//,/ }" || fail=1
    done

    if [[ $fail -ne 0 ]]; then
        echo "formal verification FAILED after $((SECONDS - t0)) s (full output in formal/build/)" >&2
        exit 1
    fi
    echo "formal verification passed in $((SECONDS - t0)) s: ${#BASE_LEMMAS[@]} lemmas verified, ${#VARIANTS[@]} broken variants caught"
}

if [[ "${1:-}" == "--inner" ]]; then
    inner
    exit 0
fi
if [[ -n "${NO_DOCKER:-}" ]]; then
    inner
    exit 0
fi

if ! docker image inspect "$IMAGE" >/dev/null 2>&1; then
    docker build -t "$IMAGE" "$HERE"
fi
docker run --rm -u "$(id -u):$(id -g)" -e HOME=/tmp -e LANG=C.UTF-8 -e TIMEOUT="$TIMEOUT" \
    -v "$HERE:$HERE" -w "$HERE" "$IMAGE" bash "$HERE/run.sh" --inner
