#!/usr/bin/env python3
# SPDX-License-Identifier: MIT OR GPL-2.0-only
# Copyright (C) 2026 Daniel Henrique Joppi
"""Legacy-build campaigns from a plan file (#662, #905): plan, dispatch,
status, collect, verdicts.

Campaigns run through `ns3-campaign` (swarm-routing/ns3-tools, its
change_plan.py, ported from this file) on benchmark-cell.yml's image path:
the org's reusable cell, every cell on a campaign image by digest and one
pinned commit (swarm-routing/.github#156). This tool is kept for the one thing
the reusable cell cannot do: the legacy in-job build, where a cell installs
and builds AntHocNet from the checkout inside a frozen ns-3 image (#867's old
arm, re-running published cells in the image their ##PROV## names).
`ns3-campaign` refuses those arms; this tool dispatches only those arms.

A campaign is cells x arms. The plan (YAML, committed in the PR that
pre-registers the campaign) names each cell's harness command line,
each arm's extra attributes, and which arm runs on which cells. Every job gets
a label, `<arm>/<cell>`, or `<arm>/<cell>-s<firstRun>` for a cell split into
seed blocks; the run-name is `<generation>/<label>`, which is how a dispatch
finds its run.

Why a tool: the v2.2.0 control arm dispatched 36 jobs at once and held every
runner, so PR CI queued for hours (#662). `dispatch` keeps at most `cap`
benchmark-cell.yml runs queued or in progress (12 of the account's 20 leaves 8
for PR CI); `ns3-campaign` counts the same workflow, so the two legs of a
control share the cap. It needs no long-lived process: each call dispatches
until the cap is reached and exits, and the label -> run id log makes it
resumable.

Usage:
    campaign.py plan     PLAN [--offline]       # list the jobs; dispatches nothing
    campaign.py dispatch PLAN --log RUNS.tsv --arm ARM [--cap 12] [--wait]
    campaign.py status   PLAN --log RUNS.tsv [--arm ARM]
    campaign.py collect  PLAN --log RUNS.tsv --out results/cells [--from DIR] [--arm ARM]
    campaign.py verdicts PLAN --dir results/cells [--arm ARM]

Every cell runs one commit (#867, #905). `source_sha` (40 hex; per arm in
`pins.<arm>.source_sha`, as ns3-campaign reads it) is passed to every cell,
which checks it out whatever ref it was dispatched at and fails unless HEAD is
that commit. `dispatch` refuses a plan without it, and the log's first line
(`#source_sha<TAB><sha>`) refuses a resumed dispatch on another commit.
`collect` rejects a cell whose ##PROV## commit= or image= is not its pin.

Legacy images, by digest: `legacy_arms` maps an arm to {version -> image}
(only those arms are legacy, so ns3-campaign can run the plan's other arms);
a plan-wide `legacy_images` map makes every arm legacy (ns3-campaign then
refuses the whole plan). Each job passes its version's image as
benchmark-cell.yml's `legacy_image`; a version the map lacks is refused, so no
cell falls back to ns3:<version>. An arm with no legacy image is refused:
dispatch it with ns3-campaign.

`collect` writes one file per (cell, arm), `<cell>-<generation>.txt` for the
control arm and `<cell>-<generation>-<arm>.txt` for the others, with a `#`
header naming the runs. The seed blocks of a split cell are concatenated, and
bench_parse.py pairs them by seed. Without --from it downloads each run's
`benchmark-cell` artifact with `gh api` into --cache (default
.campaign-artifacts/<run id>/; by run id, since two plans can share a label).
Where that cannot follow the blob redirect, fetch the artifacts another way and
pass --from DIR, holding DIR/<label>/<results file>: the rescue-artifacts
workflow's bundle_run_ids input gathers every run's artifact into one
`rescue-bundle`. `plan`, `status`, `collect` and `verdicts` also read the
committed historical plans, which predate the pins.

`verdicts` runs bench_parse.py --ab --proto P on every arm's file against the
control file of the same cell, for every protocol P the cell's command line
names (--protocols=), and prints one line per (arm, cell, protocol).

Plan format:

    generation: v230              # file suffix
    ref: main                     # the ref every job is dispatched at
    repo: swarm-routing/AntHocNet # read by ns3-campaign
    source_sha: <40 hex>          # the commit every cell runs
    program: routing-compare      # optional: the MANET program a compare
                                  # cell runs (default anthocnet-compare, #769)
    cap: 12
    control: control              # the arm whose files carry no arm suffix
    pins:                         # per arm (ns3-campaign's format)
      dev: {image: ghcr.io/...@sha256:...}
    legacy_arms:                  # the arms this tool runs, by version
      old: {3.42-opt: ghcr.io/danieljoppi/ns3@sha256:...}
    arms:
      old: ""
      dev: ""
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
      - {arms: [old, dev], cells: "*"}

A job entry's `inputs` override the cell's. An arm's attributes are appended to
the cell's `extraArgs`, then the cell's `armArgs` for that arm, if any (an arm
whose value depends on the cell, such as a radio range).

Every job is dispatched to benchmark-cell.yml (#746). A cell naming
paper-benchmark.yml or satellite-benchmark.yml (the committed v2.2.0-v2.4.0
plans) is translated into the command line that workflow built from the same
inputs, flag for flag, so an old plan re-runs the same cells.

`plan` prints one line per job: the label, the ns-3 release it runs, its
image, its source_sha and its command line (swarm-routing/ns3-tools#89, #155;
the view `ns3-campaign plan` prints). A legacy job runs the release its cell's
`version` names; an arm pinned to an image runs the release that image was
built on, read from its labels (`io.github.swarm-routing.ns3-dev.tag`, else
the `NS3_VERSION` its ns3-dev base sets) through the registry's anonymous pull
API; `--offline` (or NS3_TOOL_OFFLINE=1) prints `?`. A cell whose arms run
different releases gets a warning on stderr, the confound #867's image control
had (3.42 legacy cells against a 3.48 image); arms that all print `?` are not
compared.
"""

