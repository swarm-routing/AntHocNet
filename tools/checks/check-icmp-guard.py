#!/usr/bin/env python3
# Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
# SPDX-License-Identifier: MIT OR GPL-2.0-only
"""Every AODV-, AOMDV- and GPSR-family install runs behind the ICMP guard (#843).

NS3-001: ns-3.49's stock AODV livelocks in a zero-time ICMP storm, and the
vendored AOMDV and GPSR (gpsr, gpcr, gpsr3d: one helper, attribute modes) hit
the same storm. Every install of them goes through routing-bench's
`IcmpErrorGuardHelper`, and their RNG streams are assigned through the object
aggregated to the node (`GetObject<...::RoutingProtocol>()`), because
`Ipv4::GetRoutingProtocol()` is the guard. A site that bypasses either shows
up only as a 3.49 CI timeout, so this check reads ns3/'s C++ (comments and
strings stripped) and fails on:

  1. `AodvHelper`, `AomdvHelper` or `GpsrHelper` anywhere but a declaration
     of one variable (`AodvHelper a, b;` fails: one helper per statement)
     or a temporary handed straight to `IcmpErrorGuardHelper(...)`/`{...}`;
  2. a variable of one of those types used other than wrapped in the guard,
     `.Set(...)`, or, for `GpsrHelper` only, `.Install(...)` (the position
     header, not routing) and `.AssignStreams(...)` (it reaches GPSR through
     the node). So `SetRoutingHelper(aodv)`, `list.Add(aodv, 10)`, `.Create(`
     and `AodvHelper`/`AomdvHelper::AssignStreams` (a cast of
     `GetRoutingProtocol()`) all fail;
  3. a cast to `aodv::`, `aomdv::` or `gpsr::RoutingProtocol` (`DynamicCast`,
     `StaticCast`, `dynamic_cast`): behind the guard it finds nothing, reach
     the protocol with `GetObject` instead;
  4. `CreateObject<aodv|aomdv|gpsr::RoutingProtocol>`: an install with no
     helper at all.

The helpers' own definitions (aomdv-helper.*, gpsr-helper.*) are not read. A
site that must stay unguarded (a test of the livelock itself) carries
`icmp-guard: unguarded on purpose (<reason>)` in a comment on the line, or on
the line above; a marker with no reason, or one that excuses nothing, fails.

Stdlib only.

Usage:  python3 tools/checks/check-icmp-guard.py              # the repo's ns3/
        python3 tools/checks/check-icmp-guard.py --self-test
"""

from __future__ import annotations

import os
import re
import sys

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
ROOT = "ns3"
EXCLUDE = {
    "ns3/baselines/aomdv/helper/aomdv-helper.h",
    "ns3/baselines/aomdv/helper/aomdv-helper.cc",
    "ns3/baselines/gpsr/helper/gpsr-helper.h",
    "ns3/baselines/gpsr/helper/gpsr-helper.cc",
}
EXTS = (".cc", ".h", ".cpp", ".hpp")

HELPERS = ("AodvHelper", "AomdvHelper", "GpsrHelper")
METHODS = {
    "AodvHelper": {"Set"},
    "AomdvHelper": {"Set"},
    "GpsrHelper": {"Set", "Install", "AssignStreams"},
}
# The guard's opening delimiter, then any qualifier of what it wraps
# (`ns3::AodvHelper()`, `this->m_helper`).
GUARD_OPEN = re.compile(
    r"IcmpErrorGuardHelper(\s+[A-Za-z_]\w*)?\s*[({]\s*"
    r"(?:(?:[A-Za-z_]\w*|\(\s*\*\s*this\s*\))\s*(?:::|->|\.)\s*)*$"
)
TYPE_RE = re.compile(r"\b(" + "|".join(HELPERS) + r")\b")
DECL_RE = re.compile(r"\s*(?:[&*]+\s*|\s)\s*([A-Za-z_]\w*)\s*(?=[;,={()\[])")
TEMP_IN_GUARD = re.compile(r"\s*(?:\(\s*\)|\{\s*\})\s*[)}]")
CAST_RE = re.compile(
    r"\b(?:DynamicCast|StaticCast|dynamic_cast|static_cast)\s*<\s*(?:Ptr\s*<\s*)?(?:ns3\s*::\s*)?"
    r"(aodv|aomdv|gpsr)\s*::\s*RoutingProtocol\b"
)
CREATE_RE = re.compile(
    r"\bCreateObject\s*<\s*(?:ns3\s*::\s*)?(aodv|aomdv|gpsr)\s*::\s*RoutingProtocol\b"
)
MARKER_RE = re.compile(r"icmp-guard:\s*unguarded on purpose\s*\(([^)]*)\)")
MARKER_ANY = re.compile(r"icmp-guard:")


