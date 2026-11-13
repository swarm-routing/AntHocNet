#!/usr/bin/env python3
# SPDX-License-Identifier: MIT OR GPL-2.0-only
# Copyright (C) 2026 Daniel Henrique Joppi
"""Self-test for campaign.py (#662).

The dispatcher spends runner hours and the collector writes the files a
results page is built on, so each piece gets a case with no network: GitHub is
replaced by a fake `gh` that records dispatches and reports a configurable
number of active runs.

Run: python3 test_campaign.py     (no pytest dependency; exits non-zero on the
                                   first failure)
"""

import argparse
import contextlib
import importlib.util
import io
import json
import os
import sys
import tempfile


def _load(name):
    """Load a sibling module from source (see test_scenario_check.py)."""
    path = os.path.join(os.path.dirname(os.path.abspath(__file__)), name + ".py")
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


cp = _load("campaign")
HERE = os.path.dirname(os.path.abspath(__file__))

CASES = []


def case(name):
    def deco(fn):
        CASES.append((name, fn))
        return fn
    return deco


def expect(cond, name, detail):
    if not cond:
        print(f"FAIL {name}: {detail}")
        sys.exit(1)


PLAN = {
    "generation": "vtest", "ref": "main", "cap": 2, "control": "control",
    "arms": {"control": "", "quiet": "--Quiet=true"},
    "cells": {
        "grid": {"workflow": "paper-benchmark.yml",
                 "inputs": {"scenario": "paper", "runs": 20}},
        "sat": {"workflow": "satellite-benchmark.yml",
                "inputs": {"harness": "isl-grid", "extraArgs": "--breakAt=450"},
                "armArgs": {"quiet": "--Range=5000"}},
        "vanet": {"workflow": "paper-benchmark.yml",
                  "inputs": {"scenario": "vanet", "runs": 4}, "blocks": [1, 5]},
    },
    "jobs": [
        {"arms": ["control"], "cells": "*", "inputs": {"protocols": "anthocnet,aodv"}},
        {"arms": ["quiet"], "cells": ["sat", "vanet"]},
    ],
}


SHA = "a" * 40
LEGACY = "ghcr.io/danieljoppi/ns3@sha256:" + "d" * 64
IMAGE = "ghcr.io/swarm-routing/anthocnet-ns3-campaign@sha256:" + "b" * 64
# PLAN as a dispatchable legacy plan: one commit, every arm in a legacy image.
LPLAN = dict(PLAN, source_sha=SHA, legacy_images={"3.42": LEGACY})


class FakeGh:
    """Stands in for campaign.gh: answers the few endpoints the tool calls."""

    def __init__(self, active=0):
        self.active = active
        self.dispatched = []
        self.runs = []  # {"id", "display_title"}, newest last
        self.next_id = 100

    def __call__(self, *args, stdin=None, binary=False):
        url = args[0] if args[0] != "-X" else args[2]
        if "status=" in url:
            # All active runs count once, as benchmark-cell's queued runs.
            hit = "benchmark-cell" in url and "status=queued" in url
            return f"{self.active if hit else 0}\n"
        if url.endswith("/dispatches"):
            self.dispatched.append(stdin)
            self.active += 1
            self.next_id += 1
            self.runs.append({"id": self.next_id,
                              "display_title": json.loads(stdin)["inputs"]["label"]})
            return ""
        if "event=workflow_dispatch" in url:
            return json.dumps({"workflow_runs": self.runs[::-1]})
        raise AssertionError(f"unexpected gh call {args}")


def dispatch(log, fake, cap=None, arm=None, plan=None):
    cp.gh = fake
    cp.time.sleep = lambda _s: None
    a = argparse.Namespace(log=log, cap=cap, wait=False, arm=arm)
    plan = dict(plan or LPLAN, program=cp.PROGRAM)
    with contextlib.redirect_stderr(io.StringIO()) as err, \
            contextlib.redirect_stdout(io.StringIO()):
        try:
            cp.cmd_dispatch(plan, a)
        except SystemExit as e:
            print(e, file=sys.stderr)
    return err.getvalue()


