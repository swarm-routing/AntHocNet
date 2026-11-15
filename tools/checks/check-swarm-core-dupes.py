#!/usr/bin/env python3
# SPDX-License-Identifier: MIT OR GPL-2.0-only
# Copyright (C) 2026 Daniel Henrique Joppi
"""List the primitives core/ still defines that swarm-core's core/ provides.

swarm-routing/ns3-swarm-core#14 moves this repository's core/ onto
swarm-core's dual-licensed core/ (org golden rule 4), slice by slice (S2-S8).
Its third acceptance criterion is "no primitive is defined in both repos".
This script is that criterion as a check: MAPPING below is the plan's table
(AntHocNet's definition -> swarm-core's counterpart, and the slice that
replaces it). For each row it reports whether core/ still defines the symbol
(a class, struct, enum, alias or constant of that name; a using-declaration
of, or a class derived from, the swarm::core one is a use, not a definition)
and, with --swarm-core, that swarm-core still defines the counterpart.

Report-only until S9, which runs it with --gate (exit 1 while any row is
still defined here). Output: one line per row, then a summary line
(ADR-0014: CI reads the verdict, not the headers).

Usage:
  check-swarm-core-dupes.py [--core core] [--swarm-core DIR] [--gate]
  check-swarm-core-dupes.py --self-test
"""

from __future__ import annotations

import argparse
import pathlib
import re
import sys
import tempfile

# (AntHocNet symbol, its file under core/, swarm-core symbol, swarm-core header, slice)
MAPPING = [
    ("NodeAddress", "include/anthocnet/core/types.h", "NodeAddress", "swarm-types.h", "S2"),
    ("Time", "include/anthocnet/core/types.h", "Time", "swarm-types.h", "S2"),
    ("kInvalidAddress", "include/anthocnet/core/types.h", "kInvalidAddress", "swarm-types.h", "S2"),
    ("IClock", "include/anthocnet/core/ports.h", "IClock", "swarm-ports.h", "S2"),
    ("IRng", "include/anthocnet/core/ports.h", "IRng", "swarm-ports.h", "S2"),
    ("INeighborProvider", "include/anthocnet/core/ports.h", "INeighborProvider", "swarm-ports.h", "S2"),
    ("ITimerScheduler", "include/anthocnet/core/ports.h", "ITimerScheduler", "swarm-ports.h", "S2"),
    ("ILinkState", "include/anthocnet/core/ports.h", "ILinkState", "swarm-ports.h", "S2"),
    ("ILinkQuality", "include/anthocnet/core/ports.h", "ILinkQuality", "swarm-ports.h", "S2"),
    ("IEnergy", "include/anthocnet/core/ports.h", "IEnergy", "swarm-ports.h", "S2"),
    ("IRouterObserver", "include/anthocnet/core/ports.h", "IRouterObserver", "swarm-observer.h", "S2"),
    ("KindName", "include/anthocnet/core/ports.h", "KindName", "swarm-observer.h", "S2"),
    ("RouteAction", "include/anthocnet/core/route_decision.h", "RouteAction", "swarm-route-decision.h", "S2"),
    ("RouteDecision", "include/anthocnet/core/route_decision.h", "RouteDecision", "swarm-route-decision.h", "S3"),
    ("AntHistoryTracker", "include/anthocnet/core/ant_history.h", "DedupHistory", "swarm-dedup-history.h", "S4"),
    ("AntHop", "include/anthocnet/core/visited_path.h", "Hop", "swarm-visited-path.h", "S5"),
    ("VisitedPath", "include/anthocnet/core/visited_path.h", "VisitedPath", "swarm-visited-path.h", "S5"),
    ("Writer", "src/ant_message_codec.cpp", "WireWriter", "swarm-wire.h", "S6"),
    ("Reader", "src/ant_message_codec.cpp", "WireReader", "swarm-wire.h", "S6"),
    ("PheromoneTable", "include/anthocnet/core/pheromone_table.h", "PheromoneTable", "swarm-pheromone-table.h", "S7"),
    ("ILinkMetric", "include/anthocnet/core/link_metric.h", "ILinkMetric", "swarm-link-metric.h", "S8"),
    ("LinkObservation", "include/anthocnet/core/link_metric.h", "LinkObservation", "swarm-link-metric.h", "S8"),
    ("HopSignals", "include/anthocnet/core/link_metric.h", "HopSignals", "swarm-link-metric.h", "S8"),
    ("EnergyMetric", "include/anthocnet/core/link_metric.h", "EnergyMetric", "swarm-energy-metric.h", "S8"),
]


