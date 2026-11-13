#!/usr/bin/env python3
# SPDX-License-Identifier: MIT OR GPL-2.0-only
# Copyright (C) 2026 Daniel Henrique Joppi
"""docs/wire-format.md names the wire version the codec emits (#981).

Golden rule 4: a wire-format change bumps kWireVersion and updates the layout
table in docs/wire-format.md. The 0x04 bump (#573) changed the codec, the ns-3
header and the tests, and the page stayed at 0x03. This fails when the page's
current version is not the codec's:

  * the status note's `kWireVersion = 0xNN`;
  * the `version` row's "currently `0xNN`";
  * the version history has a "(landed, 0xNN)" step for the codec's value.

Stdlib only.

Usage:  python3 tools/checks/check-wire-doc.py              # the repo
        python3 tools/checks/check-wire-doc.py --self-test
"""
from __future__ import annotations

import os
import re
import sys

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
CODEC = "core/include/anthocnet/core/ant_message_codec.h"
DOC = "docs/wire-format.md"


def code_version(header: str) -> int | None:
    m = re.search(r"^constexpr\s+std::uint8_t\s+kWireVersion\s*=\s*(0x[0-9A-Fa-f]+|\d+)\s*;", header, re.MULTILINE)
    return int(m.group(1), 0) if m else None


def check(header: str, doc: str) -> list[str]:
    ver = code_version(header)
    if ver is None:
        return [f"{CODEC}: no `constexpr std::uint8_t kWireVersion = ...;` found"]
    want = f"0x{ver:02X}"
    errs = []
    places = [
        ("the status note (`kWireVersion = 0xNN`)", r"`kWireVersion = (0x[0-9A-Fa-f]{2})`"),
        ("the version row (currently `0xNN`)", r"currently `(0x[0-9A-Fa-f]{2})`"),
    ]
    for name, pat in places:
        got = re.findall(pat, doc)
        if not got:
            errs.append(f"{DOC}: {name} is missing")
        for g in got:
            if int(g, 16) != ver:
                errs.append(f"{DOC}: {name} says {g}, {CODEC} says kWireVersion = {want}")
    landed = {int(g, 16) for g in re.findall(r"\(landed, (0x[0-9A-Fa-f]{2})\)", doc)}
    if ver not in landed:
        errs.append(f'{DOC}: the version history has no "(landed, {want})" step')
    return errs


def self_test() -> int:
    header = "/// 0x04: + kinematics.\nconstexpr std::uint8_t kWireVersion = 0x04;\n"
    doc = ("> `kWireVersion = 0x04`. All landed.\n| 0 | `version` | (`kWireVersion`, currently `0x04`). |\n"
           "3. **slim (landed, 0x03)**\n4. **kinematics (landed, 0x04)**\n")
    cases = [
        ("code and page agree", header, doc, 0),
        ("the page's version row is one behind", header, doc.replace("currently `0x04`", "currently `0x03`"), 1),
        ("the page's status note is one behind", header, doc.replace("= 0x04`", "= 0x03`"), 1),
        ("the page is wholly at the old version", header.replace("= 0x04;", "= 0x05;"), doc, 3),
        ("the history lacks the current step", header, doc.replace("(landed, 0x04)", "(planned)"), 1),
        ("the version row is gone", header, doc.replace("currently `0x04`", "see the codec"), 1),
        ("the constant is not in the header", "// nothing\n", doc, 1),
    ]
    bad = 0
    for name, h, d, want in cases:
        n = len(check(h, d))
        ok = n == want
        print(f"{'ok  ' if ok else 'FAIL'} {name}: {n} finding(s), want {want}")
        bad += not ok
    print(f"{len(cases) - bad}/{len(cases)} cases passed")
    return 1 if bad else 0


def main() -> int:
    if "--self-test" in sys.argv:
        return self_test()
    with open(os.path.join(REPO, CODEC), encoding="utf-8") as f:
        header = f.read()
    with open(os.path.join(REPO, DOC), encoding="utf-8") as f:
        doc = f.read()
    errs = check(header, doc)
    for e in errs:
        print(f"FAIL {e}")
    if errs:
        return 1
    print(f"ok: {DOC} is at kWireVersion = 0x{code_version(header):02X}, as {CODEC}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
