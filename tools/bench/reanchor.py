#!/usr/bin/env python3
# Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
# SPDX-License-Identifier: MIT OR GPL-2.0-only
"""Re-anchor a gate's seed window when an ns-3 release moves the field (#1222).

The field a gate runs on is a function of the seed AND of the ns-3 release
(#1222: ns-3.49's ad-hoc MAC takes 2 more RNG streams per device, so the same
seed draws another field). A gate that presupposes a protocol-independent
property of its field (static, connected, multi-hop) keeps its floors and
parameters; only its seed window is re-chosen, by the rule of
docs/benchmarks/methodology.md "Re-anchoring a gate's seeds", read off the
oracle alone, never off an arm:

    the first N consecutive seeds from 1 (N = the gate's --runs) in which
    every flow is connected (oracle PDR >= 99) and multi-hop (oracle
    hopsMean >= 2.0) on every release in the comparison.

A gate that measures an arm's paper trend takes no seed selection (fixed
seeds 1..N); a gate whose property holds by construction (single-hop,
analytic geometry) needs none. This script never changes a floor, a bound or
a gate: it prints the proposed window and the evidence, and a person edits
--firstRun in the gate's script with the table in the PR.

The gates' invocations are read from tools/checks/check-anchors.sh and
tools/checks/check-arm-delivery.sh, never restated here; only which kind each
gate is lives here (KIND), and --self-test fails when a gate has none.

    reanchor.py gates                         every gate, its kind, its seeds
    reanchor.py survey --ns3 DIR [--gate G]... [--seeds M] [--out FILE]
                                              the oracle on seeds 1..M, one row
                                              per seed (TSV), in one built tree
    reanchor.py propose SURVEY... [--gate G]...
                                              the rule over every release in
                                              the surveys: window + evidence
    reanchor.py run --ns3 DIR [--ns3 DIR]... [--gate G]... [--seeds M]
                                              survey each tree, then propose
    reanchor.py commands --image REF [--image REF]... [--seeds M]
                                              the docker lines that build this
                                              checkout in each ns3-dev image
                                              and survey it, then propose
    reanchor.py --self-test                   parser and rule on fixtures

A tree must have AntHocNet installed and built with its examples (as
`make install-ns3` and the ns-3 build in docs/development.md, or the lines
`commands` prints). Standard library only, plus the bench_parse reader at the
ns3-tools.lock pin (tools/ns3_tools.py).
"""

import argparse
import os
import re
import shlex
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
ANCHORS_SH = os.path.join(ROOT, "tools", "checks", "check-anchors.sh")
ARM_DELIVERY_SH = os.path.join(ROOT, "tools", "checks", "check-arm-delivery.sh")

# The rule's thresholds (#1222), on the oracle's per-seed row.
PDR_MIN = 99.0
HOPS_MIN = 2.0

# Which rule applies to each gate. "window": its field must be static,
# connected and multi-hop, so its seeds are chosen by the rule. "trend": an
# arm's paper trend, measured on fixed seeds 1..N, never selected. "geometry":
# the property holds for every seed by construction (one hop, or an analytic
# field), so no window is chosen. "literature": a published floor on a
# random field, read over fixed seeds like a trend.
KIND = {
    "arm-delivery": "window",
    "mesh-hwmp-shortest": "window",
    "fanet-gpsr3d-recovery": "trend",
    "vanet-gpcr-vs-gpsr": "trend",
    "broch-low-mobility": "literature",
    "single-hop": "geometry",
    "fanet-single-hop-3d": "geometry",
    "fanet-vertical-3d": "geometry",
    "vanet-single-hop": "geometry",
    "vanet-building": "geometry",
}


def _read(path):
    with open(path) as fh:
        return fh.read()


def parse_anchors(text):
    """{anchor: {"harness", "args"}} from check-anchors.sh's case block."""
    gates = {}
    m = re.search(r'^case "\$ANCHOR" in\n(.*?)^esac', text, re.DOTALL | re.MULTILINE)
    if not m:
        raise ValueError("check-anchors.sh: no `case \"$ANCHOR\" in` block")
    for name, body in re.findall(r"^    ([a-z0-9-]+)\)\n(.*?)^        ;;", m.group(1), re.DOTALL | re.MULTILINE):
        h = re.search(r"^\s*harness=(\S+)", body, re.MULTILINE)
        a = re.search(r'^\s*args="([^"]*)"', body, re.MULTILINE)
        if not a:
            raise ValueError(f"check-anchors.sh: anchor {name} has no args=")
        gates[name] = {"harness": h.group(1) if h else "manet-baselines", "args": a.group(1)}
    return gates


