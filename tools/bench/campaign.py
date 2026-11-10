#!/usr/bin/env python3
# SPDX-License-Identifier: MIT OR GPL-2.0-only
# Copyright (C) 2026 Daniel Henrique Joppi
"""Benchmark campaigns from a plan file (#662): plan, dispatch, status,
collect, verdicts.

A campaign is cells x arms. The plan (YAML, committed in the PR that
pre-registers the campaign) names each cell's harness command line,
each arm's extra attributes, and which arm runs on which cells. Every job gets
a label, `<arm>/<cell>`, or `<arm>/<cell>-s<firstRun>` for a cell split into
seed blocks.

Why a tool: the v2.2.0 control arm dispatched 36 jobs at once and held every
runner, so PR CI queued for hours (#662). `dispatch` keeps at most `cap`
benchmark runs queued or in progress (12 of the account's 20 leaves 8 for PR
CI). It needs no long-lived process: each call dispatches until the cap is
reached and exits, and the label -> run id log makes it resumable, so it can
be re-run from a scheduled check-in until every label is logged.

Usage:
    campaign.py plan     PLAN                   # list the jobs; dispatches nothing
    campaign.py dispatch PLAN --log RUNS.tsv [--arm ARM] [--cap 12] [--wait]
                (--build-image [--ns3-version 3.48] | --image REF | --no-image)
    campaign.py status   PLAN --log RUNS.tsv [--arm ARM]
    campaign.py collect  PLAN --log RUNS.tsv --out results/cells [--from DIR] [--arm ARM]
    campaign.py verdicts PLAN --dir results/cells [--arm ARM]

`--arm ARM` limits dispatch, status and collect to that arm's jobs. A log pins
one image, so a campaign whose arms run on different images (a cross-image
control, #867) dispatches, tracks and collects each arm with its own log.

`collect` writes one file per (cell, arm), `<cell>-<generation>.txt` for the
control arm and `<cell>-<generation>-<arm>.txt` for the others, with a `#`
header naming the runs. The seed blocks of a split cell are concatenated, and
bench_parse.py pairs them by seed. Each artifact already ends with its run's
##PROV## line (#674). Without --from it downloads each run's artifact with
`gh api` into --cache (default .campaign-artifacts/<run id>/; by run id, since
two plans can share a label). Where that cannot follow the blob redirect, fetch the artifacts
another way and pass --from DIR, holding DIR/<label>/<results file>: the
rescue-artifacts workflow's bundle_run_ids input gathers every run's artifact
into one `rescue-bundle` (<run id>/...), one download for the whole campaign.

Every cell of a campaign runs one binary (#866, swarm-routing/.github#107):
`dispatch --build-image` resolves the plan's ref to a commit, dispatches
campaign-image.yml for it (ns3-dev:<--ns3-version>-optimized, resolved to a
digest there; 3.48 while the AODV livelock #843 is open) and waits for the
image digest; `--image ghcr.io/...@sha256:...` names one already built. The
digest is the first line of the log (`#image<TAB><ref>`), so a resumed
dispatch reuses it and refuses a different one, and it is passed to every
benchmark-cell.yml dispatch as its `image` input: the cells pull it by digest
and build nothing. `--no-image` is the legacy path (each cell installs and
builds the module in ns3:<version>), to replay a published cell only.
A plan's `legacy_images` map (version -> image by digest) replaces
ns3:<version> on that path: each job passes its version's image as
benchmark-cell.yml's `legacy_image` input, so a control re-runs a published
cell in the image its ##PROV## names (#867). With the map set, a legacy job
whose version it lacks is refused.

`verdicts` runs bench_parse.py --ab --proto P on every arm's file against the
control file of the same cell, for every protocol P the cell's command line
names (--protocols=), and prints one line per (arm, cell, protocol).

Plan format:

    generation: v230              # file suffix
    ref: main                     # the ref every job is dispatched at
    program: routing-compare      # optional: the MANET program a compare
                                  # cell runs (default anthocnet-compare, #769)
    cap: 12
    control: control              # the arm whose files carry no arm suffix
    legacy_images:                # optional: --no-image runs these, by version
      3.42-opt: ghcr.io/danieljoppi/ns3@sha256:...
    arms:
      control: ""
      quiet: "--ns3::anthocnet::RoutingProtocol::QuietMode=true"
    cells:
      scale-500:
        workflow: benchmark-cell.yml
        inputs: {harness: anthocnet-compare, version: 3.42-opt,
                 args: "--scenario=paper --nNodes=500 --runs=2 --diag ..."}
      vanet-main:                  # a v2.2.0-v2.4.0 cell: still accepted
        workflow: paper-benchmark.yml
        inputs: {scenario: vanet, runs: 4, ...}
        blocks: [1, 5, 9, 13, 17]  # one job per firstRun, one file
        armArgs: {ll: "--ns3::...::LinkLifetimeRange=300"}  # this cell only
    jobs:
      - {arms: [control], cells: "*", inputs: {protocols: "anthocnet,aodv"}}
      - {arms: [quiet], cells: [grid-rwp-tworay, vanet-main]}

A job entry's `inputs` override the cell's. An arm's attributes are appended to
the cell's `extraArgs`, then the cell's `armArgs` for that arm, if any (an arm
whose value depends on the cell, such as a radio range).

Every job is dispatched to benchmark-cell.yml (#746). A cell naming
paper-benchmark.yml or satellite-benchmark.yml (the committed v2.2.0-v2.4.0
plans) is translated into the command line that workflow built from the same
inputs, flag for flag, so an old plan re-runs the same cells. `plan` prints
each job's image and command line.
"""