import argparse
import json
import os
import re
import subprocess
import sys
import tempfile
import time
import urllib.request
import zipfile

REPO = os.environ.get("AHN_REPO", "swarm-routing/AntHocNet")
# Every job runs on benchmark-cell.yml (#612, #746). A cell written for the
# retired paper-benchmark / satellite-benchmark workflows is translated into
# the harness command line that workflow built (legacy_command), so the
# committed v2.2.0-v2.4.0 plans re-run unchanged.
WORKFLOW = "benchmark-cell.yml"
SHA_KEY = "#source_sha"        # the log line pinning the arm's commit
ARTIFACT = "benchmark-cell"   # what a cell uploads
SHA_RE = re.compile(r"[0-9a-f]{40}")
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
    plan["legacy_arms"] = {arm: {str(k): v for k, v in (images or {}).items()}
                           for arm, images in (plan.get("legacy_arms") or {}).items()}
    maps = [("legacy_images", plan["legacy_images"])] + \
        [(f"legacy_arms {arm}", m) for arm, m in plan["legacy_arms"].items()]
    for where, images in maps:
        for version, ref in images.items():
            if not IMAGE_RE.fullmatch(str(ref)):
                sys.exit(f"{path}: {where} {version}: {ref!r} is not "
                         "ghcr.io/<owner>/<name>@sha256:<64 hex>")
    for arm in plan["legacy_arms"]:
        if arm not in plan["arms"]:
            sys.exit(f"{path}: legacy_arms names arm '{arm}', which the plan does not define")
    plan.setdefault("pins", {})
    for where, d in [("plan", plan)] + [(f"pins {a}", p) for a, p in plan["pins"].items()]:
        if "source_sha" in d and not SHA_RE.fullmatch(str(d["source_sha"])):
            sys.exit(f"{path}: {where}: source_sha {d['source_sha']!r} is not a 40-char commit")
    return plan


def source_sha(plan, arm):
    """The commit an arm's cells run ("" when the plan pins none)."""
    return str(plan.get("pins", {}).get(arm, {}).get("source_sha",
                                                     plan.get("source_sha") or ""))


def legacy_images(plan, arm):
    """The arm's version -> image map ({} = not a legacy arm)."""
    return (plan.get("legacy_arms") or {}).get(arm) or plan.get("legacy_images") or {}


def legacy_image(plan, arm, version):
    """The image a legacy job of this arm and version runs in."""
    images = legacy_images(plan, arm)
    if version not in images:
        sys.exit(f"arm {arm}: no legacy image for version {version}; add it, "
                 "so no cell falls back to ns3:<version>")
    return images[version]


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
    """benchmark-cell.yml runs holding or waiting for a runner (both paths,
    so ns3-campaign's cells count against the same cap)."""
    n = 0
    for status in ("queued", "in_progress"):
        n += int(gh(f"repos/{REPO}/actions/workflows/{WORKFLOW}/runs?status={status}"
                    "&per_page=1", "--jq", ".total_count").strip() or 0)
    return n


def runs_titled(title):
    """Ids of benchmark-cell.yml's dispatched runs whose run-name is `title`."""
    data = json.loads(gh(f"repos/{REPO}/actions/workflows/{WORKFLOW}/runs"
                         "?event=workflow_dispatch&per_page=100"))
    return {r["id"] for r in data["workflow_runs"] if r.get("display_title") == title}


NS3_LABEL = "io.github.swarm-routing.ns3-dev.tag"


def release(version):
    """The ns-3 release of a version string ('3.42-opt', '3.48-optimized'), or None."""
    m = re.match(r"(?:ns-)?(\d+\.\d+(?:\.\d+)?)", str(version or ""))
    return m.group(1) if m else None


