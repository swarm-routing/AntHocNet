#!/usr/bin/env bash
# Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
# SPDX-License-Identifier: MIT OR GPL-2.0-only
#
# AntHocNet's downstream canary (swarm-routing/sim-containers test/downstream.sh,
# a wrapper of swarm-routing/.github tools/downstream/downstream.sh): run in
# this checkout, inside a candidate ns3-dev image (Ubuntu debug, ns-3 tree at
# /opt/ns-3, `ns3-build-contrib` on PATH), before the image's `latest-*` tags
# move. Exit 0 = pass. When this file exists the canary runs it instead of
# AntHocNet's line in sim-containers test/downstream.txt; the line's ns-3
# range still decides which images run it. The same holds for a release dry
# run that lists AntHocNet as a consumer (reusable-release.yml).
#
# 1. Install, build and test as the consumer line says: its stage, then
#    `ns3-build-contrib` on its modules with its suites, then its run (the
#    example), read from the environment downstream.sh gives every consumer.
# 2. Run the behaviour gates a release move can break (#1222): ns-3.49 drew a
#    different field from the same seed, which broke arm-delivery and two
#    anchors, and the canary (suites + example only) let `latest` move first.
#    The gates are ci-ns3.yml's `ns3-gates` steps, called the same way; keep
#    GATES below in step with them. Every gate runs, a failing one does not
#    stop the rest, and the summary lists each with its time.
#
#    Which gates. The canary image is `debug` (-O0), where a simulation runs
#    ~10x slower than on the `default` image ci-ns3.yml uses, and downstream.sh
#    gives a consumer 3600 s. GATES holds the gates whose verdict rests on a
#    field drawn from the seed or on the stack itself, the ones a release
#    moves: arm-delivery, every check-anchors.sh anchor (misc, fanet, vanet),
#    the determinism gates and the satellite anchors and arm smoke (43 min
#    one after another on ns3-dev:3.49-debug, 4 CPUs; side by side, as below,
#    about the slowest gate's 21 min; plus ~2 min of install and suites).
#    Left out, each ~5-30 min on
#    debug and none on a seed-drawn field: the 3-D field and Manhattan
#    smokes, scene replay, partition and disaster (committed or generated
#    scenes), seed independence, leo-walker; and misc's tuning-bench line
#    (it tests the learn site's flag list, not the release). They run in
#    ci-ns3.yml on every PR once `latest` has moved.
#
# The gates' seeds and floors were checked from ns-3.48 on (#1222); an older
# image (an on-demand build of past releases) runs step 1 only. A gate that
# fails on a new release because its field moved is re-anchored by the rule
# in docs/benchmarks/methodology.md, never by lowering a floor.
#
# CANARY_GATES="arm-delivery misc" runs only those groups of GATES (default: all).
set -euo pipefail

cd "$(dirname "$0")/.."
REPO=$PWD
NS3DIR=${NS3DIR:-/opt/ns-3}

# Step 1 is the consumer line's own columns, which downstream.sh passes to the
# container as C_STAGE, C_MODULES, C_SUITES and C_RUN: a release dry run's
# line (swarm-routing/sim-tools .github/downstream.txt stages its candidate
# routing-bench with NS3_TOOLS_DIR) keeps meaning what it says. Without them
# (a run by hand), the defaults are sim-containers' line.
if [ -n "${C_MODULES:-}" ]; then
    stage=${C_STAGE:-} modules=$C_MODULES suites=${C_SUITES:-} run=${C_RUN:-}
else
    name=$(basename "$REPO")
    # shellcheck disable=SC2016  # expanded by the bash below, in the checkout
    stage='mkdir -p .stage/contrib && touch .stage/CMakeLists.txt && make -s install-ns3 NS3DIR="$PWD/.stage"'
    modules=""
    for m in swarm-core routing-bench oracle aomdv gpsr adaptive anthocnet; do
        modules="$modules $m=$name/.stage/contrib/$m"
    done
    suites="anthocnet oracle adaptive routing-bench"
    run="cd $NS3DIR && ./ns3 run --no-build anthocnet-example"
fi

if [ -n "$stage" ]; then echo "canary: stage: $stage"; bash -euo pipefail -c "$stage"; fi
args=()
for spec in $modules; do args+=("${spec%%=*}=$(dirname "$REPO")/${spec#*=}"); done
tests=(--test)
for s in $suites; do tests+=(--suite "$s"); done
ns3-build-contrib "${tests[@]}" "${args[@]}"
if [ -n "$run" ]; then echo "canary: run: $run"; bash -euo pipefail -c "$run"; fi