import argparse
import json
import os
import re
import subprocess
import sys
import tempfile
import time
import zipfile

REPO = os.environ.get("AHN_REPO", "swarm-routing/AntHocNet")
# Every job runs on benchmark-cell.yml (#612, #746). A cell written for the
# retired paper-benchmark / satellite-benchmark workflows is translated into
# the harness command line that workflow built (legacy_command), so the
# committed v2.2.0-v2.4.0 plans re-run unchanged.
WORKFLOW = "benchmark-cell.yml"
# Builds one campaign image per (commit, ns3-dev digest) (#866).
IMAGE_WORKFLOW = "campaign-image.yml"
IMAGE_KEY = "#image"          # the log line holding the campaign's image
NO_IMAGE = "-"                # ... or saying it runs the legacy path
IMAGE_RE = re.compile(r"ghcr\.io/[a-z0-9._-]+/[a-z0-9._/-]+@sha256:[0-9a-f]{64}")
RESULTS = ("results.txt", "paper-results.txt", "satellite-results.txt")

# The retired workflows' input defaults, for inputs a plan leaves out.
PAPER_DEFAULTS = {"scenario": "paper", "version": "3.42", "nNodes": "50",
                  "time": "300", "runs": "3", "areaX": "1500", "areaY": "",
                  "pause": "30", "speed": "20",
                  "protocols": "anthocnet,aodv,olsr,dsdv", "propagation": "range",
                  "mobility": "rwp", "transport": "udp", "range": "300",
                  "harness": "compare", "extraArgs": ""}
SAT_DEFAULTS = {"harness": "isl-grid", "version": "3.42", "rows": "4", "cols": "4",
                "torus": "true", "islDelayMs": "5", "islRate": "10Mbps",
                "time": "300", "runs": "3", "flows": "8", "cbrBps": "4096",
                "protocols": "anthocnet,aodv,olsr,oracle", "extraArgs": ""}


# The MANET-family program. ADR-0027 kept anthocnet-compare as an alias of
# routing-compare (same arguments, same output), so committed plans keep
# their name; a plan sets `program: routing-compare` to run the new one (#769).
PROGRAM = "anthocnet-compare"