def registry_config(image):
    """An image's config (labels, env), read anonymously from its registry."""
    ref, digest = image.split("@", 1)
    host, repo = ref.split("/", 1)
    with urllib.request.urlopen(f"https://{host}/token?scope=repository:{repo}:pull",
                                timeout=20) as r:
        token = json.load(r)["token"]

    def get(path, accept):
        req = urllib.request.Request(f"https://{host}/v2/{repo}/{path}",
                                     headers={"Authorization": f"Bearer {token}",
                                              "Accept": accept})
        with urllib.request.urlopen(req, timeout=20) as r:
            return json.load(r)

    single = ("application/vnd.oci.image.manifest.v1+json,"
              "application/vnd.docker.distribution.manifest.v2+json")
    manifest = get(f"manifests/{digest}",
                   "application/vnd.oci.image.index.v1+json,"
                   "application/vnd.docker.distribution.manifest.list.v2+json," + single)
    if "manifests" in manifest:  # an index: the linux/amd64 image
        one = next(m for m in manifest["manifests"]
                   if m.get("platform", {}).get("architecture") == "amd64")
        manifest = get(f"manifests/{one['digest']}", single)
    return get(f"blobs/{manifest['config']['digest']}", "*/*").get("config") or {}


_NS3 = {}


def image_ns3(image, offline=False):
    """The ns-3 release an image was built on ('?' when it cannot be read)."""
    if offline or not image or not IMAGE_RE.fullmatch(image):
        return "?"
    if image not in _NS3:
        try:  # any malformed registry answer is a release not read, never a crash
            config = registry_config(image)
            tag = str((config.get("Labels") or {}).get(NS3_LABEL, ""))
            env = dict(v.split("=", 1) for v in config.get("Env") or [] if "=" in v)
        except (OSError, ValueError, KeyError, StopIteration, TypeError, AttributeError) as e:
            print(f"warning: cannot read {image}'s labels: {e}", file=sys.stderr)
            tag, env = "", {}
        _NS3[image] = (release(tag.rsplit(":", 1)[-1]) or release(env.get("NS3_VERSION"))
                       or "?")
    return _NS3[image]


def cmd_plan(plan, a):
    releases = {}  # cell -> arm -> {ns-3 release}
    for label, cell, arm, wf, inp in jobs(plan):
        harness, args, version = legacy_command(wf, inp, plan.get("program", PROGRAM))
        images = legacy_images(plan, arm)
        if images:  # the legacy in-job build: the cell's version picks the image
            image = images.get(version, f"(no legacy image for {version})")
            ns3 = release(version) or "?"
        else:
            image = (plan.get("pins", {}).get(arm, {}).get("image", plan.get("image"))
                     or "(ns3-campaign)")
            ns3 = image_ns3(image, a.offline)
        releases.setdefault(cell, {}).setdefault(arm, set()).add(ns3)
        print(f"{label}\tns-3 {ns3}\t{image}\t{source_sha(plan, arm) or '-'}\t"
              f"{harness} {args}")
    shas = sorted({source_sha(plan, arm) or "-" for arm in plan["arms"]})
    print(f"{len(jobs(plan))} job(s), cap {plan['cap']}, ref {plan['ref']}, "
          f"source_sha {', '.join(shas)}", file=sys.stderr)
    for cell, arms in releases.items():
        values = {arm: ", ".join(sorted(v)) for arm, v in arms.items()}
        if len(set(values.values())) > 1:
            shown = ", ".join(f"{arm} {v}" for arm, v in values.items())
            unread = " (? = not read)" if "?" in values.values() else ""
            print(f"warning: {cell}: the arms run different ns-3 releases ({shown}){unread}; "
                  "a comparison of them also measures the release", file=sys.stderr)


