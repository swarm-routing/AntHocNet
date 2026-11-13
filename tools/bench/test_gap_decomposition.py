#!/usr/bin/env python3
# Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
# SPDX-License-Identifier: MIT OR GPL-2.0-only
# Copyright (C) 2026 Daniel Henrique Joppi
"""Self-test for gap_decomposition.py (#460): on a synthetic cell with known
answers, the oracle's mode and radius, the channel, routing and total terms,
routing's paired interval and the routing share come out as computed by hand;
a seed missing an arm is left out; a file with no complete seed says so."""

import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
TOOL = os.path.join(HERE, "gap_decomposition.py")

# seed arm pdr delay delay99 thrput nrl jitter off50 off90 nrl_bytes
CELL = """##CONFIG## protocols=anthocnet,aodv,oracle
##RUN## 1 anthocnet 90.0 30.0 400.0 12.0 14.0 40.0 3.5 150.5 17.9
##RUN## 2 anthocnet 94.0 30.0 400.0 12.0 14.0 40.0 3.5 150.5 17.9
##RUN## 3 anthocnet 92.0 30.0 400.0 12.0 14.0 40.0 3.5 150.5 17.9
##RUN## 1 aodv 80.0 30.0 400.0 12.0 14.0 40.0 3.5 150.5 17.9
##RUN## 2 aodv 84.0 30.0 400.0 12.0 14.0 40.0 3.5 150.5 17.9
##ORACLE## 1 oracle mode=disk-approx approx=1 nodes=50 edges=998 recomputes=900 changes=870 range=300.0 noRoute=0 noRouteOrigin=0 noRouteFwd=0 nrl=0.00
##ORACLE## 2 oracle mode=disk-approx approx=1 nodes=50 edges=990 recomputes=900 changes=870 range=300.0 noRoute=0 noRouteOrigin=0 noRouteFwd=0 nrl=0.00
##RUN## 1 oracle 99.0 9.0 20.0 9.5 0.00 10.0 2.5 20.5 0.0000
##RUN## 2 oracle 97.0 9.0 20.0 9.5 0.00 10.0 2.5 20.5 0.0000
"""


def run(text, *args):
    with tempfile.TemporaryDirectory() as d:
        p = os.path.join(d, "cell.txt")
        with open(p, "w") as f:
            f.write(text)
        r = subprocess.run(
            [sys.executable, TOOL, p, *args], capture_output=True, text=True, check=True
        )
        return r.stdout


def main():
    out = run(CELL)
    expect = [
        "== cell.txt",
        "  2 seeds  oracle mode disk-approx 300.0 m  pdr 98.00 [",  # seed 3: no aodv/oracle
        # anthocnet: total (10+6)/2 = 8; channel (1+3)/2 = 2; routing (9+3)/2 = 6
        "  anthocnet  total 8.00  channel 2.00  routing 6.00 [",
        "share 75.0 %",
        # aodv: total (20+16)/2 = 18; routing (19+13)/2 = 16; share 16/18
        "  aodv       total 18.00  channel 2.00  routing 16.00 [",
        "share 88.9 %",
    ]
    for line in expect:
        assert line in out, f"missing {line!r} in:\n{out}"
    out = run("##RUN## 1 anthocnet 90.0 30.0 400.0 12.0 14.0 40.0 3.5 150.5 17.9\n")
    assert "no seed carries a ##RUN## row for every arm and oracle" in out, out
    print("test_gap_decomposition: OK")


if __name__ == "__main__":
    main()
