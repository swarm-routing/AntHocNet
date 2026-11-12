#!/usr/bin/env python3
# SPDX-License-Identifier: MIT OR GPL-2.0-only
# Copyright (C) 2026 Daniel Henrique Joppi
"""Self-test for common_tables.py (#460): on a synthetic cell with known
answers, the hop means, the per-seed bound counts, the paired differences and
the 'faster' counts come out as computed by hand; a seed missing an arm is left
out; a file with no complete seed says so."""

import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
TOOL = os.path.join(HERE, "common_tables.py")

# run proto nSelf nCommon p99C meanC p99S hopsC hopsS
CELL = """##CONFIG## protocols=anthocnet,aodv,oracle
##COMMON## 1 anthocnet 100 80 50.0 4.0 60.0 1.50 1.6
##COMMON## 1 aodv 100 80 70.0 6.0 80.0 1.40 1.5
##COMMON## 1 oracle 100 80 10.0 2.0 20.0 1.30 1.3
##COMMON## 2 anthocnet 100 90 30.0 6.0 40.0 1.70 1.8
##COMMON## 2 aodv 100 90 90.0 8.0 99.0 1.20 1.3
##COMMON## 2 oracle 100 90 50.0 2.0 60.0 1.30 1.3
##COMMON## 3 anthocnet 100 90 30.0 6.0 40.0 1.70 1.8
"""


def run(text, *args):
    with tempfile.TemporaryDirectory() as d:
        p = os.path.join(d, "cell.txt")
        with open(p, "w") as f:
            f.write(text)
        r = subprocess.run(
            [sys.executable, TOOL, p, "--arms", "anthocnet,aodv", *args],
            capture_output=True,
            text=True,
            check=True,
        )
        return r.stdout


def main():
    out = run(CELL)
    expect = [
        "== cell.txt: 2 seeds",  # seed 3 has no aodv/oracle row
        "  nCommon mean 85",
        "  hopsC  anthocnet 1.600  aodv 1.300  oracle 1.300",
        "  oracle<=arm seeds  anthocnet 2/2  aodv 1/2",
        "  meanC  anthocnet 5.0  oracle 2.0  anthocnet-oracle +3.000",
        "  p99C  anthocnet 40.0  oracle 30.0  anthocnet-oracle +10.000",
        "anthocnet faster 1/2",  # p99C: seed 2 is -20
    ]
    for line in expect:
        assert line in out, f"missing {line!r} in:\n{out}"
    assert "oracle-anthocnet hopsC  -0.300 [" in out, out
    assert "oracle-aodv hopsC  +0.000 [" in out, out
    out = run("##COMMON## 1 anthocnet 100 80 50.0 4.0 60.0 1.50 1.6\n")
    assert "no seed carries a ##COMMON## row for every arm" in out, out
    print("test_common_tables: OK")


if __name__ == "__main__":
    main()