def legacy_command(wf, inp, program=PROGRAM):
    """(harness, args, version) for one job, as benchmark-cell.yml inputs.

    paper-benchmark.yml and satellite-benchmark.yml built the harness command
    line in shell from their inputs; this is that expansion, flag for flag and
    in the same order. A benchmark-cell.yml cell gives harness/args/version
    itself, and its extraArgs (seed block, arm) are appended to args.
    """
    if wf == WORKFLOW:
        args = " ".join(x for x in (inp.get("args", ""), inp.get("extraArgs", "")) if x)
        return inp.get("harness", program), args, inp.get("version", "3.42-opt")
    if wf == "paper-benchmark.yml":
        i = dict(PAPER_DEFAULTS, **inp)
        if i["harness"] != "compare":
            sys.exit(f"paper-benchmark harness '{i['harness']}' has no benchmark-cell "
                     "equivalent (manet-baselines is not a benchmark-cell harness)")
        args = [f"--scenario={i['scenario'] or 'paper'}", f"--runs={i['runs']}",
                f"--nNodes={i['nNodes']}", f"--time={i['time']}",
                f"--areaX={i['areaX']}", f"--pause={i['pause']}",
                f"--speed={i['speed']}", f"--propagation={i['propagation']}",
                f"--range={i['range']}"]
        if i["areaY"]:
            args.append(f"--areaY={i['areaY']}")
        if i["extraArgs"]:
            args.append(i["extraArgs"])
        args += ["--diag", f"--mobility={i['mobility']}",
                 f"--transport={i['transport']}", f"--protocols={i['protocols']}"]
        return program, " ".join(args), i["version"]
    if wf == "satellite-benchmark.yml":
        i = dict(SAT_DEFAULTS, **inp)
        common = [f"--time={i['time']}", f"--runs={i['runs']}", f"--flows={i['flows']}",
                  f"--cbrBps={i['cbrBps']}", f"--islRate={i['islRate']}"]
        if i["harness"] == "leo-walker":
            args = common + [f"--protocols={i['protocols']}"]
        elif i["harness"] == "isl-grid":
            args = [f"--rows={i['rows']}", f"--cols={i['cols']}",
                    f"--torus={i['torus']}", f"--islDelayMs={i['islDelayMs']}",
                    f"--islRate={i['islRate']}", f"--time={i['time']}",
                    f"--runs={i['runs']}", f"--flows={i['flows']}",
                    f"--cbrBps={i['cbrBps']}", "--diag",
                    f"--protocols={i['protocols']}"]
        else:
            sys.exit(f"unknown satellite harness '{i['harness']}'")
        if i["extraArgs"]:
            args.append(i["extraArgs"])
        return i["harness"], " ".join(args), i["version"]
    sys.exit(f"workflow '{wf}': a cell names benchmark-cell.yml, or a retired "
             "workflow campaign.py translates (paper-benchmark.yml, "
             "satellite-benchmark.yml)")


def load_plan(path):
    import yaml
    with open(path) as f:
        plan = yaml.safe_load(f)
    for key in ("generation", "arms", "cells", "jobs"):
        if key not in plan:
            sys.exit(f"{path}: plan has no '{key}'")
    plan.setdefault("ref", "main")
    plan.setdefault("program", PROGRAM)
    plan.setdefault("cap", 12)
    plan.setdefault("control", "control")
    plan["legacy_images"] = {str(k): v for k, v in
                             (plan.get("legacy_images") or {}).items()}
    for version, ref in plan["legacy_images"].items():
        if not IMAGE_RE.fullmatch(str(ref)):
            sys.exit(f"{path}: legacy_images {version}: {ref!r} is not "
                     "ghcr.io/<owner>/<name>@sha256:<64 hex>")
    return plan


def legacy_image(plan, version):
    """The image a legacy job of this version runs in ("" = ns3:<version>)."""
    images = plan.get("legacy_images") or {}
    if images and version not in images:
        sys.exit(f"legacy_images has no image for version {version}; add it, "
                 "so no cell of the control falls back to ns3:<version>")
    return images.get(version, "")