@case("jobs: labels, seed blocks, input overrides and arm attributes")
def _jobs():
    js = {label: (wf, inp) for label, _c, _a, wf, inp in cp.jobs(PLAN)}
    expect(list(js) == ["control/grid", "control/sat", "control/vanet-s1",
                        "control/vanet-s5", "quiet/sat", "quiet/vanet-s1",
                        "quiet/vanet-s5"], "jobs-labels", list(js))
    expect(js["control/grid"][1] == {"scenario": "paper", "runs": "20",
                                     "protocols": "anthocnet,aodv"},
           "jobs-override", js["control/grid"])
    expect("extraArgs" not in js["control/grid"][1], "jobs-no-empty-extra",
           js["control/grid"])
    expect(js["quiet/sat"][1]["extraArgs"] == "--breakAt=450 --Quiet=true --Range=5000",
           "jobs-arm-appended", js["quiet/sat"])
    expect("--Range" not in js["control/sat"][1]["extraArgs"], "jobs-armargs-scoped",
           js["control/sat"])
    expect(js["quiet/vanet-s5"][1]["extraArgs"] == "--firstRun=5 --Quiet=true",
           "jobs-block", js["quiet/vanet-s5"])
    expect(js["quiet/sat"][0] == "satellite-benchmark.yml", "jobs-workflow",
           js["quiet/sat"])


@case("jobs: an unknown arm or cell is refused")
def _jobs_unknown():
    for bad in ({"arms": ["nope"], "cells": "*"}, {"arms": ["quiet"], "cells": ["nope"]}):
        try:
            cp.jobs(dict(PLAN, jobs=[bad]))
        except SystemExit:
            continue
        expect(False, "jobs-unknown", f"{bad} was accepted")


@case("dispatch: stops at the cap, then resumes where the log ends")
def _dispatch_cap():
    with tempfile.TemporaryDirectory() as d:
        log = os.path.join(d, "runs.tsv")
        fake = FakeGh(active=0)
        err = dispatch(log, fake)
        expect(len(fake.dispatched) == 2 and "cap 2 reached" in err,
               "dispatch-cap", f"{len(fake.dispatched)} dispatched\n{err}")
        # Two runs finish: the next call sends two more, none twice.
        fake.active = 0
        dispatch(log, fake)
        with open(log) as f:
            text = f.read()
        labels = [ln.split("\t")[0] for ln in text.splitlines()]
        expect(labels == ["#source_sha", "control/grid", "control/sat", "control/vanet-s1",
                          "control/vanet-s5"], "dispatch-resume", labels)
        expect(cp.read_log(log)["control/vanet-s5"] == "104", "dispatch-run-ids", text)


@case("dispatch: every cell is pinned to source_sha; none without, none on another")
def _dispatch_source_sha():
    with tempfile.TemporaryDirectory() as d:
        fake = FakeGh()
        plan = {k: v for k, v in LPLAN.items() if k != "source_sha"}
        err = dispatch(os.path.join(d, "a.tsv"), fake, plan=plan)
        expect("source_sha" in err and not fake.dispatched, "sha-required", err)
        log = os.path.join(d, "b.tsv")
        dispatch(log, fake, cap=1)
        body = json.loads(fake.dispatched[0])["inputs"]
        expect(body["source_sha"] == SHA and cp.read_log(log)["#source_sha"] == SHA,
               "sha-input", body)
        # A resumed dispatch on another commit is refused.
        fake.active = 0
        err = dispatch(log, fake, cap=1, plan=dict(LPLAN, source_sha="c" * 40))
        expect("refusing" in err and len(fake.dispatched) == 1, "sha-mismatch", err)
        # Two arms on two commits share no log.
        fake = FakeGh()
        err = dispatch(os.path.join(d, "c.tsv"), fake,
                       plan=dict(LPLAN, pins={"quiet": {"source_sha": "c" * 40}}))
        expect("one arm per log" in err and not fake.dispatched, "sha-two-arms", err)


@case("dispatch: an arm with no legacy image belongs to ns3-campaign")
def _dispatch_not_legacy():
    with tempfile.TemporaryDirectory() as d:
        fake = FakeGh()
        plan = dict(PLAN, source_sha=SHA, legacy_arms={"quiet": {"3.42": LEGACY}})
        err = dispatch(os.path.join(d, "a.tsv"), fake, plan=plan)
        expect("ns3-campaign" in err and not fake.dispatched, "not-legacy", err)
        err = dispatch(os.path.join(d, "b.tsv"), fake, cap=10, arm="quiet", plan=plan)
        expect("all 3 job(s) dispatched" in err and len(fake.dispatched) == 3,
               "legacy-arm", err)


