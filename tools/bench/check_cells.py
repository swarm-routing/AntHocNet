#!/usr/bin/env python3
# SPDX-License-Identifier: MIT OR GPL-2.0-only
# Copyright (C) 2026 Daniel Henrique Joppi
"""Every committed cell reads strictly (#976).

A cell under results/cells/ is the evidence its published numbers are computed
from, and it is never edited once committed (ADR-0013). This check fails a cell
that a strict reader (swarm-routing/ns3-tools#146) would reject, or that any
reader could misread:

  splice  a line starting ``##`` that does not start with one complete
          ``##NAME##`` token, or carries a second one: another writer's line
          cut into it (the retired paper-benchmark.yml memory sampler's
          ``##RSS##``, #268), or a ``##RSS##`` line anywhere
  run     a ``##RUN##`` line that is not ``<run> <arm> <fields>``, or a field
          that is not a number (``inf``/``nan`` allowed)
  bench   a ``##BENCH##`` field that is not a number
  stddev  a ``# stddev`` value that is not a number
  dup     one ``##NAME## <seed> <arm>`` row twice: a reader keeps one of them
          silently (``bench_parse`` keeps the last)
  seeds   arms whose ``##RUN##`` seed sets differ: a seed lost to one arm
          breaks every paired comparison against it

Legacy cells (LEGACY below) carry the splices paper-benchmark.yml wrote between
#268 and its removal (#758). They pass only as recorded: exactly the listed
lines are spliced, every other check passes, and each spliced line is
redundant, i.e. every row it can have been cut from (its marker, seed and arm,
read from the cut text or, when the cut is too short, from the neighbouring
rows) is present exactly once intact, and at least one of them begins with
the cut text. A listed cell that turns clean fails too, so the list stays
true.

Usage:
    check_cells.py [CELL ...]            default: results/cells/*.txt
    check_cells.py --inventory CELL ...  one row per spliced line (#976)

Exit 1 on any failure. Standard library only.
"""

import argparse
import glob
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

# Spliced lines of the cells paper-benchmark.yml produced while its memory
# sampler wrote ##RSS## into the job log (#268 .. #758). Every one has an
# intact copy in the same cell; the inventory and the effect on every published
# number are on swarm-routing/AntHocNet#976.
LEGACY = {
    "fanet-main-cbr521.txt": (196, 369, 652, 1088),
    "fanet-main-oracle100ms.txt": (62, 117),
    "fanet-main.txt": (62, 117),
    "grid-gaussmarkov-nakagami.txt": (115, 143),
    "grid-gaussmarkov-tworay-cbr521.txt": (241, 450),
    "grid-gaussmarkov-tworay.txt": (88, 98, 117),
    "grid-rwp-nakagami-cbr521.txt": (236,),
    "grid-rwp-nakagami.txt": (58, 106, 129, 144),
    "grid-rwp-tworay-cbr521.txt": (243,),
    "grid-rwp-tworay.txt": (108, 147),
    "grid-ssrwp-nakagami.txt": (58, 106, 129, 144),
    "grid-ssrwp-tworay.txt": (54, 133),
    "static-mesh-cbr521.txt": (476,),
    "static-mesh-jitter-cap1s.txt": (45,),
    "vanet-main.txt": (1358,),
    "vanet-sparse.txt": (360, 935, 1093),
}

TOKEN = re.compile(r"(?=(##[A-Z][A-Z0-9_]*##))")  # overlapping: "##R##RSS##" has two
RUN = re.compile(r"^##RUN##\s+(\d+)\s+([A-Za-z][\w-]*)\s+(.+?)\s*$")
RUN_FIELDS = ["pdr", "delay", "delay99", "thrput", "nrl", "jitter", "off50", "off90", "nrl_bytes"]
ROW = re.compile(r"^##([A-Z][A-Z0-9_]*)##\s+(\d+)\s+([A-Za-z][\w-]*)(?:\s|$)")
COMMENT_KEY = re.compile(r"^#\s+\w+\s+([A-Za-z][\w-]*)\s+seed=(\d+)\b")  # "# diag aodv seed=5"
HEAD = re.compile(r"^##([A-Z0-9_]*)(#{0,2})(?:\s+(\d+)(?:\s+([A-Za-z][\w-]*))?)?")