def selected(plan, a):
    """jobs(plan), limited to --arm when it is given."""
    arm = getattr(a, "arm", None)
    if arm and arm not in plan["arms"]:
        sys.exit(f"--arm {arm}: the plan has no such arm")
    return [j for j in jobs(plan) if not arm or j[2] == arm]


def jobs(plan):
    """[(label, cell, arm, workflow, inputs)] in plan order, labels unique."""
    out, seen = [], set()
    for entry in plan["jobs"]:
        names = entry.get("cells", "*")
        names = list(plan["cells"]) if names == "*" else names
        for arm in entry["arms"]:
            if arm not in plan["arms"]:
                sys.exit(f"job names arm '{arm}', which the plan does not define")
            for cell in names:
                if cell not in plan["cells"]:
                    sys.exit(f"job names cell '{cell}', which the plan does not define")
                spec = plan["cells"][cell]
                inp = dict(spec.get("inputs", {}), **entry.get("inputs", {}))
                extra = " ".join(x for x in (str(inp.get("extraArgs", "")),
                                             plan["arms"][arm] or "",
                                             spec.get("armArgs", {}).get(arm, ""))
                                 if x)
                for first in spec.get("blocks", [None]):
                    args = f"--firstRun={first} {extra}".strip() if first else extra
                    label = f"{arm}/{cell}" + (f"-s{first}" if first else "")
                    if label in seen:
                        sys.exit(f"label '{label}' appears twice in the plan")
                    seen.add(label)
                    job_inp = {k: str(v) for k, v in inp.items()}
                    if args:
                        job_inp["extraArgs"] = args
                    else:
                        job_inp.pop("extraArgs", None)
                    out.append((label, cell, arm, spec["workflow"], job_inp))
    return out


def read_log(path):
    """label -> run id, from a TSV the dispatcher appends to."""
    if not os.path.exists(path):
        return {}
    with open(path) as f:
        return dict(line.rstrip("\n").split("\t")[:2] for line in f if "\t" in line)


def gh(*args, stdin=None, binary=False):
    p = subprocess.run(["gh", "api", *args], input=stdin, capture_output=True,
                       text=not binary, check=False)
    if p.returncode != 0:
        err = p.stderr if not binary else p.stderr.decode(errors="replace")
        raise RuntimeError(f"gh api {args[0]}: {err.strip()}")
    return p.stdout


def active_runs():
    """Benchmark runs holding or waiting for a runner."""
    n = 0
    for status in ("queued", "in_progress"):
        n += int(gh(f"repos/{REPO}/actions/workflows/{WORKFLOW}/runs?status={status}"
                    "&per_page=1", "--jq", ".total_count").strip() or 0)
    return n


def newest_dispatch(wf):
    return gh(f"repos/{REPO}/actions/workflows/{wf}/runs?event=workflow_dispatch"
              "&per_page=1", "--jq", ".workflow_runs[0].id // empty").strip()


def cmd_plan(plan, _a):
    for label, _c, _arm, wf, inp in jobs(plan):
        harness, args, version = legacy_command(wf, inp, plan.get("program", PROGRAM))
        image = (plan.get("legacy_images") or {}).get(version) or f"ns3:{version}"
        print(f"{label}\t{image}\t{harness} {args}")
    print(f"{len(jobs(plan))} job(s), cap {plan['cap']}, ref {plan['ref']}",
          file=sys.stderr)


def wait_new_run(wf, before, what):
    """The id of the run a dispatch just created (the API returns none)."""
    for _ in range(30):
        time.sleep(4)
        rid = newest_dispatch(wf)
        if rid and rid != before:
            return rid
    sys.exit(f"{what}: dispatched, but no new run appeared")