def cmd_dispatch(plan, a):
    cap = a.cap or plan["cap"]
    mine = selected(plan, a)
    for arm in sorted({j[2] for j in mine}):
        if not legacy_images(plan, arm):
            sys.exit(f"arm {arm} has no legacy images (legacy_arms.{arm} or "
                     "legacy_images): this tool runs only the legacy in-job build; "
                     "dispatch the arm with ns3-campaign (swarm-routing/ns3-tools)")
    shas = {source_sha(plan, arm) for _l, _c, arm, _w, _i in mine}
    if len(shas) != 1 or "" in shas:
        sys.exit("every cell is pinned to one commit: set the plan's source_sha "
                 "(or pins.<arm>.source_sha), and dispatch one arm per log (--arm)")
    sha = shas.pop()
    logged = read_log(a.log).get(SHA_KEY)
    if logged and logged != sha:
        sys.exit(f"{a.log} pins this campaign to {logged}; refusing {sha} "
                 "(one campaign, one commit)")
    if not logged:
        with open(a.log, "a") as f:
            f.write(f"{SHA_KEY}\t{sha}\n")
    done = read_log(a.log)
    todo = [j for j in mine if j[0] not in done]
    for label, cell, arm, wf, inp in todo:
        harness, args, version = legacy_command(wf, inp, plan.get("program", PROGRAM))
        legacy = legacy_image(plan, arm, version)
        while active_runs() >= cap:
            if not a.wait:
                print(f"cap {cap} reached; {len(read_log(a.log)) - 1} of "
                      f"{len(mine)} dispatched, run again later",
                      file=sys.stderr)
                return
            time.sleep(120)
        title = f"{plan['generation']}/{label}"
        before = runs_titled(title)
        body = {"ref": plan["ref"],
                "inputs": {"source_sha": sha, "harness": harness, "args": args,
                           "version": version, "legacy_image": legacy,
                           "cell": cell, "label": title}}
        gh("-X", "POST", f"repos/{REPO}/actions/workflows/{WORKFLOW}/dispatches",
           "--input", "-", stdin=json.dumps(body))
        # The dispatch API returns no run id: find the new run by its run-name.
        for _ in range(30):
            new = runs_titled(title) - before
            if new:
                break
            time.sleep(4)
        else:
            sys.exit(f"{label}: dispatched, but no run named '{title}' appeared; "
                     f"find its id and append '{label}<TAB><id>' to {a.log}")
        rid = max(new)
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


def fetch(rid, dest, name=ARTIFACT):
    """Download run `rid`'s artifact `name` into dest."""
    arts = json.loads(gh(f"repos/{REPO}/actions/runs/{rid}/artifacts"))["artifacts"]
    arts = [x for x in arts if x["name"] == name]
    if not arts:
        raise RuntimeError(f"run {rid} has no artifact {name}")
    data = gh(f"repos/{REPO}/actions/artifacts/{arts[0]['id']}/zip", binary=True)
    with tempfile.NamedTemporaryFile(suffix=".zip") as z:
        z.write(data)
        z.flush()
        with zipfile.ZipFile(z.name) as zf:
            zf.extractall(dest)


def prov_problem(text, sha, images):
    """Why a cell's last ##PROV## disagrees with its pin ("" when it agrees)."""
    lines = [ln for ln in text.splitlines() if ln.startswith("##PROV##")]
    if not lines:
        return ""  # warned about separately (a run from before #674)
    prov = dict(t.split("=", 1) for t in lines[-1].split()[1:] if "=" in t)
    bad = []
    if sha and prov.get("commit") != sha:
        bad.append(f"commit {prov.get('commit', '-')}, not {sha}")
    if images and prov.get("image") not in images.values():
        bad.append(f"image {prov.get('image', '-')}, not a legacy image of the plan")
    return "; ".join(bad)


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
    rejected = []
    for (cell, arm), labels in groups.items():
        sha, images = source_sha(plan, arm), legacy_images(plan, arm)
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
            why = prov_problem(text, sha, images)
            if why:
                rejected.append((label, rid, why))
                continue
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
                    f"{len(bodies)} run(s): {runs}; ref {plan['ref']}"
                    + (f"; source_sha {sha}" if sha else "") + "\n")
            for _l, _r, text in bodies:
                f.write(text if text.endswith("\n") else text + "\n")
        written += 1
    print(f"wrote {written} file(s), skipped {skipped}", file=sys.stderr)
    if rejected:
        print("rejected (each cell must run its arm's source_sha and legacy image); "
              f"drop these labels from {a.log} and dispatch again:", file=sys.stderr)
        for label, rid, why in rejected:
            print(f"  {label}\trun {rid}\t{why}", file=sys.stderr)
        sys.exit(1)


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
    # The reader pinned in ns3-tools.lock (swarm-routing/ns3-tools#29).
    parser = [os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                           "ns3_tools.py"), "run", "bench_parse"]
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
            res = subprocess.run([sys.executable, *parser, "--ab", "--proto",
                                  proto, base, test],
                                 capture_output=True, text=True, check=False)
            if res.returncode != 0 and not res.stdout:  # the reader did not start
                sys.exit(f"bench_parse failed (exit {res.returncode}):\n{res.stderr}")
            out = res.stdout
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
            s.add_argument("--arm", help="only this arm's jobs (one log per arm)")
        if name == "plan":
            s.add_argument("--offline", action="store_true",
                           default=os.environ.get("NS3_TOOL_OFFLINE") == "1",
                           help="do not read an image's ns-3 release from its registry (prints ?)")
        if name == "status":
            s.add_argument("--brief", action="store_true",
                           help="list only the labels not yet successful")
        if name == "dispatch":
            s.add_argument("--cap", type=int)
            s.add_argument("--wait", action="store_true",
                           help="sleep at the cap instead of exiting")
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
