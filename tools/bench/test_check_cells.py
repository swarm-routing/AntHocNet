#!/usr/bin/env python3
# SPDX-License-Identifier: MIT OR GPL-2.0-only
# Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
"""Self-test for check_cells.py (#976): a sound cell passes; a spliced line,
a malformed ##RUN##/##BENCH##/# stddev field, a duplicated row and an arm with
a missing seed each fail; a legacy cell passes only while each recorded splice
has its intact copy, and fails once its list is wrong. Then every committed
cell passes."""
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import check_cells

SOUND = """# label line that mentions ##RUN## and ##MATCH## in prose
##CONFIG## scenario=paper runs=2 firstRun=1 protocols=anthocnet,aodv
##RUN## 1 anthocnet 99.0 3.0 10.5 13.0 4.5 1.0 2.5 inf 7.1
##AIR## 1 anthocnet 1.0 2.0 3.0 4.0
##RUN## 1 aodv 97.0 14.0 215.5 12.8 17.5 9.0 2.5 inf 9.9
##RUN## 2 anthocnet 98.0 3.1 11.5 13.0 4.6 1.1 2.5 inf 7.2
##AIR## 2 anthocnet 1.0 2.0 3.0 4.0
##RUN## 2 aodv 96.0 15.0 216.5 12.7 17.6 9.1 2.5 inf 9.8
# stddev anthocnet pdr=0.71 delay=0.1 delay99=0.7 nrl=0.07
# stddev aodv pdr=0.71 delay=0.7 delay99=0.7 nrl=0.07
##BENCH## anthocnet 98.5 3.0 11.0 13.0 4.55 1.05 2.5 inf 7.15
##BENCH## aodv 96.5 14.5 216.0 12.8 17.55 9.05 2.5 inf 9.85
##PROV## commit=aaaa run_id=1
"""
RSS = "##RSS## t=900s top=ns3.42-anthocne pid=457 rss_kb=110728 mem_avail_kb=15226132"

failures = 0


def expect(cond, msg):
    global failures
    if not cond:
        failures += 1
        print(f"FAIL: {msg}")


def findings(text, name="cell.txt", legacy=None):
    with tempfile.TemporaryDirectory() as d:
        path = os.path.join(d, name)
        with open(path, "w", encoding="utf-8") as f:
            f.write(text)
        saved = dict(check_cells.LEGACY)
        if legacy is not None:
            check_cells.LEGACY[name] = legacy
        try:
            return check_cells.check(path)
        finally:
            check_cells.LEGACY.clear()
            check_cells.LEGACY.update(saved)


def replace(text, old, new):
    assert old in text, old
    return text.replace(old, new, 1)


expect(findings(SOUND) == [], f"a sound cell passes: {findings(SOUND)}")

# The #976 shapes: a row cut by ##RSS## mid-marker, mid-row, mid-field.
for cut in ("##RUN## 2 ao", "##RUN## 2 aodv 96.0 15.", "##A", "##AIR#"):
    full = "##RUN## 2 aodv 96.0 15.0 216.5 12.7 17.6 9.1 2.5 inf 9.8" if cut.startswith("##R") \
        else "##AIR## 2 anthocnet 1.0 2.0 3.0 4.0"
    bad = replace(SOUND, full, cut + RSS)
    got = findings(bad)
    expect(any("splice" in f for f in got), f"a cut {cut!r} is a splice: {got}")
    # The same splice, recorded as legacy, with the intact copy appended before
    # ##PROV## as paper-benchmark.yml's compact block left it: passes.
    lineno = bad.splitlines().index(cut + RSS) + 1
    healed = replace(bad, "##PROV##", full + "\n##PROV##")
    got = findings(healed, legacy=(lineno,))
    expect(got == [], f"a recorded splice {cut!r} with its intact copy passes: {got}")
    # Recorded, but the copy is missing: fails (the seed would be lost).
    got = findings(bad, legacy=(lineno,))
    expect(any("intact copy" in f or "lacks" in f for f in got),
           f"a recorded splice {cut!r} without a copy fails: {got}")
    # Recorded at the wrong line: fails.
    got = findings(healed, legacy=(lineno + 1,))
    expect(any("legacy" in f for f in got), f"a wrong legacy list fails: {got}")

expect(any("legacy" in f for f in findings(SOUND, legacy=(3,))),
       "a listed cell that is clean fails (the list must stay true)")

# A marker line that does not start with a complete token is a splice too.
got = findings(replace(SOUND, "##AIR## 1", "##AI##AIR## 1"))
expect(any("splice" in f for f in got), f"'##AI##AIR##' is a splice: {got}")

# ns3-tools#146's field rules.
got = findings(replace(SOUND, "13.0 4.6 1.1", "13.0 x4.6 1.1"))
expect(any("run:" in f and "nrl" in f for f in got), f"a non-numeric ##RUN## field fails: {got}")
got = findings(replace(SOUND, "##RUN## 2 aodv 96.0", "##RUN## two aodv 96.0"))
expect(any("run:" in f for f in got), f"a ##RUN## row without <run> <arm> fails: {got}")
got = findings(replace(SOUND, "##BENCH## aodv 96.5", "##BENCH## aodv 96.x"))
expect(any("bench:" in f for f in got), f"a non-numeric ##BENCH## field fails: {got}")
got = findings(replace(SOUND, "aodv pdr=0.71", "aodv pdr=0.7#"))
expect(any("stddev:" in f for f in got), f"a non-numeric # stddev value fails: {got}")
expect(findings(replace(SOUND, "2.5 inf 7.2", "2.5 nan 7.2")) == [], "nan and inf are numbers")

# A duplicated row, and an arm short of a seed.
got = findings(replace(SOUND, "##PROV##", "##RUN## 2 aodv 1 2 3 4 5\n##PROV##"))
expect(any("dup:" in f for f in got), f"a duplicated ##RUN## row fails: {got}")
got = findings(replace(SOUND, "##RUN## 2 aodv 96.0 15.0 216.5 12.7 17.6 9.1 2.5 inf 9.8\n", ""))
expect(any("seeds:" in f and "aodv" in f for f in got), f"an arm short of a seed fails: {got}")
got = findings(replace(replace(SOUND, "##RUN## 1 aodv 97.0 14.0 215.5 12.8 17.5 9.0 2.5 inf 9.9\n", ""),
                       "##RUN## 2 aodv 96.0 15.0 216.5 12.7 17.6 9.1 2.5 inf 9.8\n", ""))
expect(any("seeds:" in f and "aodv" in f for f in got),
       f"a declared arm with no ##RUN## row fails: {got}")
got = findings(replace(SOUND, "runs=2 firstRun=1", "runs=3 firstRun=1"))
expect(any("seeds:" in f and "[3]" in f for f in got),
       f"a seed ##CONFIG## declares and no arm ran fails: {got}")
got = findings(replace(SOUND, "runs=2 firstRun=1", "runs=1 firstRun=1"))
expect(any("seeds:" in f and "no ##CONFIG## declares" in f for f in got),
       f"a seed ##CONFIG## does not declare fails: {got}")

# Every committed cell passes, the 16 legacy ones included.
r = subprocess.run([sys.executable, os.path.join(HERE, "check_cells.py")],
                   capture_output=True, text=True, check=False)
expect(r.returncode == 0, f"the committed cells pass:\n{r.stdout[-2000:]}{r.stderr[-2000:]}")
expect(f"{len(check_cells.LEGACY)} legacy" in r.stdout, f"every legacy cell is read: {r.stdout[-300:]}")

if failures:
    sys.exit(f"test_check_cells: {failures} failure(s)")
print("test_check_cells: all checks passed")