def build_image(plan, version):
    """Dispatch campaign-image.yml for the plan's ref; wait; its image@digest."""
    commit = gh(f"repos/{REPO}/commits/{plan['ref']}", "--jq", ".sha").strip()
    before = newest_dispatch(IMAGE_WORKFLOW)
    body = {"ref": plan["ref"], "inputs": {"ns3-version": version, "commit": commit}}
    gh("-X", "POST", f"repos/{REPO}/actions/workflows/{IMAGE_WORKFLOW}/dispatches",
       "--input", "-", stdin=json.dumps(body))
    rid = wait_new_run(IMAGE_WORKFLOW, before, "campaign image")
    print(f"campaign image: building {commit[:7]} on ns3-dev:{version}-optimized, "
          f"run {rid}", file=sys.stderr)
    for _ in range(180):  # 90 min: the reusable build's own timeout
        r = json.loads(gh(f"repos/{REPO}/actions/runs/{rid}"))
        if r["status"] == "completed":
            break
        time.sleep(30)
    else:
        sys.exit(f"campaign image run {rid} still not finished; re-run with "
                 "--image once it has")
    if r["conclusion"] != "success":
        sys.exit(f"campaign image run {rid}: {r['conclusion']}; no cell dispatched")
    with tempfile.TemporaryDirectory() as d:
        try:
            fetch(rid, d, name="campaign-image")
        except RuntimeError as e:
            sys.exit(f"campaign image run {rid} succeeded, but its image.txt could "
                     f"not be fetched ({e}); read the digest from the run's summary "
                     "and re-run with --image ghcr.io/...@sha256:...")
        with open(os.path.join(d, "image.txt")) as f:
            ref = f.readline().strip()
    if not IMAGE_RE.fullmatch(ref):
        sys.exit(f"campaign image run {rid} gave no image digest ({ref!r})")
    return ref


def campaign_image(plan, a, todo):
    """The image every cell runs in ("" = legacy), pinned once in the log."""
    logged = read_log(a.log).get(IMAGE_KEY)
    given = a.image or (NO_IMAGE if a.no_image else None)
    if a.image and not IMAGE_RE.fullmatch(a.image):
        sys.exit(f"--image must be ghcr.io/<owner>/<name>@sha256:<64 hex> (got {a.image!r})")
    if logged and given and given != logged:
        sys.exit(f"{a.log} pins this campaign to {logged}; refusing {given} "
                 "(one campaign, one binary)")
    chosen = logged or given
    if not chosen and a.build_image:
        chosen = build_image(plan, a.ns3_version)
    if not chosen:
        sys.exit("dispatch needs the campaign's image: --build-image, --image "
                 "REF, or --no-image (legacy in-job build, to replay old cells)")
    if not logged:
        with open(a.log, "a") as f:
            f.write(f"{IMAGE_KEY}\t{chosen}\n")
    if chosen != NO_IMAGE:
        versions = sorted({legacy_command(wf, inp, plan["program"])[2]
                           for _l, _c, _a, wf, inp in todo})
        print(f"every cell runs in {chosen} (the plan's version inputs "
              f"{', '.join(versions)} are not used)", file=sys.stderr)
    return "" if chosen == NO_IMAGE else chosen


def cmd_dispatch(plan, a):
    cap = a.cap or plan["cap"]
    mine = selected(plan, a)
    image = campaign_image(plan, a, mine)
    done = read_log(a.log)
    todo = [j for j in mine if j[0] not in done]
    for label, _c, _arm, wf, inp in todo:
        harness, args, version = legacy_command(wf, inp, plan.get("program", PROGRAM))
        legacy = "" if image else legacy_image(plan, version)
        while active_runs() >= cap:
            if not a.wait:
                print(f"cap {cap} reached; {len(read_log(a.log)) - 1} of "
                      f"{len(mine)} dispatched, run again later",
                      file=sys.stderr)
                return
            time.sleep(120)
        before = newest_dispatch(WORKFLOW)
        body = {"ref": plan["ref"],
                "inputs": {"harness": harness, "args": args, "version": version}}
        if image:
            body["inputs"]["image"] = image
        elif legacy:
            body["inputs"]["legacy_image"] = legacy
        gh("-X", "POST", f"repos/{REPO}/actions/workflows/{WORKFLOW}/dispatches",
           "--input", "-", stdin=json.dumps(body))
        # The dispatch API returns no run id: poll for the newest dispatched
        # run until it changes. Runs are dispatched one at a time, so the new
        # one is ours.
        rid = wait_new_run(WORKFLOW, before,
                           f"{label} (find its id and append '{label}<TAB><id>' "
                           f"to {a.log})")
        with open(a.log, "a") as f:
            f.write(f"{label}\t{rid}\n")
        print(f"{label}\t{rid}")
    print(f"all {len(mine)} job(s) dispatched", file=sys.stderr)