def number(tok):
    """True when tok is a number as the readers parse it (inf/nan included)."""
    try:
        float(tok)
    except ValueError:
        return False
    return True


def splice_at(line):
    """Where another line was cut into this one, or None for a sound line."""
    if "##RSS##" in line:
        return line.index("##RSS##")
    if not line.startswith("##"):
        return None
    starts = [m.start() for m in TOKEN.finditer(line)]
    if not starts or starts[0] != 0:
        return 0
    return starts[1] if len(starts) > 1 else None


def field_findings(lineno, line):
    """The run / bench / stddev findings of one sound line."""
    out = []
    if line.startswith("##RUN##"):
        m = RUN.match(line)
        if not m:
            return [f"{lineno}: run: not '<run> <arm> <fields>': {line[:60]!r}"]
        for name, tok in zip(RUN_FIELDS, m.group(3).split()):
            if not number(tok):
                out.append(f"{lineno}: run: {m.group(2)} seed {m.group(1)} {name}={tok!r}")
    elif line.startswith("##BENCH##"):
        toks = line.split()[1:]
        out += [f"{lineno}: bench: {toks[0]} field {i} = {t!r}"
                for i, t in enumerate(toks[1:], 1) if not number(t)]
    elif re.match(r"^#\s+stddev\s", line):
        for tok in line.split()[3:]:
            k, eq, v = tok.partition("=")
            if eq and not number(v):
                out.append(f"{lineno}: stddev: {line.split()[2]} {k}={v!r}")
    return out


def read(path):
    with open(path, encoding="utf-8", errors="replace") as f:
        return f.read().splitlines()


def rows(lines, skip):
    """{(marker, seed, arm): [line numbers]} over the sound lines."""
    out = {}
    for i, line in enumerate(lines, 1):
        if i in skip:
            continue
        m = ROW.match(line)
        if m:
            out.setdefault((m.group(1), int(m.group(2)), m.group(3)), []).append(i)
    return out


def context(lines, i, skip):
    """The (seed, arm) of the nearest sound row before and after line i."""
    keys = []
    for rng in (range(i - 1, 0, -1), range(i + 1, len(lines) + 1)):
        for j in rng:
            if j in skip:
                continue
            m = ROW.match(lines[j - 1])
            c = COMMENT_KEY.match(lines[j - 1])
            if m:
                keys.append((int(m.group(2)), m.group(3)))
                break
            if c:
                keys.append((int(c.group(2)), c.group(1)))
                break
    return keys


def identify(lines, i, cut, skip, table):
    """Every row a spliced line can have been cut from, and its intact copies.

    Returns (candidates, copies): candidates are the (marker, seed, arm) keys
    the cut text and its neighbours allow and the cell carries for that arm;
    copies are the sound lines among them that begin with the cut text.
    """
    head = lines[i - 1][:cut]
    m = HEAD.match(head)
    name, closed, seed, arm = m.groups() if m else ("", "", None, None)
    complete_arm = arm is not None and len(head) > m.end() and head[m.end()].isspace()
    markers = {k[0] for k in table}
    names = [name] if closed == "##" else [n for n in markers if n.startswith(name)]
    if seed is not None and arm is not None:
        keys = [(int(seed), a) for a in {k[2] for k in table} if a == arm or
                (not complete_arm and a.startswith(arm))]
    elif seed is not None:
        keys = [(int(seed), a) for a in {k[2] for k in table}]
    else:
        keys = context(lines, i, skip)
    arms_of = {}
    for mk, _s, a in table:
        arms_of.setdefault(mk, set()).add(a)
    cands = sorted({(n, s, a) for n in names for s, a in keys if a in arms_of.get(n, ())})
    copies = [j for c in cands for j in table.get(c, []) if lines[j - 1].startswith(head)]
    return cands, sorted(copies)