minor=$(sed 's/^3\.\([0-9]*\).*/\1/' "$NS3DIR/VERSION")
if [ "$minor" -lt 48 ]; then
    echo "canary: ns-3 3.$minor is older than the gates' checked releases (3.48+, #1222): suites and example only"
    exit 0
fi

# group|command, the slowest first (debug, measured 2026-11-24: gpsr3d-recovery
# 21 min, arm-delivery 10, mesh-hwmp-shortest 3.5, the rest < 3 each), so
# the parallel run below finishes about when the slowest gate does.
GATES=(
    "fanet|bash tools/checks/check-anchors.sh $NS3DIR fanet-gpsr3d-recovery"
    "arm-delivery|bash tools/checks/check-arm-delivery.sh $NS3DIR"
    "misc|bash tools/checks/check-anchors.sh $NS3DIR mesh-hwmp-shortest"
    "misc|bash tools/checks/check-anchors.sh $NS3DIR single-hop"
    "fanet|bash tools/checks/check-anchors.sh $NS3DIR fanet-single-hop-3d"
    "fanet|bash tools/checks/check-anchors.sh $NS3DIR fanet-vertical-3d"
    "vanet|bash tools/checks/check-anchors.sh $NS3DIR vanet-single-hop"
    "vanet|bash tools/checks/check-anchors.sh $NS3DIR vanet-building"
    "vanet|bash tools/checks/check-anchors.sh $NS3DIR vanet-gpcr-vs-gpsr"
    "seeds|bash tools/checks/check-determinism.sh $NS3DIR"
    "satellite|bash tools/checks/check-sat-anchors.sh $NS3DIR single-isl"
    "satellite|bash tools/checks/check-sat-anchors.sh $NS3DIR hop-delay"
    "satellite|bash tools/checks/check-sat-arms.sh $NS3DIR"
    "satellite|bash tools/checks/check-determinism.sh $NS3DIR isl-grid"
)
want=${CANARY_GATES:-arm-delivery misc fanet vanet seeds satellite}
for w in $want; do
    case " ${GATES[*]%%|*} " in *" $w "*) ;; *) echo "canary: no gate group '$w' (CANARY_GATES)" >&2; exit 2 ;; esac
done

# Each gate is one single-threaded simulation after another, and the tree is
# built, so the gates run side by side, CANARY_JOBS at a time (default: the
# CPU count); each gate's output is printed whole, in GATES order.
par=${CANARY_JOBS:-$(nproc)}
[[ "$par" =~ ^[1-9][0-9]*$ ]] || { echo "canary: CANARY_JOBS must be a positive integer, not '$par'" >&2; exit 2; }
out=$(mktemp -d)
n=0
for g in "${GATES[@]}"; do
    group=${g%%|*}
    case " $want " in *" $group "*) ;; *) continue ;; esac
    while [ "$(jobs -rp | wc -l)" -ge "$par" ]; do wait -n || true; done
    (
        start=$(date +%s)
        if (cd "$REPO" && ${g#*|}) > "$out/$n.log" 2>&1; then rc=0; else rc=$?; fi
        echo "$rc $(( $(date +%s) - start ))" > "$out/$n.rc"
    ) &
    n=$((n + 1))
done
wait

fails=0
summary=()
i=0
for g in "${GATES[@]}"; do
    group=${g%%|*}
    cmd=${g#*|}
    case " $want " in *" $group "*) ;; *) continue ;; esac
    read -r rc took < "$out/$i.rc"
    echo "::group::canary gate [$group] $cmd"
    cat "$out/$i.log"
    echo "::endgroup::"
    if [ "$rc" -eq 0 ]; then
        summary+=("pass  ${took}s  [$group] $cmd")
    else
        summary+=("FAIL  ${took}s  [$group] $cmd (exit $rc)")
        fails=$((fails + 1))
    fi
    i=$((i + 1))
done
rm -rf "$out"

echo "canary: gates on ns-3 $(cat "$NS3DIR/VERSION"), $par at a time"
printf '  %s\n' "${summary[@]}"
[ "$fails" -eq 0 ] || { echo "canary: $fails gate(s) failed" >&2; exit 1; }
echo "canary: every gate passed"