def cmd_status(plan, a):
    log = read_log(a.log)
    counts = {}
    for label, *_ in selected(plan, a):
        rid = log.get(label)
        if not rid:
            state = "not-dispatched"
        else:
            r = json.loads(gh(f"repos/{REPO}/actions/runs/{rid}"))
            state = r["conclusion"] or r["status"]
        counts[state] = counts.get(state, 0) + 1
        if not (a.brief and state == "success"):
            print(f"{label}\t{rid or '-'}\t{state}")
    print(", ".join(f"{v} {k}" for k, v in sorted(counts.items())),
          file=sys.stderr)


def results_text(directory):
    for name in RESULTS:
        p = os.path.join(directory, name)
        if os.path.exists(p):
            with open(p) as f:
                return f.read()
    return None


def fetch(rid, dest, name=None):
    """Download run `rid`'s artifact (the first, or the one named) into dest."""
    arts = json.loads(gh(f"repos/{REPO}/actions/runs/{rid}/artifacts"))["artifacts"]
    if name:
        arts = [x for x in arts if x["name"] == name]
    if not arts:
        raise RuntimeError(f"run {rid} has no artifact{' ' + name if name else ''}")
    data = gh(f"repos/{REPO}/actions/artifacts/{arts[0]['id']}/zip", binary=True)
    with tempfile.NamedTemporaryFile(suffix=".zip") as z:
        z.write(data)
        z.flush()
        with zipfile.ZipFile(z.name) as zf:
            zf.extractall(dest)


def out_name(plan, cell, arm):
    suffix = "" if arm == plan["control"] else f"-{arm}"
    return f"{cell}-{plan['generation']}{suffix}.txt"


def cmd_collect(plan, a):
    log = read_log(a.log)
    groups = {}
    for label, cell, arm, _wf, _inp in selected(plan, a):
        groups.setdefault((cell, arm), []).append(label)
    os.makedirs(a.out, exist_ok=True)
    written = skipped = 0
    for (cell, arm), labels in groups.items():
        bodies = []
        for label in labels:
            rid = log.get(label)
            src = os.path.join(a.src, label) if a.src else None
            if rid and not a.src:
                # Keyed by run id: two plans can share a label (#740's
                # screen and stage 2 both have retry0/scale-500-s1).
                src = os.path.join(a.cache, str(rid))
                if results_text(src) is None:
                    try:
                        fetch(rid, src)
                    except RuntimeError as e:
                        print(f"{label}: {e}", file=sys.stderr)
            text = results_text(src) if src else None
            if not rid or text is None:
                break
            if "##RUN##" not in text and "##BENCH##" not in text:
                # A run killed at its time limit still uploads a stub with
                # only the ##PROV## line; it is a missing run, not a result.
                print(f"{label}: artifact has no result rows (a timed-out "
                      "run's stub?); re-run it, or edit the cell by hand",
                      file=sys.stderr)
                break
            if "##PROV##" not in text:
                print(f"{label}: no ##PROV## line in the artifact (a run from "
                      "before #674?); the cell is not self-describing",
                      file=sys.stderr)
            bodies.append((label, rid, text))
        name = out_name(plan, cell, arm)
        if len(bodies) < len(labels):
            print(f"SKIP {name}: {len(bodies)} of {len(labels)} run(s) ready",
                  file=sys.stderr)
            skipped += 1
            continue
        runs = " / ".join(f"{rid} ({label})" for label, rid, _ in bodies)
        with open(os.path.join(a.out, name), "w") as f:
            f.write(f"# {plan['generation']} campaign {name[:-4]}: "
                    f"{len(bodies)} run(s): {runs}; ref {plan['ref']}\n")
            for _l, _r, text in bodies:
                f.write(text if text.endswith("\n") else text + "\n")
        written += 1
    print(f"wrote {written} file(s), skipped {skipped}", file=sys.stderr)