def parse_arm_delivery(text):
    """The arm-delivery smoke's invocation: routing-compare with its ARGS."""
    a = re.search(r'^ARGS="([^"]*)"', text, re.MULTILINE)
    if not a:
        raise ValueError("check-arm-delivery.sh: no ARGS=")
    return {"harness": "routing-compare", "args": a.group(1)}


def load_gates(anchors_text=None, arm_text=None):
    gates = parse_anchors(anchors_text if anchors_text is not None else _read(ANCHORS_SH))
    gates["arm-delivery"] = parse_arm_delivery(arm_text if arm_text is not None else _read(ARM_DELIVERY_SH))
    for name, g in gates.items():
        g["kind"] = KIND.get(name)
        g["first"], g["runs"] = window_of(g["args"])
    return gates


def window_of(args):
    """(firstRun, runs) of an invocation; the harness defaults are 1 and 1."""
    first = re.search(r"--firstRun=(\d+)", args)
    runs = re.search(r"--runs=(\d+)", args)
    return (int(first.group(1)) if first else 1, int(runs.group(1)) if runs else 1)


def survey_cmd(gate, seed):
    """The gate's own invocation on one seed, the oracle alone."""
    args = re.sub(r"\s*--(firstRun|runs|protocols)=\S+", "", gate["args"]).strip()
    return f"{gate['harness']} {args} --firstRun={seed} --runs=1 --protocols=oracle"


def ns3_release(ns3):
    with open(os.path.join(ns3, "VERSION")) as fh:
        return fh.read().strip()


def survey(ns3, names, seeds, out):
    """Run the oracle on seeds 1..seeds for each gate; write TSV rows."""
    sys.path.insert(0, os.path.join(ROOT, "tools"))
    import ns3_tools

    query = ns3_tools.module("bench_parse").query
    gates = load_gates()
    release = ns3_release(ns3)
    rows = []
    for name in names:
        for s in range(1, seeds + 1):
            cmd = survey_cmd(gates[name], s)
            print(f"[reanchor] ns-3 {release} {name} seed {s}: ./ns3 run \"{cmd}\"", file=sys.stderr)
            p = subprocess.run(["./ns3", "run", cmd], cwd=ns3, capture_output=True, text=True, check=False)
            text = p.stdout + p.stderr  # the gates read both (2>&1)
            if p.returncode != 0:
                sys.stderr.write(text[-3000:])
                raise SystemExit(f"reanchor: {name} seed {s} did not run on ns-3 {release}")
            pdr = query(text, "oracle", "pdr", "run")
            hops = query(text, "oracle", "hopsMean", "paths")
            cells = (release, name, str(s), pdr[0] if pdr else "-", hops[0] if hops else "-")
            out.write("\t".join(cells) + "\n")
            rows += read_survey("\t".join(cells))  # typed as propose reads a file
            out.flush()
    return rows


def read_survey(text):
    """[(release, gate, seed, pdr|None, hops|None)] from survey TSV."""
    rows = []
    for line in text.splitlines():
        if not line.strip() or line.startswith("#"):
            continue
        release, gate, seed, pdr, hops = line.split("\t")
        rows.append((release, gate, int(seed), _num(pdr), _num(hops)))
    return rows


def _num(v):
    return None if v in ("-", "") else float(v)


def ok(pdr, hops):
    return pdr is not None and hops is not None and pdr >= PDR_MIN and hops >= HOPS_MIN


def first_window(good, n, seeds):
    """The first s with s..s+n-1 all in `good`, within 1..seeds; else None."""
    for s in range(1, seeds - n + 2):
        if all(t in good for t in range(s, s + n)):
            return s
    return None