def defines(text: str, name: str) -> bool:
    """True when text defines name: class/struct/enum/union, an alias, a constant.

    `using swarm::core::X;` (no '=') and `class X : public swarm::core::X` are
    uses of swarm-core's symbol, not definitions of a second one.
    """
    n = re.escape(name)
    pats = (
        rf"^\s*(?:template\s*<[^>]*>\s*)?(?:class|struct|union|enum(?:\s+class)?)\s+{n}\b(?!\s*;)",
        rf"^\s*using\s+{n}\s*=",
        rf"^\s*typedef\b[^;]*\b{n}\s*;",
        rf"^\s*(?:inline\s+)?(?:static\s+)?constexpr\b[^=;(]*\b{n}\s*(?:\[[^\]]*\]\s*)?=",
    )
    for line in text.splitlines():
        if "swarm::core::" in line:
            continue
        if any(re.search(p, line) for p in pats):
            return True
    return False


def scan(core: pathlib.Path, swarm: pathlib.Path | None) -> tuple[list[str], int, int]:
    lines, dupes, missing = [], 0, 0
    for ours, path, theirs, header, slice_ in MAPPING:
        f = core / path
        here = f.is_file() and defines(f.read_text(encoding="utf-8"), ours)
        there = ""
        if swarm is not None:
            h = swarm / header
            ok = h.is_file() and defines(h.read_text(encoding="utf-8"), theirs)
            if not ok:
                missing += 1
            there = "" if ok else "  (swarm-core counterpart NOT FOUND)"
        if here:
            dupes += 1
            lines.append(f"both:  anthocnet::core::{ours} ({path}) = swarm::core::{theirs} ({header}), {slice_}{there}")
        else:
            lines.append(f"done:  {ours} -> swarm::core::{theirs} ({slice_}){there}")
    return lines, dupes, missing


def self_test() -> int:
    ok = True

    def check(cond: bool, what: str) -> None:
        nonlocal ok
        if not cond:
            ok = False
            print(f"self-test FAIL: {what}")

    check(defines("class IClock {", "IClock"), "class definition")
    check(not defines("class IClock;", "IClock"), "forward declaration")
    check(defines("using Time = double;", "Time"), "alias")
    check(not defines("using swarm::core::Time;", "Time"), "using-declaration")
    check(not defines("class IRouterObserver : public swarm::core::IRouterObserver {", "IRouterObserver"),
          "derived from swarm-core's")
    check(defines("constexpr NodeAddress kInvalidAddress = -1;", "kInvalidAddress"), "constant")
    check(defines("enum class RouteAction {", "RouteAction"), "enum class")
    check(defines("template <typename M>\nstruct RouteDecision {", "RouteDecision"), "template struct (next line)")
    check(not defines("struct RouteDecisionX {", "RouteDecision"), "prefix name")
    with tempfile.TemporaryDirectory() as d:
        core = pathlib.Path(d, "core")
        swarm = pathlib.Path(d, "swc")
        (core / "include/anthocnet/core").mkdir(parents=True)
        (core / "src").mkdir()
        swarm.mkdir()
        (core / "include/anthocnet/core/types.h").write_text(
            "using NodeAddress = int;\nusing swarm::core::Time;\n", encoding="utf-8")
        (swarm / "swarm-types.h").write_text("using NodeAddress = int;\nusing Time = double;\n", encoding="utf-8")
        lines, dupes, missing = scan(core, swarm)
        check(dupes == 1, f"fixture: one row defined in both (got {dupes})")
        check(any(x.startswith("both:  anthocnet::core::NodeAddress") for x in lines), "fixture: NodeAddress reported")
        check(any(x.startswith("done:  Time") for x in lines), "fixture: Time reported as done")
        check(missing == len(MAPPING) - 2, f"fixture: counterparts missing (got {missing})")
    print("check-swarm-core-dupes self-test: " + ("ok" if ok else "FAIL"))
    return 0 if ok else 1


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--core", default="core", help="this repository's core/ (default: core)")
    ap.add_argument("--swarm-core", help="swarm-core's core/ directory, to check each counterpart exists")
    ap.add_argument("--gate", action="store_true", help="exit 1 while any primitive is defined in both (S9)")
    ap.add_argument("--self-test", action="store_true")
    a = ap.parse_args()
    if a.self_test:
        return self_test()
    swarm = pathlib.Path(a.swarm_core) if a.swarm_core else None
    if swarm is not None and not (swarm / "swarm-ports.h").is_file():
        print(f"check-swarm-core-dupes: {swarm} is not swarm-core's core/ directory")
        return 2
    lines, dupes, missing = scan(pathlib.Path(a.core), swarm)
    print("\n".join(lines))
    summary = f"swarm-core dupes: {dupes} of {len(MAPPING)} primitive(s) still defined in both"
    if swarm is not None:
        summary += f"; {missing} swarm-core counterpart(s) not found"
    print(summary + ("" if a.gate else " (report-only until S9)"))
    if a.gate and (dupes or missing):
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