@case("dispatch: the plan's ref and the job's inputs reach the API")
def _dispatch_body():
    with tempfile.TemporaryDirectory() as d:
        fake = FakeGh()
        dispatch(os.path.join(d, "runs.tsv"), fake, cap=1)
        body = json.loads(fake.dispatched[0])
        expect(body == {"ref": "main", "inputs": {
            "source_sha": SHA, "harness": "anthocnet-compare",
            "args": "--scenario=paper --runs=20 --nNodes=50 --time=300 "
                    "--areaX=1500 --pause=30 --speed=20 --propagation=range "
                    "--range=300 --diag --mobility=rwp --transport=udp "
                    "--protocols=anthocnet,aodv",
            "version": "3.42", "legacy_image": LEGACY, "cell": "grid",
            "label": "vtest/control/grid"}},
            "dispatch-body", body)


@case("legacy cells: the retired workflows' command lines, flag for flag")
def _legacy():
    # paper-benchmark.yml's shell: common flags, areaY if set, extraArgs,
    # then --diag --mobility --transport --protocols.
    h, a, v = cp.legacy_command("paper-benchmark.yml", {
        "scenario": "paper", "version": "3.42-opt", "nNodes": "500",
        "time": "300", "runs": "2", "areaX": "4743", "areaY": "1581",
        "pause": "30", "speed": "20", "protocols": "anthocnet,aodv",
        "propagation": "range", "mobility": "rwp", "transport": "udp",
        "range": "300", "harness": "compare", "extraArgs": "--firstRun=3"})
    expect((h, v) == ("anthocnet-compare", "3.42-opt") and a ==
           "--scenario=paper --runs=2 --nNodes=500 --time=300 --areaX=4743 "
           "--pause=30 --speed=20 --propagation=range --range=300 --areaY=1581 "
           "--firstRun=3 --diag --mobility=rwp --transport=udp "
           "--protocols=anthocnet,aodv", "legacy-paper", a)
    h, a, v = cp.legacy_command("satellite-benchmark.yml", {
        "harness": "isl-grid", "version": "3.42-opt", "time": "900",
        "runs": "20", "cbrBps": "4096", "protocols": "anthocnet",
        "extraArgs": "--breakAt=450"})
    expect(h == "isl-grid" and a ==
           "--rows=4 --cols=4 --torus=true --islDelayMs=5 --islRate=10Mbps "
           "--time=900 --runs=20 --flows=8 --cbrBps=4096 --diag "
           "--protocols=anthocnet --breakAt=450", "legacy-isl", a)
    h, a, v = cp.legacy_command("satellite-benchmark.yml", {
        "harness": "leo-walker", "version": "3.48", "flows": "4",
        "cbrBps": "2048", "protocols": "anthocnet", "extraArgs": "--shell=s1"})
    expect((h, v) == ("leo-walker", "3.48") and a ==
           "--time=300 --runs=3 --flows=4 --cbrBps=2048 --islRate=10Mbps "
           "--protocols=anthocnet --shell=s1", "legacy-leo", a)
    h, a, v = cp.legacy_command("benchmark-cell.yml", {
        "harness": "isl-grid", "args": "--rows=8", "extraArgs": "--firstRun=5"})
    expect((h, a, v) == ("isl-grid", "--rows=8 --firstRun=5", "3.42-opt"),
           "native-cell", (h, a, v))
    for wf, inp in (("paper-benchmark.yml", {"harness": "baselines"}),
                    ("scenario-matrix.yml", {})):
        try:
            cp.legacy_command(wf, inp)
        except SystemExit:
            continue
        expect(False, "legacy-refused", f"{wf} {inp} was accepted")


@case("every committed plan translates to benchmark-cell inputs")
def _plans_translate():
    import glob
    for path in sorted(glob.glob(os.path.join(HERE, "campaigns", "*.yaml"))):
        plan = cp.load_plan(path)
        for _l, _c, _a, wf, inp in cp.jobs(plan):
            h, a, _v = cp.legacy_command(wf, inp, plan["program"])
            expect(h in ("anthocnet-compare", "routing-compare", "isl-grid", "leo-walker") and a,
                   "plans-translate", f"{path}: {h} {a}")