def strip_code(text: str) -> str:
    """Blank comments, string and char literals and preprocessor lines, keeping offsets."""
    out = list(text)
    i, n = 0, len(text)

    def blank(a: int, b: int) -> None:
        for k in range(a, b):
            if out[k] != "\n":
                out[k] = " "

    at_line_start = True
    while i < n:
        c = text[i]
        if at_line_start and c == "#":
            j = i
            while j < n and text[j] != "\n":
                j += 2 if text[j] == "\\" and j + 1 < n else 1
            blank(i, j)
            i = j
            continue
        if c == "\n":
            at_line_start = True
            i += 1
            continue
        if not c.isspace():
            at_line_start = False
        if text.startswith("//", i):
            j = text.find("\n", i)
            j = n if j < 0 else j
            blank(i, j)
            i = j
        elif text.startswith("/*", i):
            j = text.find("*/", i + 2)
            j = n if j < 0 else j + 2
            blank(i, j)
            i = j
        elif c in "\"'":
            j = i + 1
            while j < n and text[j] != c and text[j] != "\n":
                j += 2 if text[j] == "\\" else 1
            j = min(j + 1, n)
            blank(i + 1, j - 1)
            i = j
        else:
            i += 1
    return "".join(out)


def ends_statement(code: str, i: int) -> int:
    """Offset of the first top-level `,` before the `;` ending the statement at i, or -1.

    -1 also when an unmatched `)` comes first: i is in a parameter list,
    whose other parameters are declarations of their own.
    """
    depth, comma = 0, -1
    while i < len(code):
        c = code[i]
        if c in "({[":
            depth += 1
        elif c in ")}]":
            if depth == 0:
                return -1
            depth -= 1
        elif c == ";" and depth == 0:
            return comma
        elif c == "," and depth == 0 and comma < 0:
            comma = i
        i += 1
    return -1


def line_of(text: str, pos: int) -> int:
    return text.count("\n", 0, pos) + 1


def check_source(path: str, text: str) -> list[str]:
    code = strip_code(text)
    found: list[tuple[int, str]] = []

    def add(pos: int, msg: str) -> None:
        found.append((line_of(code, pos), msg))

    # variable -> (allowed methods, declaration offsets); a name declared with
    # more than one helper type keeps only the methods every type allows.
    names: dict[str, tuple[set[str], set[int]]] = {}

    def declare(name: str, helper: str, pos: int) -> None:
        allowed, decls = names.setdefault(name, (set(METHODS[helper]), set()))
        allowed &= METHODS[helper]
        decls.add(pos)

    for m in TYPE_RE.finditer(code):
        helper, after = m.group(1), code[m.end() :]
        before = code[: m.start()]
        d = DECL_RE.match(after)
        if d and d.group(1) not in ("operator",):
            pos = m.end() + d.start(1)
            declare(d.group(1), helper, pos)
            # `AodvHelper a, b;`: a second declarator could hide behind any
            # initializer (`a = x < y ? f() : g(), b`), so the check asks for one
            # helper per declaration instead of parsing C++.
            comma = ends_statement(code, m.end() + d.end(1))
            if comma >= 0:
                add(
                    comma,
                    f"declare one {helper} per statement, so this check can follow each (NS3-001, #843)",
                )
            continue
        if GUARD_OPEN.search(before) and TEMP_IN_GUARD.match(after):
            continue
        add(
            m.start(),
            f"{helper} used outside routing-bench's IcmpErrorGuardHelper (NS3-001, #843)",
        )

    for name, (allowed, decls) in names.items():
        for u in re.finditer(r"(?<![\w:])" + re.escape(name) + r"\b(?!\s*::)", code):
            if u.start() in decls:
                continue
            before, after = code[: u.start()], code[u.end() :]
            # `x.name` / `x->name` is another object's member, except `this->name`
            if re.search(r"(?:\.|->)\s*$", before) and not re.search(
                r"(?:\bthis\s*->|\(\s*\*\s*this\s*\)\s*\.)\s*$", before
            ):
                continue
            if GUARD_OPEN.search(before) and re.match(r"\s*[)}]", after):
                continue
            call = re.match(r"\s*\.\s*([A-Za-z_]\w*)\s*\(", after)
            if call and call.group(1) in allowed:
                continue
            what = f".{call.group(1)}()" if call else "here"
            hint = (
                " (it casts Ipv4::GetRoutingProtocol(), the guard: assign through GetObject)"
                if call and call.group(1) == "AssignStreams"
                else ""
            )
            add(
                u.start(),
                f"{helper} '{name}' used {what} without the ICMP guard{hint} (NS3-001, #843)",
            )

    for m in CAST_RE.finditer(code):
        add(
            m.start(),
            f"cast to {m.group(1)}::RoutingProtocol: behind the ICMP guard it finds nothing, "
            f"use GetObject<{m.group(1)}::RoutingProtocol>() (NS3-001, #843)",
        )
    for m in CREATE_RE.finditer(code):
        add(
            m.start(),
            f"CreateObject<{m.group(1)}::RoutingProtocol>: installed with no ICMP guard (NS3-001, #843)",
        )

    lines = text.split("\n")
    markers: dict[int, bool] = {}  # marker line -> used
    errors: list[str] = []
    for no, line in enumerate(lines, 1):
        if MARKER_ANY.search(line):
            mm = MARKER_RE.search(line)
            if not mm or not mm.group(1).strip():
                errors.append(
                    f"{path}:{no}: icmp-guard marker needs a reason: `icmp-guard: unguarded on purpose (<reason>)`"
                )
            else:
                markers[no] = False
    for no, msg in sorted(set(found)):
        for m in (no, no - 1):
            if m in markers:
                markers[m] = True
                break
        else:
            errors.append(f"{path}:{no}: {msg}")
    for no, used in sorted(markers.items()):
        if not used:
            errors.append(
                f"{path}:{no}: icmp-guard marker excuses nothing on this line or the next; remove it"
            )
    return errors


