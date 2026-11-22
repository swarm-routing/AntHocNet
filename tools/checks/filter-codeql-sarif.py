#!/usr/bin/env python3
# Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
# SPDX-License-Identifier: MIT OR GPL-2.0-only
"""Drop reviewed CodeQL results from a SARIF file before it is uploaded.

CodeQL's own `query-filters` exclude a rule everywhere; they cannot be scoped
to a path. This keeps the exclusion as narrow as the review was: a result is
dropped only when its rule is in RULES *and* its primary location is under
PATH_PREFIX. The same rules elsewhere (core/src/ above all) still upload and
still raise an alert.

The two rules and why they are noise in core/tests/ (maintainer decision
2026-11-22; evidence per alert on the `security` tickets, summary on
https://github.com/swarm-routing/.github/issues/629#issuecomment-7083133901):

  cpp/equality-on-floats      test asserts of values that are exact by
                              construction (a Config default against the
                              literal it is initialised with, a literal or
                              sentinel returned from a branch, sums of integer
                              weights, one bit-identity determinism check);
                              a tolerance would weaken the test.
  cpp/unused-static-function  `fieldCount` in core/tests/config_dump.cpp is
                              used only in a static_assert, which CodeQL does
                              not count as a use.

Usage:
  filter-codeql-sarif.py FILE.sarif [...]   rewrite each file in place
  filter-codeql-sarif.py --self-test
"""

from __future__ import annotations

import json
import sys
import tempfile
from pathlib import Path

RULES = frozenset({"cpp/equality-on-floats", "cpp/unused-static-function"})
PATH_PREFIX = "core/tests/"


def _uri(result: dict) -> str:
    locs = result.get("locations") or []
    if not locs:
        return ""
    # CodeQL writes repository-relative URIs (uriBaseId %SRCROOT%).
    return locs[0].get("physicalLocation", {}).get("artifactLocation", {}).get("uri", "")


def _rule(result: dict) -> str:
    if result.get("ruleId"):
        return result["ruleId"]
    return result.get("rule", {}).get("id", "")


def dropped(result: dict) -> bool:
    return _rule(result) in RULES and _uri(result).startswith(PATH_PREFIX)


def filter_sarif(doc: dict) -> int:
    """Remove the reviewed results from doc in place; return how many."""
    n = 0
    for run in doc.get("runs", []):
        results = run.get("results") or []
        kept = [r for r in results if not dropped(r)]
        n += len(results) - len(kept)
        run["results"] = kept
    return n


def filter_file(path: Path) -> int:
    doc = json.loads(path.read_text(encoding="utf-8"))
    n = filter_sarif(doc)
    path.write_text(json.dumps(doc), encoding="utf-8")
    return n


def _res(rule: str, uri: str) -> dict:
    return {
        "ruleId": rule,
        "locations": [{"physicalLocation": {"artifactLocation": {"uri": uri}}}],
    }


def self_test() -> int:
    doc = {
        "runs": [
            {
                "results": [
                    _res("cpp/equality-on-floats", "core/tests/test_config_defaults.cpp"),
                    _res("cpp/unused-static-function", "core/tests/config_dump.cpp"),
                    _res("cpp/equality-on-floats", "core/src/shortest_path.cpp"),
                    _res("cpp/large-parameter", "core/tests/test_properties.cpp"),
                    {"rule": {"id": "cpp/equality-on-floats"}, "locations": []},
                ]
            }
        ]
    }
    with tempfile.TemporaryDirectory() as tmp:
        p = Path(tmp) / "cpp.sarif"
        p.write_text(json.dumps(doc), encoding="utf-8")
        n = filter_file(p)
        kept = json.loads(p.read_text(encoding="utf-8"))["runs"][0]["results"]
    uris = [(_rule(r), _uri(r)) for r in kept]
    expect = [
        ("cpp/equality-on-floats", "core/src/shortest_path.cpp"),
        ("cpp/large-parameter", "core/tests/test_properties.cpp"),
        ("cpp/equality-on-floats", ""),
    ]
    if n != 2 or uris != expect:
        print(f"self-test FAILED: dropped {n}, kept {uris}", file=sys.stderr)
        return 1
    print("self-test ok: 2 dropped (core/tests/ only), core/src/ and other rules kept")
    return 0


def main(argv: list[str]) -> int:
    if argv == ["--self-test"]:
        return self_test()
    if not argv:
        print(__doc__, file=sys.stderr)
        return 2
    for name in argv:
        n = filter_file(Path(name))
        print(
            f"{name}: dropped {n} reviewed result(s) under {PATH_PREFIX} ({', '.join(sorted(RULES))})"
        )
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