def collect(log, src, out):
    a = argparse.Namespace(log=log, out=out, src=src, cache=None)
    with contextlib.redirect_stderr(io.StringIO()) as err:
        cp.cmd_collect(PLAN, a)
    return err.getvalue()


def write_run(src, label, text, name="paper-results.txt"):
    os.makedirs(os.path.join(src, label), exist_ok=True)
    with open(os.path.join(src, label, name), "w") as f:
        f.write(text)


@case("collect: the artifact cache is keyed by run id, not by label")
def _collect_cache():
    # Two plans may share a label (the v2.5.0 screen and stage 2 both have
    # retry0/scale-500-s1); a label-keyed cache handed stage 2 the screen's
    # artifact. A run id names one run.
    with tempfile.TemporaryDirectory() as d:
        cache, out, log = (os.path.join(d, x) for x in ("cache", "out", "runs.tsv"))
        with open(log, "w") as f:
            f.write("quiet/vanet-s1\t21\nquiet/vanet-s5\t22\n")
        write_run(cache, "quiet/vanet-s1", "##RUN## 1 anthocnet 1\n##PROV## commit=stale\n")
        write_run(cache, "21", "##RUN## 1 anthocnet 40\n##PROV## commit=a\n")
        write_run(cache, "22", "##RUN## 5 anthocnet 41\n##PROV## commit=b\n")
        a = argparse.Namespace(log=log, out=out, src=None, cache=cache)
        with contextlib.redirect_stderr(io.StringIO()):
            cp.cmd_collect(PLAN, a)
        with open(os.path.join(out, "vanet-vtest-quiet.txt")) as f:
            text = f.read()
        expect("commit=a" in text and "commit=stale" not in text, "collect-cache-key", text)


@case("collect: seed blocks concatenate into one file per cell and arm")
def _collect():
    with tempfile.TemporaryDirectory() as d:
        src, out, log = (os.path.join(d, x) for x in ("src", "out", "runs.tsv"))
        with open(log, "w") as f:
            f.write("quiet/vanet-s1\t11\nquiet/vanet-s5\t12\nquiet/sat\t13\n")
        write_run(src, "quiet/vanet-s1", "##RUN## 1 anthocnet 40\n##PROV## commit=a\n")
        write_run(src, "quiet/vanet-s5", "##RUN## 5 anthocnet 41\n##PROV## commit=b\n")
        write_run(src, "quiet/sat", "##RUN## 1 anthocnet 99\n", "satellite-results.txt")
        err = collect(log, src, out)
        files = sorted(os.listdir(out))
        expect(files == ["sat-vtest-quiet.txt", "vanet-vtest-quiet.txt"],
               "collect-names", files)
        with open(os.path.join(out, "vanet-vtest-quiet.txt")) as f:
            text = f.read()
        expect(text.startswith("# vtest campaign vanet-vtest-quiet: 2 run(s): "
                               "11 (quiet/vanet-s1) / 12 (quiet/vanet-s5)"),
               "collect-header", text)
        expect(text.count("##PROV##") == 2 and "##RUN## 5" in text,
               "collect-body", text)
        expect("quiet/sat: no ##PROV## line" in err, "collect-prov-warn", err)
        expect("SKIP grid-vtest.txt: 0 of 1" in err, "collect-skip", err)


@case("status --brief: only the labels not yet successful, counts on stderr")
def _status_brief():
    def fake(*args, **_k):
        rid = args[0].rsplit("/", 1)[1]
        done = rid == "11"
        return json.dumps({"conclusion": "success" if done else None,
                           "status": "completed" if done else "in_progress"})
    cp.gh = fake
    with tempfile.TemporaryDirectory() as d:
        log = os.path.join(d, "runs.tsv")
        with open(log, "w") as f:
            f.write("quiet/vanet-s1\t11\nquiet/vanet-s5\t12\n")
        a = argparse.Namespace(log=log, brief=True)
        with contextlib.redirect_stderr(io.StringIO()) as err, \
                contextlib.redirect_stdout(io.StringIO()) as out:
            cp.cmd_status(PLAN, a)
    lines = out.getvalue().splitlines()
    expect("quiet/vanet-s1\t11\tsuccess" not in lines
           and "quiet/vanet-s5\t12\tin_progress" in lines
           and "control/grid\t-\tnot-dispatched" in lines, "status-brief", lines)
    expect("1 success" in err.getvalue(), "status-brief-counts", err.getvalue())