def cell_protocols(plan, cell, arm):
    """The protocols an arm's jobs of a cell run, in command-line order."""
    out = []
    for _l, c, job_arm, wf, inp in jobs(plan):
        if (c, job_arm) != (cell, arm):
            continue
        args = legacy_command(wf, inp, plan["program"])[1]
        named = re.findall(r"--protocols=(\S+)", args)
        for p in (named[-1].split(",") if named else ["anthocnet"]):
            if p not in out:
                out.append(p)
    return out


def cmd_verdicts(plan, a):
    parser = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                          "bench_parse.py")
    pairs = sorted({(cell, arm) for _l, cell, arm, *_ in jobs(plan)
                    if arm != plan["control"] and (not a.arm or arm == a.arm)},
                   key=lambda p: (p[1], list(plan["cells"]).index(p[0])))
    for cell, arm in pairs:
        base = os.path.join(a.dir, out_name(plan, cell, plan["control"]))
        test = os.path.join(a.dir, out_name(plan, cell, arm))
        if not (os.path.exists(base) and os.path.exists(test)):
            print(f"{arm}\t{cell}\t-\t(missing {os.path.basename(base)} or "
                  f"{os.path.basename(test)})")
            continue
        for proto in cell_protocols(plan, cell, arm):
            out = subprocess.run([sys.executable, parser, "--ab", "--proto",
                                  proto, base, test],
                                 capture_output=True, text=True, check=False).stdout
            line = next((ln.strip() for ln in out.splitlines()
                         if ln.strip().startswith("paired 95% CI")), None)
            print(f"{arm}\t{cell}\t{proto}\t"
                  f"{line or '(no paired result; see bench_parse.py)'}")


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    sub = p.add_subparsers(dest="cmd", required=True)
    for name in ("plan", "dispatch", "status", "collect", "verdicts"):
        s = sub.add_parser(name)
        s.add_argument("plan")
        if name in ("dispatch", "status", "collect"):
            s.add_argument("--log", required=True)
            s.add_argument("--arm", help="only this arm's jobs (one log per arm "
                           "when the arms run on different images)")
        if name == "status":
            s.add_argument("--brief", action="store_true",
                           help="list only the labels not yet successful")
        if name == "dispatch":
            s.add_argument("--cap", type=int)
            s.add_argument("--wait", action="store_true",
                           help="sleep at the cap instead of exiting")
            img = s.add_mutually_exclusive_group()
            img.add_argument("--build-image", action="store_true",
                             help="build the campaign image (campaign-image.yml) first")
            img.add_argument("--image", help="a campaign image by digest")
            img.add_argument("--no-image", action="store_true",
                             help="legacy: each cell builds the module in ns3:<version>")
            s.add_argument("--ns3-version", default="3.48",
                           help="ns3-dev version for --build-image (3.48 while #843 is open)")
        if name == "collect":
            s.add_argument("--out", required=True)
            s.add_argument("--from", dest="src")
            s.add_argument("--cache", default=".campaign-artifacts")
        if name == "verdicts":
            s.add_argument("--dir", required=True)
            s.add_argument("--arm")
    a = p.parse_args(argv)
    plan = load_plan(a.plan)
    {"plan": cmd_plan, "dispatch": cmd_dispatch, "status": cmd_status,
     "collect": cmd_collect, "verdicts": cmd_verdicts}[a.cmd](plan, a)


if __name__ == "__main__":
    main()