def scan(repo: str) -> tuple[int, list[str]]:
    files, errors = 0, []
    for dirpath, dirnames, filenames in os.walk(os.path.join(repo, ROOT)):
        dirnames.sort()
        for f in sorted(filenames):
            if not f.endswith(EXTS):
                continue
            full = os.path.join(dirpath, f)
            rel = os.path.relpath(full, repo).replace(os.sep, "/")
            if rel in EXCLUDE:
                continue
            with open(full, encoding="utf-8", errors="replace") as fh:
                files += 1
                errors += check_source(rel, fh.read())
    return files, errors


GOOD = """
#include "ns3/aodv-helper.h"  // AodvHelper in a comment: ignored
int64_t AssignAodvStreams(const NodeContainer& nodes, int64_t stream) {
    Ptr<aodv::RoutingProtocol> aodv = (*it)->GetObject<aodv::RoutingProtocol>();
    return aodv->AssignStreams(stream);
}
void Run() {
    const char* s = "SetRoutingHelper(AodvHelper())";
    bench::IcmpErrorGuardHelper aodvHelper{AodvHelper()};  // NS3-001 (#843)
    internet.SetRoutingHelper(aodvHelper);
    internet.SetRoutingHelper(bench::IcmpErrorGuardHelper(AomdvHelper()));
    GpsrHelper gpsrHelper;
    gpsrHelper.Set("ThreeD", BooleanValue(true));
    internet.SetRoutingHelper(bench::IcmpErrorGuardHelper(gpsrHelper));
    internet.Install(all);
    gpsrHelper.Install(all);
    stream += gpsrHelper.AssignStreams(all, stream);
    bench::IcmpErrorGuardHelper qualified{ns3::AodvHelper()};
    AomdvHelper first;
    AomdvHelper second{};
    list.Add(bench::IcmpErrorGuardHelper(first), 10);
    list.Add(bench::IcmpErrorGuardHelper(second), 5);
}
void f(int a, int b) {}
void g(const GpsrHelper& helper, int n) { helper.Install(nodes); }
struct Arm {
    GpsrHelper m_helper;
    void Install() { internet.SetRoutingHelper(bench::IcmpErrorGuardHelper(this->m_helper)); }
    void Wrap() { list.Add(bench::IcmpErrorGuardHelper((*this).m_helper), 1); }
    void Other(Run& run) { run.m_helper = 1; }
};
void Elsewhere() {
    GpsrHelper gpsrHelper;
    internet.SetRoutingHelper(bench::IcmpErrorGuardHelper(gpsrHelper));
}
"""