@case("collect: a timed-out run's stub artifact is a missing run")
def _collect_stub():
    with tempfile.TemporaryDirectory() as d:
        src, out, log = (os.path.join(d, x) for x in ("src", "out", "runs.tsv"))
        with open(log, "w") as f:
            f.write("quiet/vanet-s1\t11\nquiet/vanet-s5\t12\n")
        write_run(src, "quiet/vanet-s1", "##RUN## 1 anthocnet 40\n##PROV## commit=a\n")
        write_run(src, "quiet/vanet-s5", "##PROV## commit=a\n")
        err = collect(log, src, out)
        expect(not os.path.exists(os.path.join(out, "vanet-vtest-quiet.txt")),
               "collect-stub", os.listdir(out))
        expect("quiet/vanet-s5: artifact has no result rows" in err
               and "SKIP vanet-vtest-quiet.txt: 1 of 2" in err, "collect-stub", err)


@case("collect: a cell with a block still running is not written")
def _collect_partial():
    with tempfile.TemporaryDirectory() as d:
        src, out, log = (os.path.join(d, x) for x in ("src", "out", "runs.tsv"))
        with open(log, "w") as f:
            f.write("quiet/vanet-s1\t11\nquiet/vanet-s5\t12\n")
        write_run(src, "quiet/vanet-s1", "##RUN## 1 anthocnet 40\n")
        err = collect(log, src, out)
        expect(not os.path.exists(os.path.join(out, "vanet-vtest-quiet.txt")),
               "collect-partial", os.listdir(out))
        expect("SKIP vanet-vtest-quiet.txt: 1 of 2" in err, "collect-partial", err)


@case("the committed v2.2.0 phase-2 plan expands to its 99 jobs")
def _v220_plan():
    plan = cp.load_plan(os.path.join(HERE, "campaigns", "v2.2.0-phase2.yaml"))
    js = cp.jobs(plan)
    expect(len(js) == 99, "v220-count", len(js))
    arms = {}
    for _l, _c, arm, _w, _i in js:
        arms[arm] = arms.get(arm, 0) + 1
    expect(arms == {"recheck": 1, "attrib-repair200": 6, "repair200": 23,
                    "sinr": 23, "hello500": 23, "reconv1s": 23},
           "v220-arms", arms)


@case("dispatch --arm: only that arm's jobs, each arm in its own log")
def _dispatch_arm():
    with tempfile.TemporaryDirectory() as d:
        fake = FakeGh()
        err = dispatch(os.path.join(d, "quiet.tsv"), fake, cap=10, arm="quiet")
        labels = list(cp.read_log(os.path.join(d, "quiet.tsv")))
        expect(labels == ["#source_sha", "quiet/sat", "quiet/vanet-s1", "quiet/vanet-s5"],
               "arm-labels", labels)
        expect("all 3 job(s) dispatched" in err, "arm-count", err)
        err = dispatch(os.path.join(d, "x.tsv"), fake, arm="nope")
        expect("no such arm" in err and len(fake.dispatched) == 3, "arm-unknown", err)


@case("legacy images: each legacy job runs its arm's image for its version, by digest")
def _legacy_images():
    with tempfile.TemporaryDirectory() as d:
        fake = FakeGh()
        dispatch(os.path.join(d, "old.tsv"), fake, cap=10, arm="control")
        got = [json.loads(b)["inputs"].get("legacy_image") for b in fake.dispatched]
        expect(got == [LEGACY] * 4, "legacy-image-input", got)
        # A version the map lacks is refused, not run in ns3:<version>.
        fake = FakeGh()
        err = dispatch(os.path.join(d, "miss.tsv"), fake, cap=10, arm="control",
                       plan=dict(LPLAN, legacy_images={"3.48": LEGACY}))
        expect("no legacy image for version 3.42" in err and not fake.dispatched,
               "legacy-image-missing", err)
    # load_plan refuses a tag: a control image is pinned by digest.
    with tempfile.TemporaryDirectory() as d:
        path = os.path.join(d, "p.yaml")
        for extra in ("legacy_images: {3.42-opt: 'ghcr.io/danieljoppi/ns3:3.42-opt'}\n",
                      "legacy_arms: {a: {3.42-opt: 'ghcr.io/danieljoppi/ns3:3.42-opt'}}\n",
                      "source_sha: abc\n"):
            with open(path, "w") as f:
                f.write("generation: g\narms: {a: ''}\ncells: {}\njobs: []\n" + extra)
            try:
                cp.load_plan(path)
                expect(False, "plan-pins", f"accepted {extra}")
            except SystemExit as e:
                expect("sha256" in str(e) or "40-char" in str(e), "plan-pins", e)