def propose(rows, gates, names=None):
    """Lines of the proposal per gate: the joint window, each release's, the table."""
    out = []
    by_gate = {}
    for release, gate, seed, pdr, hops in rows:
        by_gate.setdefault(gate, {}).setdefault(release, {})[seed] = (pdr, hops)
    # The comparison is every release any survey covers: a gate missing
    # from one of them gets no joint window.
    every = sorted({r[0] for r in rows}, key=lambda r: [int(x) for x in re.findall(r"\d+", r)])
    for name in names or sorted(by_gate):
        g = gates.get(name)
        if g is None:
            out.append(f"{name}: not a gate in check-anchors.sh or check-arm-delivery.sh")
            continue
        if g["kind"] != "window":
            out.append(f"{name}: {g['kind']} gate, no seed selection (seeds {g['first']}-{g['first'] + g['runs'] - 1} as they are)")
            continue
        per = by_gate.get(name)
        if not per:
            out.append(f"{name}: no survey rows")
            continue
        missing = [r for r in every if r not in per]
        if missing:
            out.append(f"{name}: PROPOSED: none: no survey on {', '.join(missing)} (every release in the comparison needs one)")
            continue
        releases = every
        n = g["runs"]
        seeds = min(max(per[r]) for r in releases)
        cur = f"{g['first']}-{g['first'] + n - 1}"
        good = {r: {s for s, v in per[r].items() if ok(*v)} for r in releases}
        joint = first_window(set.intersection(*good.values()), n, seeds)
        out.append(f"{name}: N={n} (--runs), current seeds {cur} (--firstRun={g['first']}), surveyed seeds 1-{seeds} on {', '.join(releases)}")
        for r in releases:
            s = first_window(good[r], n, seeds)
            out.append(f"  {r} alone: " + (f"seeds {s}-{s + n - 1}" if s else f"no window in 1-{seeds}"))
        if joint is None:
            out.append(f"  PROPOSED: none: no {n} consecutive seeds in 1-{seeds} qualify on every release;"
                       " survey more seeds, or the gate goes to the maintainer")
        else:
            same = "unchanged" if joint == g["first"] else f"was {cur}"
            out.append(f"  PROPOSED: seeds {joint}-{joint + n - 1} (--firstRun={joint} --runs={n}; {same})")
        out.append("  | seed | " + " | ".join(releases) + " |")
        out.append("  |---|" + "---|" * len(releases))
        for s in range(1, seeds + 1):
            cells = []
            for r in releases:
                pdr, hops = per[r].get(s, (None, None))
                txt = f"{'-' if pdr is None else f'{pdr:.1f}'} / {'-' if hops is None else f'{hops:.2f}'}"
                cells.append(f"**{txt}**" if s in good[r] else txt)
            mark = " <" if joint is not None and joint <= s < joint + n else ""
            out.append(f"  | {s}{mark} | " + " | ".join(cells) + " |")
        out.append(f"  (oracle PDR / hopsMean; bold: connected and multi-hop, PDR >= {PDR_MIN:g} and hopsMean >= {HOPS_MIN:g}; < the window)")
    return out


def window_gates(gates):
    return [n for n in sorted(gates) if gates[n]["kind"] == "window"]


def commands(images, seeds):
    """Docker lines: build this checkout in each image, survey into ./reanchor-<tag>.tsv."""
    inner = (
        "cp -a /src /w && cd /w && mkdir -p .stage/contrib && touch .stage/CMakeLists.txt"
        " && make -s install-ns3 NS3DIR=/w/.stage"
        " && ns3-build-contrib "
        + " ".join(f"{m}=/w/.stage/contrib/{m}" for m in
                   ("swarm-core", "routing-bench", "oracle", "aomdv", "gpsr", "adaptive", "anthocnet"))
        + f" && python3 tools/bench/reanchor.py survey --ns3 /opt/ns-3 --seeds {seeds} --out /out/$OUT"
    )
    lines = ["# from this checkout's root; each line writes ./reanchor-<tag>.tsv"]
    outs = []
    for ref in images:
        tag = re.sub(r"[^A-Za-z0-9._-]", "_", ref.rsplit(":", 1)[-1])
        out = f"reanchor-{tag}.tsv"
        outs.append(out)
        lines.append(
            f'docker run --rm -e OUT={out} -v "$PWD:/src:ro" -v "$PWD:/out" {shlex.quote(ref)} '
            f"bash -euo pipefail -c {shlex.quote(inner)}"
        )
    lines.append("python3 tools/bench/reanchor.py propose " + " ".join(outs))
    return lines


# --- self-test -------------------------------------------------------------