BAD = {
    "direct install": "AodvHelper aodv;\ninternet.SetRoutingHelper(aodv);\n",
    "direct temporary": "internet.SetRoutingHelper(AomdvHelper());\n",
    "qualified temporary": "internet.SetRoutingHelper(ns3::AodvHelper());\n",
    "second declarator": (
        "AodvHelper guarded, unguarded;\nbench::IcmpErrorGuardHelper guard(guarded);\n"
        "internet.SetRoutingHelper(unguarded);\n"
    ),
    "declarator after an initializer": (
        "AodvHelper a{}, b;\ninternet.SetRoutingHelper(bench::IcmpErrorGuardHelper(a));\n"
        "internet.SetRoutingHelper(b);\n"
    ),
    "comma in a comparison": (
        "AodvHelper guarded = flag < 1 ? makeA() : makeB(), unguarded;\n"
        "bench::IcmpErrorGuardHelper guard(guarded);\ninternet.SetRoutingHelper(unguarded);\n"
    ),
    "comma in a template argument": (
        "AodvHelper guarded = make<A, B>(), unguarded;\n"
        "bench::IcmpErrorGuardHelper guard(guarded);\ninternet.SetRoutingHelper(unguarded);\n"
    ),
    "this-> member": (
        "struct A {\n  AodvHelper m_helper;\n"
        "  void I() { internet.SetRoutingHelper(this->m_helper); }\n};\n"
    ),
    "list routing": "Ipv4ListRoutingHelper l;\nGpsrHelper g;\nl.Add(g, 10);\n",
    "helper create": "AomdvHelper h;\nPtr<Ipv4RoutingProtocol> p = h.Create(node);\n",
    "AodvHelper::AssignStreams": (
        "AodvHelper aodv;\ninternet.SetRoutingHelper(bench::IcmpErrorGuardHelper(aodv));\n"
        "stream += aodv.AssignStreams(nodes, stream);\n"
    ),
    "cast through GetRoutingProtocol": (
        "Ptr<aomdv::RoutingProtocol> p =\n    DynamicCast<aomdv::RoutingProtocol>(ipv4->GetRoutingProtocol());\n"
    ),
    "CreateObject": "auto p = CreateObject<aodv::RoutingProtocol>();\n",
    "marker without a reason": "// icmp-guard: unguarded on purpose ()\ninternet.SetRoutingHelper(AodvHelper());\n",
    "marker excusing nothing": "// icmp-guard: unguarded on purpose (stale)\nint x = 0;\n",
}

ALLOWED = (
    "// icmp-guard: unguarded on purpose (this test shows the 3.49 livelock itself)\n"
    "internet.SetRoutingHelper(AodvHelper());\n"
    "internet.SetRoutingHelper(GpsrHelper());  // icmp-guard: unguarded on purpose (same, GPSR)\n"
)


def self_test() -> int:
    fails = 0
    got = check_source("good.cc", GOOD)
    if got:
        print("self-test FAIL: the guarded fixture reports:\n  " + "\n  ".join(got))
        fails += 1
    for name, src in BAD.items():
        if not check_source("bad.cc", src):
            print(f"self-test FAIL: '{name}' passes")
            fails += 1
    got = check_source("allowed.cc", ALLOWED)
    if got:
        print("self-test FAIL: the marked fixture reports:\n  " + "\n  ".join(got))
        fails += 1
    print(
        f"check-icmp-guard self-test: {'FAIL' if fails else 'ok'} ({len(BAD) + 2} cases)"
    )
    return 1 if fails else 0


def main(argv: list[str]) -> int:
    if argv[1:] == ["--self-test"]:
        return self_test()
    if argv[1:]:
        print(__doc__.split("Usage:")[1].rstrip(), file=sys.stderr)
        return 2
    files, errors = scan(REPO)
    for e in errors:
        print(e)
    if errors:
        print(
            f"check-icmp-guard: {len(errors)} site(s) bypass routing-bench's ICMP error guard "
            "(NS3-001, #843; AGENTS.md 'Build & verify')"
        )
        return 1
    print(
        f"check-icmp-guard: ok ({files} files under {ROOT}/; every AODV/AOMDV/GPSR install is guarded)"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