@case("collect: a cell whose ##PROV## is not its pin is rejected")
def _collect_prov():
    with tempfile.TemporaryDirectory() as d:
        src, out, log = (os.path.join(d, x) for x in ("src", "out", "runs.tsv"))
        with open(log, "w") as f:
            f.write("quiet/vanet-s1\t11\nquiet/vanet-s5\t12\nquiet/sat\t13\n")
        good = f"##RUN## 1 anthocnet 40\n##PROV## commit={SHA} image={LEGACY}\n"
        write_run(src, "quiet/vanet-s1", good)
        write_run(src, "quiet/vanet-s5", good.replace(SHA, "c" * 40))
        write_run(src, "quiet/sat", good.replace(LEGACY, IMAGE))
        a = argparse.Namespace(log=log, out=out, src=src, cache=None, arm="quiet")
        with contextlib.redirect_stderr(io.StringIO()) as err:
            try:
                cp.cmd_collect(LPLAN, a)
                code = 0
            except SystemExit as e:
                code = e.code
        err = err.getvalue()
        expect(code == 1 and os.listdir(out) == [], "collect-prov", (code, os.listdir(out)))
        expect(f"quiet/vanet-s5\trun 12\tcommit {'c' * 40}, not {SHA}" in err
               and "quiet/sat\trun 13\timage " + IMAGE in err, "collect-prov-why", err)


@case("status and collect --arm: the other arm's labels are not reported")
def _status_collect_arm():
    cp.gh = lambda *args, **_k: json.dumps({"conclusion": "success",
                                            "status": "completed"})
    with tempfile.TemporaryDirectory() as d:
        src, out, log = (os.path.join(d, x) for x in ("src", "out", "runs.tsv"))
        with open(log, "w") as f:
            f.write("quiet/vanet-s1\t11\nquiet/vanet-s5\t12\nquiet/sat\t13\n")
        a = argparse.Namespace(log=log, brief=False, arm="quiet")
        with contextlib.redirect_stderr(io.StringIO()) as err, \
                contextlib.redirect_stdout(io.StringIO()):
            cp.cmd_status(PLAN, a)
        expect(err.getvalue().strip() == "3 success", "status-arm", err.getvalue())
        for label, n in (("quiet/vanet-s1", 1), ("quiet/vanet-s5", 5), ("quiet/sat", 1)):
            write_run(src, label, f"##RUN## {n} anthocnet 40\n##PROV## commit=a\n")
        a = argparse.Namespace(log=log, out=out, src=src, cache=None, arm="quiet")
        with contextlib.redirect_stderr(io.StringIO()) as err:
            cp.cmd_collect(PLAN, a)
        expect("SKIP" not in err.getvalue() and "wrote 2 file(s)" in err.getvalue(),
               "collect-arm", err.getvalue())


@case("verdicts: one bench_parse --ab per protocol the cell runs")
def _verdicts():
    calls = []

    def run(cmd, **_k):
        calls.append(cmd[cmd.index("--proto") + 1])
        return argparse.Namespace(stdout=f"    paired 95% CI (n=2): dPDR {calls[-1]}\n",
                                  stderr="", returncode=0)

    plan = dict(PLAN, program=cp.PROGRAM, jobs=[
        {"arms": ["control", "quiet"], "cells": "*",
         "inputs": {"protocols": "anthocnet,aodv,oracle"}}])
    real = cp.subprocess.run
    cp.subprocess.run = run
    try:
        with tempfile.TemporaryDirectory() as d:
            for cell in ("grid", "vanet"):
                for arm in ("control", "quiet"):
                    open(os.path.join(d, cp.out_name(plan, cell, arm)), "w").close()
            a = argparse.Namespace(dir=d, arm=None)
            with contextlib.redirect_stdout(io.StringIO()) as out:
                cp.cmd_verdicts(plan, a)
    finally:
        cp.subprocess.run = real
    lines = out.getvalue().splitlines()
    expect(calls == ["anthocnet", "aodv", "oracle"] * 2, "verdicts-protos", calls)
    expect(lines[1] == "quiet\tgrid\taodv\tpaired 95% CI (n=2): dPDR aodv"
           and "quiet\tsat\t-\t(missing" in lines[3], "verdicts-lines", lines)


