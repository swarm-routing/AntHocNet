#!/usr/bin/env python3
# SPDX-License-Identifier: MIT OR GPL-2.0-only
# Copyright (C) 2026 Daniel Henrique Joppi
"""The oracle gap decomposition from a cell's ##RUN## rows (#460).

grid.md's "Gap decomposition" table, recomputed by script from the committed
cells rather than by hand (ADR-0014):

    python3 tools/bench/gap_decomposition.py results/cells/grid-*-g460d.txt

With an oracle arm that carries no routing overhead, an arm's shortfall to
100 % PDR splits as

    channel = 100 - oracle_pdr        what the oracle reference did not deliver
    routing = oracle_pdr - arm_pdr    what this arm lost that the oracle did not
    total   = 100 - arm_pdr           = channel + routing

Per cell, over the seeds where every arm and the oracle wrote a ##RUN## row:
the oracle's mode and radius (from ##ORACLE##), its mean PDR with the 95 % t
interval, and per arm the mean total, channel and routing terms, routing's
per-seed paired 95 % t interval, and the routing share (routing / total, of
the means). Each term is the mean of its per-seed values, rounded on its own.

The decomposition is only as exact as the oracle's adjacency: on a fading or
two-ray channel no disk is exact (approx=1), so the oracle is a reference and
the channel term is "what this reference could not deliver", not "what no
router could deliver" (metrics.md).
"""

import argparse
import os
import re
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
import ns3_tools  # the readers pinned in ns3-tools.lock

ORACLE = re.compile(r"^\s*##ORACLE##\s+\d+\s+\S+\s+mode=(\S+)\s.*\brange=(\S+)")


def mean(xs):
    return sum(xs) / len(xs)


def ci(su, xs):
    m, hw = su.t_ci(xs)
    if hw is None:
        return f"{m:.2f}"
    return f"{m:.2f} [{m - hw:.2f}, {m + hw:.2f}]"


def table(bp, su, path, arms, ref):
    with open(path) as f:
        text = f.read()
    runs = bp.parse_runs(text)
    modes = sorted({m.groups() for m in map(ORACLE.match, text.splitlines()) if m})
    out = [f"== {os.path.basename(path)}"]
    have = [set(runs.get(p, {})) for p in arms + [ref]]
    seeds = sorted(set.intersection(*have)) if all(have) else []
    if not seeds:
        return out + [f"  no seed carries a ##RUN## row for every arm and {ref}"]
    mode = ", ".join(f"{m} {r} m" for m, r in modes) or "unknown"
    o = [runs[ref][s]["pdr"] for s in seeds]
    out.append(f"  {len(seeds)} seeds  {ref} mode {mode}  pdr {ci(su, o)}")
    for p in arms:
        a = [runs[p][s]["pdr"] for s in seeds]
        total = mean([100.0 - x for x in a])
        chan = mean([100.0 - x for x in o])
        rout = [y - x for x, y in zip(a, o)]
        share = 100.0 * mean(rout) / total if total else float("nan")
        out.append(
            f"  {p:<10} total {total:.2f}  channel {chan:.2f}  "
            f"routing {ci(su, rout)}  share {share:.1f} %"
        )
    return out


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("cells", nargs="+")
    ap.add_argument("--arms", default="anthocnet,aodv")
    ap.add_argument("--ref", default="oracle")
    a = ap.parse_args(argv)
    bp = ns3_tools.module("bench_parse")
    su = ns3_tools.module("stats_util")
    for path in a.cells:
        print("\n".join(table(bp, su, path, a.arms.split(","), a.ref)))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