# `survey` on seeds 1-20 in ns3-dev:3.48-debug and 3.49-debug (sim-tools
# v1.1.3, oracle PDR / hopsMean), the evidence #1222 chose its seeds on; a
# field with no delivered packet prints hopsMean 0.00 (an older reader, "-").
FIXTURE_ARM = {
    "3.48": "100.0/5.32 100.0/3.34 100.0/2.35 100.0/2.31 33.1/1.00 66.7/1.99 99.5/3.00 65.3/2.50 100.0/2.66 100.0/3.74 "
            "66.6/2.00 33.6/3.00 100.0/1.66 33.6/3.00 100.0/3.00 100.0/3.33 33.7/3.00 0.0/- 100.0/2.96 65.7/2.49",
    "3.49": "67.3/1.51 100.0/1.68 99.5/2.66 100.0/3.00 100.0/1.33 67.0/1.00 100.0/1.00 100.0/2.35 66.1/3.00 100.0/2.67 "
            "0.0/0.00 100.0/2.67 100.0/2.32 33.6/2.00 68.0/2.00 100.0/3.94 0.0/0.00 100.0/3.71 100.0/2.02 100.0/1.67",
}
FIXTURE_MESH = {
    "3.48": "100.0/6.00 100.0/3.00 100.0/3.00 100.0/2.48 0.0/0.00 50.2/1.00 100.0/3.00 100.0/2.50 100.0/3.00 100.0/4.55 "
            "100.0/2.00 0.0/0.00 100.0/2.00 49.7/3.00 100.0/3.00 100.0/2.48 50.3/3.00 0.0/0.00 100.0/2.48 49.6/2.00",
    "3.49": "50.2/1.00 100.0/1.00 99.6/2.50 100.0/2.49 100.0/1.00 100.0/1.00 100.0/1.00 100.0/2.00 100.0/3.00 100.0/3.00 "
            "0.0/0.00 100.0/2.50 100.0/3.00 49.7/2.00 100.0/2.00 100.0/5.48 0.0/0.00 100.0/2.50 100.0/2.02 100.0/1.50",
}


def _fixture_tsv(name, table):
    lines = ["# reanchor survey (fixture)"]
    for release, cells in table.items():
        for s, cell in enumerate(cells.split(), 1):
            pdr, hops = cell.split("/")
            lines.append("\t".join((release, name, str(s), pdr, hops)))
    return "\n".join(lines) + "\n"


def self_test():
    fails = []

    def check(what, got, want):
        if got != want:
            fails.append(f"{what}: got {got!r}, want {want!r}")

    gates = load_gates()
    check("every gate has a KIND", sorted(n for n, g in gates.items() if g["kind"] is None), [])
    check("KIND names only real gates", sorted(set(KIND) - set(gates)), [])
    check("window gates", window_gates(gates), ["arm-delivery", "mesh-hwmp-shortest"])
    check("arm-delivery harness", gates["arm-delivery"]["harness"], "routing-compare")
    check("default harness", gates["single-hop"]["harness"], "manet-baselines")
    check("gpsr3d runs", gates["fanet-gpsr3d-recovery"]["runs"], 20)
    check("window_of default", window_of("--time=60"), (1, 1))
    check("window_of", window_of("--firstRun=3 --runs=2"), (3, 2))
    g = {"harness": "anthocnet-compare", "args": "--nNodes=16 --firstRun=3 --runs=2 --standard=80211a --diag"}
    check("survey_cmd", survey_cmd(g, 7),
          "anthocnet-compare --nNodes=16 --standard=80211a --diag --firstRun=7 --runs=1 --protocols=oracle")

    # The rule on #1222's evidence: arm-delivery (N=1) -> seed 3; mesh (N=2) -> 3-4.
    fake = {
        "arm-delivery": {"kind": "window", "first": 1, "runs": 1, "args": ""},
        "mesh-hwmp-shortest": {"kind": "window", "first": 1, "runs": 2, "args": ""},
        "fanet-gpsr3d-recovery": {"kind": "trend", "first": 1, "runs": 20, "args": ""},
    }
    rows = read_survey(_fixture_tsv("arm-delivery", FIXTURE_ARM) + _fixture_tsv("mesh-hwmp-shortest", FIXTURE_MESH))
    check("rows read", len(rows), 80)
    check("missing hops reads None", rows[17][4], None)
    text = "\n".join(propose(rows, fake))
    check("arm-delivery proposal", "PROPOSED: seeds 3-3 (--firstRun=3 --runs=1; was 1-1)" in text, True)
    check("arm-delivery 3.48 alone", "3.48 alone: seeds 1-1" in text, True)
    check("mesh proposal", "PROPOSED: seeds 3-4 (--firstRun=3 --runs=2; was 1-2)" in text, True)
    check("mesh 3.49 alone", "3.49 alone: seeds 3-4" in text.split("mesh-hwmp-shortest:")[1], True)
    check("evidence row", "| 3 < | **100.0 / 2.35** | **99.5 / 2.66** |" in text, True)
    check("partitioned row", "| 11 | 66.6 / 2.00 | 0.0 / 0.00 |" in text, True)
    check("missing row", "| 18 | 0.0 / - | **100.0 / 3.71** |" in text, True)
    fake["arm-delivery"]["first"] = 3
    check("unchanged", "seeds 3-3 (--firstRun=3 --runs=1; unchanged)" in "\n".join(propose(rows, fake)), True)
    # Seeds where only one release qualifies never make a window.
    only = read_survey("3.48\tarm-delivery\t1\t100\t3\n3.49\tarm-delivery\t1\t67\t1.5\n")
    check("no window", "PROPOSED: none" in "\n".join(propose(only, fake)), True)
    # A gate surveyed on one release of the comparison only gets no window.
    part = read_survey(_fixture_tsv("arm-delivery", FIXTURE_ARM) + _fixture_tsv("mesh-hwmp-shortest", {"3.48": FIXTURE_MESH["3.48"]}))
    check("incomplete evidence", propose(part, fake, ["mesh-hwmp-shortest"]),
          ["mesh-hwmp-shortest: PROPOSED: none: no survey on 3.49 (every release in the comparison needs one)"])
    # survey's own rows are typed as read_survey's (run passes them to propose).
    check("typed rows", read_survey("3.49\tarm-delivery\t3\t99.5\t2.66"), [("3.49", "arm-delivery", 3, 99.5, 2.66)])
    check("trend gate", propose(rows, fake, ["fanet-gpsr3d-recovery"]),
          ["fanet-gpsr3d-recovery: trend gate, no seed selection (seeds 1-20 as they are)"])
    check("first_window", (first_window({2, 3, 5, 6, 7}, 3, 10), first_window({1}, 2, 10)), (5, None))
    cmds = commands(["ghcr.io/swarm-routing/ns3-dev:3.49-debug"], 20)
    check("commands", ("OUT=reanchor-3.49-debug.tsv" in cmds[1], cmds[-1]),
          (True, "python3 tools/bench/reanchor.py propose reanchor-3.49-debug.tsv"))

    for f in fails:
        print("FAIL", f)
    print(f"reanchor.py self-test: {'ok' if not fails else f'{len(fails)} failed'}")
    return 0 if not fails else 1