def check(path):
    """The findings for one cell (empty when it passes)."""
    lines = read(path)
    base = os.path.basename(path)
    spliced = {i: c for i, line in enumerate(lines, 1)
               if (c := splice_at(line)) is not None}
    out = []
    listed = LEGACY.get(base)
    if listed is not None and set(listed) != set(spliced):
        out.append(f"legacy (#976): spliced lines {sorted(spliced)} != listed {sorted(listed)}")
    for i, cut in sorted(spliced.items()):
        if listed is None or i not in listed:
            out.append(f"{i}: splice: {lines[i - 1][:cut + 12]!r}")
    for i, line in enumerate(lines, 1):
        if i not in spliced:
            out += field_findings(i, line)
    table = rows(lines, spliced)
    out += [f"{ls[1]}: dup: {k[0]} seed {k[1]} {k[2]} also on line {ls[0]}"
            for k, ls in sorted(table.items()) if len(ls) > 1]
    seeds = {}
    for (mk, s, a) in table:
        if mk == "RUN":
            seeds.setdefault(a, set()).add(s)
    if len({frozenset(v) for v in seeds.values()}) > 1:
        ref = set.union(*seeds.values())
        out += [f"seeds: {a} lacks ##RUN## seed(s) {sorted(ref - v)}"
                for a, v in sorted(seeds.items()) if ref - v]
    if listed is not None:
        for i in sorted(set(listed) & set(spliced)):
            cands, copies = identify(lines, i, spliced[i], spliced, table)
            missing = [c for c in cands if len(table.get(c, [])) != 1]
            if not copies or missing:
                out.append(f"{i}: legacy splice has no single intact copy "
                           f"(candidates {cands}, copies {copies})")
    return out


def lenient_read(line, cut):
    """What a lenient bench_parse (pre-#146) made of a spliced line."""
    if not line.startswith("##RUN##"):
        return "not read by bench_parse"
    m = RUN.match(line)
    if not m:
        return "row skipped"
    kept = [n for n, t in zip(RUN_FIELDS, m.group(3).split()) if number(t)]
    if "pdr" not in kept:
        return "row skipped"
    lost = [n for n in RUN_FIELDS if n not in kept]
    return f"kept {len(kept)}/{len(RUN_FIELDS)} fields, lost {','.join(lost)}" if lost else "kept whole"


def inventory(paths):
    print("| cell | line | cut text | row(s) it can be | lenient bench_parse read | intact copy |")
    print("|---|---|---|---|---|---|")
    for path in paths:
        lines = read(path)
        spliced = {i: c for i, line in enumerate(lines, 1)
                   if (c := splice_at(line)) is not None}
        table = rows(lines, spliced)
        for i, cut in sorted(spliced.items()):
            cands, copies = identify(lines, i, cut, spliced, table)
            who = ", ".join(f"{m} {s} {a}" for m, s, a in cands) or "?"
            head = lines[i - 1][:cut]
            shown = head if len(head) <= 40 else head[:37] + "..."
            copy = ", ".join(f"line {j}" for j in copies) or "**none**"
            print(f"| `{os.path.basename(path)}` | {i} | `{shown}` | {who} | "
                  f"{lenient_read(lines[i - 1], cut)} | {copy} |")


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("cells", nargs="*")
    ap.add_argument("--inventory", action="store_true",
                    help="print one table row per spliced line instead of checking")
    args = ap.parse_args(argv)
    paths = args.cells or sorted(glob.glob(os.path.join(ROOT, "results", "cells", "*.txt")))
    if args.inventory:
        inventory(paths)
        return 0
    failed = 0
    legacy = 0
    for path in paths:
        found = check(path)
        if found:
            failed += 1
            print(f"FAIL {path}")
            for f in found:
                print(f"  {f}")
        elif os.path.basename(path) in LEGACY:
            legacy += 1
    print(f"check_cells: {len(paths)} cell(s), {failed} failed, "
          f"{legacy} legacy cell(s) with recorded redundant splices (#976)")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