@case("the #867 control plan: both images, every family, seed for seed, one commit")
def _ctl867_plan():
    plan = cp.load_plan(os.path.join(HERE, "campaigns", "v2.7.x-image-control.yaml"))
    js = cp.jobs(plan)
    by_arm = {}
    for label, _c, arm, wf, inp in js:
        harness, args, version = cp.legacy_command(wf, inp, plan["program"])
        by_arm.setdefault(arm, {})[label.split("/", 1)[1]] = (harness, args, version)
        if arm == "old":
            expect(cp.legacy_image(plan, arm, version).startswith(
                "ghcr.io/danieljoppi/ns3@sha256:"), "ctl867-legacy-image", (label, version))
    expect(sorted(by_arm) == ["dev", "old"] and len(by_arm["old"]) == 23
           and by_arm["old"] == by_arm["dev"], "ctl867-legs",
           {k: len(v) for k, v in by_arm.items()})
    # One commit for both legs; the dev arm alone on the ctl867 image, the old
    # arm alone legacy, so ns3-campaign runs dev and refuses old (#905).
    expect(plan["source_sha"] == "b531d5d40c199b159527257119f872e3da7eeb5b"
           and plan["ref"] == "main" and plan["repo"] == "swarm-routing/AntHocNet"
           and not plan["legacy_images"] and "image" not in plan
           and list(plan["legacy_arms"]) == ["old"] and cp.legacy_images(plan, "dev") == {}
           and plan["pins"] == {"dev": {"image": "ghcr.io/swarm-routing/anthocnet-ns3-campaign"
                                        "@sha256:ee930d8c6592b012a4ec321f0f255945f66cbf505e"
                                        "5850ab7fea683f1e2aba68"}},
           "ctl867-pins", {k: plan.get(k) for k in ("source_sha", "ref", "pins", "legacy_arms")})
    for cell, spec in plan["cells"].items():
        expect(os.path.exists(os.path.join(HERE, "..", "..", "results", "cells",
                                           spec["source"])), "ctl867-source", cell)
    expect("vanet-sparse" in plan["cells"] and "vanet-main" not in plan["cells"],
           "ctl867-vanet", list(plan["cells"]))


@case("plan: each job's ns-3 release, image and commit; a mixed-release cell is flagged")
def _plan_releases():
    image = "ghcr.io/swarm-routing/anthocnet-ns3-campaign@sha256:" + "c" * 64
    real = cp.registry_config
    cp.registry_config = lambda ref: {"Labels": {cp.NS3_LABEL: "ghcr.io/swarm-routing/ns3-dev:3.48-optimized"}}
    plan = dict(PLAN, source_sha=SHA, image=image, legacy_arms={"control": {"3.42": LEGACY}})
    try:
        with contextlib.redirect_stdout(io.StringIO()) as out, \
                contextlib.redirect_stderr(io.StringIO()) as err:
            cp.cmd_plan(plan, argparse.Namespace(offline=False))
        with contextlib.redirect_stdout(io.StringIO()) as off, \
                contextlib.redirect_stderr(io.StringIO()) as offerr:
            cp.cmd_plan(plan, argparse.Namespace(offline=True))
    finally:
        cp.registry_config = real
    rows = [ln.split("\t") for ln in out.getvalue().splitlines()]
    got = {r[0]: r[1:4] for r in rows}
    expect(got["control/sat"] == ["ns-3 3.42", LEGACY, SHA]
           and got["quiet/sat"] == ["ns-3 3.48", image, SHA], "plan-rows", got)
    expect("warning: sat: the arms run different ns-3 releases (control 3.42, quiet 3.48)"
           in err.getvalue() and "warning: grid" not in err.getvalue(), "plan-mixed", err.getvalue())
    # offline: the image's release is not read, and the cell is flagged as unread
    expect("\tns-3 ?\t" + image in off.getvalue()
           and "(control 3.42, quiet ?) (? = not read)" in offerr.getvalue(), "plan-offline",
           offerr.getvalue())


def main():
    for name, fn in CASES:
        fn()
        print(f"ok   {name}")
    print(f"\n{len(CASES)} cases passed")


if __name__ == "__main__":
    main()
