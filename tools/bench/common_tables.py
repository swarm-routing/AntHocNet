#!/usr/bin/env python3
# SPDX-License-Identifier: MIT OR GPL-2.0-only
# Copyright (C) 2026 Daniel Henrique Joppi
"""The oracle's matched-set tables from a cell's ##COMMON## rows (#460).

grid.md's matched-hop and matched-latency tables, recomputed by script from
the committed cells rather than by hand (ADR-0014):

    python3 tools/bench/common_tables.py results/cells/grid-*-g460.txt

Per cell, over the seeds where every arm and the oracle wrote a row:
  - each arm's and the oracle's mean hopsC;
  - the hop bound: on how many seeds the oracle's hopsC is <= the arm's
    (strict, no tolerance; scenario_check.py's gate adds 0.05);
  - oracle - arm hopsC, the per-seed paired mean and its 95 % t interval;
  - meanC and p99C, anthocnet - oracle, paired the same way, and on how many
    seeds anthocnet is the faster.

##COMMON## <run> <proto> <nSelf> <nCommon> <p99C> <meanC> <p99S> <hopsC> <hopsS>
The matched set is the intersection over every arm in the run, so a cell's
numbers are only comparable with a cell run with the same arms.
"""

import argparse
import os
import re
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
import ns3_tools  # the readers pinned in ns3-tools.lock

ROW = re.compile(
    r"^\s*##COMMON##\s+(\d+)\s+([a-z][\w-]*)\s+\d+\s+(\d+)\s+(\S+)\s+(\S+)\s+\S+\s+(\S+)"
)


def mean(xs):
    return sum(xs) / len(xs)


def paired(su, diffs):
    m, hw = su.t_ci(diffs)
    if hw is None:
        return f"{m:+.3f}"
    return f"{m:+.3f} [{m - hw:+.3f}, {m + hw:+.3f}]"


def read(path):
    rows = {}
    with open(path) as f:
        for line in f:
            m = ROW.match(line)
            if m:
                run, proto, n, p99, meanc, hops = m.groups()
                rows.setdefault(int(run), {})[proto] = {
                    "n": int(n),
                    "p99": float(p99),
                    "meanc": float(meanc),
                    "hops": float(hops),
                }
    return rows


def table(su, path, arms, ref, lead):
    rows = read(path)
    seeds = sorted(r for r in rows if all(p in rows[r] for p in arms + [ref]))
    out = [f"== {os.path.basename(path)}: {len(seeds)} seeds"]
    if not seeds:
        return out + ["  no seed carries a ##COMMON## row for every arm"]
    out.append(f"  nCommon mean {mean([rows[s][ref]['n'] for s in seeds]):.0f}")
    out.append(
        "  hopsC  "
        + "  ".join(f"{p} {mean([rows[s][p]['hops'] for s in seeds]):.3f}" for p in arms + [ref])
    )
    out.append(
        f"  {ref}<=arm seeds  "
        + "  ".join(
            f"{p} {sum(rows[s][ref]['hops'] <= rows[s][p]['hops'] for s in seeds)}/{len(seeds)}"
            for p in arms
        )
    )
    for p in arms:
        d = [rows[s][ref]["hops"] - rows[s][p]["hops"] for s in seeds]
        out.append(f"  {ref}-{p} hopsC  {paired(su, d)}")
    for k, lab in (("meanc", "meanC"), ("p99", "p99C")):
        a = [rows[s][lead][k] for s in seeds]
        o = [rows[s][ref][k] for s in seeds]
        d = [x - y for x, y in zip(a, o)]
        out.append(
            f"  {lab}  {lead} {mean(a):.1f}  {ref} {mean(o):.1f}  "
            f"{lead}-{ref} {paired(su, d)}  {lead} faster {sum(x < 0 for x in d)}/{len(d)}"
        )
    return out


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("cells", nargs="+")
    ap.add_argument("--arms", default="anthocnet,aodv,olsr,dsdv")
    ap.add_argument("--ref", default="oracle")
    ap.add_argument("--lead", default="anthocnet")
    a = ap.parse_args(argv)
    su = ns3_tools.module("stats_util")
    for path in a.cells:
        print("\n".join(table(su, path, a.arms.split(","), a.ref, a.lead)))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