def main(argv):
    if argv == ["--self-test"]:
        return self_test()
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("gates", help="every gate, its kind and its seeds")
    sp = sub.add_parser("survey", help="the oracle on seeds 1..M in one tree (TSV)")
    sp.add_argument("--ns3", required=True)
    pp = sub.add_parser("propose", help="the rule over survey files")
    pp.add_argument("surveys", nargs="+")
    rp = sub.add_parser("run", help="survey each tree, then propose")
    rp.add_argument("--ns3", action="append", required=True)
    cp = sub.add_parser("commands", help="docker lines to survey in each image")
    cp.add_argument("--image", action="append", required=True)
    for p in (sp, rp, cp):
        p.add_argument("--seeds", type=int, default=20, help="survey seeds 1..M (default 20)")
    for p in (sp, pp, rp):
        p.add_argument("--gate", action="append", help="a window gate (default: every one)")
    sp.add_argument("--out", help="write the TSV here too (default: stdout only)")
    a = ap.parse_args(argv)

    gates = load_gates()
    if a.cmd == "gates":
        for n in sorted(gates):
            g = gates[n]
            print(f"{n:24} {g['kind']:10} seeds {g['first']}-{g['first'] + g['runs'] - 1:<3} {g['harness']} {g['args']}")
        return 0
    if a.cmd == "commands":
        print("\n".join(commands(a.image, a.seeds)))
        return 0
    names = a.gate or window_gates(gates)
    bad = [n for n in names if gates.get(n, {}).get("kind") != "window"]
    if bad and a.cmd != "propose":
        raise SystemExit(f"reanchor: not a window gate (see `gates`): {' '.join(bad)}")
    if a.cmd == "propose":
        rows = []
        for path in a.surveys:
            rows += read_survey(_read(path))
    else:
        rows = []
        for ns3 in [a.ns3] if a.cmd == "survey" else a.ns3:
            if a.cmd == "survey" and a.out:
                with open(a.out, "w") as fh:
                    rows += survey(ns3, names, a.seeds, Tee(fh))
            else:
                rows += survey(ns3, names, a.seeds, sys.stdout)
        if a.cmd == "survey":
            return 0
    print("\n".join(propose(rows, gates, names)))
    return 0


class Tee:
    """Write to a file and to stdout."""

    def __init__(self, fh):
        self.fh = fh

    def write(self, s):
        self.fh.write(s)
        sys.stdout.write(s)

    def flush(self):
        self.fh.flush()
        sys.stdout.flush()


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
