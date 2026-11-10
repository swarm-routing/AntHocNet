---
name: benchmark-results
description: Run the full AntHocNet benchmark loop — pre-flight-validate scenario configs, dispatch benchmark-cell runs and campaign plans, fetch results cheaply, check result plausibility and anchor floors, parse and A/B-compare runs, and summarize classified campaign CSVs — with automatic run-to-run-noise verdicts. Use whenever dispatching benchmark workflows, validating a scenario or its results, or collecting/comparing AntHocNet benchmark numbers (including results/campaign/*.csv sweeps) so validation, parsing, deltas, filtering, and the noise call are done by scripts instead of by hand in context.
---

# benchmark-results

> **Where the scripts live:** `tools/bench/` — not in this skill directory. They
> ship with every release (the shipped `ns3/tools/` harness tools import them),
> while `.claude/` is excluded from release packages (#604). This file is the
> *procedure*; the scripts are product code with their own CI (ruff, the two
> self-tests in `lint.yml`).

Turns raw `anthocnet-compare` output into a compact delta grid with a noise
verdict, using `bench_parse.py`. All arithmetic and the
noise call happen in the script — your context only sees the summary.

## Getting the numbers out of CI (the constraint)

Scripts here **cannot** download CI logs or artifacts — the sandbox proxy blocks
the blob hosts (`api.github.com` JSON reads work; log/artifact/zip 403). So the
result text must come through `mcp__github__get_job_logs`. Two ways to keep that
fetch cheap:

- **Preferred:** collect the run's artifact (`ns3-campaign collect` or
  `campaign.py collect`, or `open_pr=true` on the dispatch). The artifact's `results.txt` is the
  harness output plus `##ARGS##` and `##PROV## commit=…` (#365, #674).
- **Log tail:** `benchmark-cell.yml`'s last step prints a compact block (the
  `##RUN##` and table rows, the scenario_check verdict, then `##PROV##`), so
  `get_job_logs` with `tail_lines: 45` captures the numbers. `##PROV##` is the
  commit the cell was measured at. Keep it in whatever you save: it is what makes the
  saved file self-describing after the run's logs expire. The artifact's
  results file ends with the same line (#674), so a cell saved from the
  artifact needs no provenance rebuilt from the run record.
- Otherwise fetch ~55 tail lines (the human table sits just above the
  upload/cleanup noise) and save it.
- **Whole log to a file, not to context:** `mcp__github__get_job_logs` with
  `return_content: false` returns a short-lived signed URL; `curl -sSL "$URL"
  -o job.log` then works (the signed host is reachable even though `gh api`
  cannot follow the blob redirect). Use it for long cells (leo-walker,
  campaign logs) and grep/extract by script.

Save each run's text to a file (one file per run); a leading `# <label>` line
names the cell. Then run the parser — never eyeball tables or compute deltas by
hand.

## Commands

```bash
S=tools/bench/bench_parse.py
python3 $S off.txt on.txt                 # deltas of every cell vs the first
python3 $S --ab off1 on1 off2 on2         # (off,on) pairs; flags cross-pair sign
python3 $S --all a.txt b.txt              # every protocol, not just anthocnet
python3 $S --proto aodv a.txt b.txt       # compare a different protocol
```

`--ab` is the money mode for `EnableMacMetric` sweeps: it computes on-vs-off
within each load pair and, if the PDR deltas disagree in sign across pairs,
prints the **NOISE** call (the exact trap hit in #47/#68/#71 — opposite
directions at low run counts mean noise, bump `--runs`).

Each delta line shows `dPDR / d_delay / d_d99 / d_NRL` (d_d99 = 99th-pct delay,
often the real signal — e.g. A2's win was flat PDR but −20% d99). Verdict is
multi-signal over the material deltas (PDR up good; d99 and NRL down good):
`NOISE` if none are material, `IMPROVED`/`WORSE` if they agree, `MIXED` if they
conflict. So a flat-PDR run whose tail and overhead both drop reads IMPROVED, not
MIXED. Treat a single pair's verdict at <5 runs with suspicion — confirm with
more runs before concluding.
Cells with `##RUN##` per-seed rows also get a `95% CI` line per protocol and,
per comparison, a `paired 95% CI (n=..)` line whose `PAIRED-*` verdict
**replaces** the materiality verdict. A column-mapping self-check FAIL exits 1:
never quote numbers from that cell. Details: [reference/statistics.md](reference/statistics.md).

## Dispatching runs

Every cell runs one pinned commit (#905, swarm-routing/.github#156):
`benchmark-cell.yml` takes `source_sha` (40 hex), checks it out whatever ref
it was dispatched at, fails unless HEAD is that commit, and stamps it as
`##PROV## commit=`. Two paths, chosen by `image`:

- **`image`** (a campaign image by digest, built by `campaign-image.yml` from
  `source_sha`): the job is the org's `reusable-benchmark-cell.yml`. It checks
  the image's labels name this repository at `source_sha`, runs the harness
  with `--no-build`, and a `verdict` job adds the scenario_check verdict.
  Inputs: `image`, `source_sha`, `harness` (routing-compare | anthocnet-compare
  | isl-grid | leo-walker), `args` (the harness's own flags verbatim), `seeds`
  (`<first>-<last>`, optional), `cell`, `label`. This is the path for every
  new number.
- **no `image`** (the legacy in-job build): builds `source_sha` inside
  `ghcr.io/swarm-routing/ns3:<version>` or `legacy_image` (by digest). Only for
  replay traces (`replay`, `published`) and a control's legacy arm (#867).

A single run: `mcp__github__actions_run_trigger` on `benchmark-cell.yml` with
those inputs. To build an image for a commit, dispatch `campaign-image.yml`
with `commit=<40 hex>` (and `ns3-version`, 3.48 while #843 is open); its
summary and `campaign-image` artifact give the digest. `open_pr=true` with
`cell=<name>` opens a PR adding `results/cells/<name>.txt`.
paper-benchmark / satellite-benchmark / scenario-matrix were removed in v2.5.0
(#746); both campaign tools still translate plans that name them.
Pair every ON run with an identical OFF run for a clean A/B (baselines are
deterministic on identical seeds).

**A campaign (more than a handful of runs) goes through `ns3-campaign` and a
committed plan** (#662, #905). `ns3-campaign` is swarm-routing/ns3-tools'
runner (`pip install ./python` in a checkout of it; or run
`python/ns3_tool/change_plan.py` directly), a port of `tools/bench/campaign.py`.
The plan is `tools/bench/campaigns/<release>.yaml`: cells, arms and which arm
runs where, plus `repo`, `source_sha` and `image` (or per arm
`pins.<arm>: {source_sha, image}`), all required to dispatch. Cells written
for the retired workflows are translated to the same command line (`plan`
prints it). The plan is committed in the PR that pre-registers the campaign.

```bash
P=tools/bench/campaigns/<plan>.yaml
ns3-campaign plan $P                            # review the job list; sends nothing
ns3-campaign dispatch $P [--arm A]              # cells up to the cap (12); state in .campaign/<generation>/
ns3-campaign status $P --brief                  # only labels not yet green; counts
ns3-campaign collect $P --out results/cells     # rejects a cell whose ##PROV## is not its pin
ns3-campaign collect $P --out results/cells --redispatch   # ... and re-queues those labels
ns3-campaign verdicts $P --dir results/cells    # bench_parse --ab per arm x cell x protocol
```

- **A legacy arm goes through `tools/bench/campaign.py`.** `ns3-campaign`
  refuses an arm with no image (and a plan with a plan-wide `legacy_images`).
  An arm in the plan's `legacy_arms` (arm -> {version -> `ghcr.io/...@sha256:...`})
  runs the in-job build in the image a published cell's `##PROV##` names; only
  `campaign.py` dispatches it, one log per arm, and it refuses a plan without
  `source_sha`. `campaigns/v2.7.x-image-control.yaml` (#867) is the example:
  ```bash
  python3 tools/bench/campaign.py dispatch $P --arm old --cap 12 --log old.tsv
  python3 tools/bench/campaign.py status   $P --arm old --log old.tsv --brief
  python3 tools/bench/campaign.py collect  $P --arm old --log old.tsv --out results/cells
  ```
  Both tools count every `benchmark-cell.yml` run against the cap, so two legs
  together hold at most `cap` runners.
- **The cap leaves PR CI its runners.** v2.2.0's phase 1 dispatched 36 jobs at
  once and PR CI queued for hours. Never hand-dispatch a campaign.
- **`dispatch` is resumable.** It skips labels already logged and exits
  at the cap. Re-run it from a scheduled check-in every 60–90 min; an agent's
  background tasks die at 2 h, and a campaign takes about 10 h. A resumed
  dispatch refuses a different commit or image than the one it logged.
- **A check-in is `dispatch` then `status --brief`** — two calls, a few
  lines of output. Read full `status` only when a label needs its run id.
- **`collect` refuses a stub.** A run killed at its time limit uploads an
  artifact with only `##PROV##`; collect reports it and skips the cell. Re-run
  the label, or, if the maintainer accepts fewer seeds, write the cell by hand
  and say so in its header (scale-500-olsr-v240, n=19).
- **Artifacts are kept 7 days** on the image path: collect within the week.
- **`collect --from DIR`** reads `DIR/<label>/<results file>`, for an
  environment where `gh api` cannot follow the artifact's blob redirect.
  There, get each artifact's signed URL with
  `mcp__github__actions_get` (`download_workflow_run_artifact`) and `curl` it,
  changing `\u0026` to `&`.

## The full campaign loop (dispatch → fetch → parse)

The cross-session procedure (formerly buried in issue #91's session notes):

0. **Pre-flight** the scenario before spending a dispatch (#121 budget):
   `scenario_check.py preflight` with the knobs you intend to override —
   flags partitioned fields, channel saturation, accidental single-hop,
   too-short sim time (see below).
1. **Dispatch** `benchmark-cell.yml` (one cell) or a campaign plan via
   `ns3-campaign` (a sweep is a set of cells), from `main`: the plan's
   `source_sha` decides what runs, not the dispatch ref.
2. **Wait** — a real cell can exceed an hour (job timeout 360 min).
   Poll `mcp__github__actions_get` occasionally or schedule a check-in; do
   not spin.
3. **Fetch cheap.** The artifact (`ns3-campaign collect`, or the bot PR from
   `open_pr=true`); otherwise the log tail above. Save each run verbatim, one
   file per run. Older `results/campaign/*.csv` sweeps came from the retired
   scenario-matrix workflow and stay readable by `sweep_summary.py`.
4. **Validate before comparing**: `scenario_check.py results` on the saved
   cell/CSV — plausibility invariants (PDR bounds, delay99 ≥ mean, negative
   metrics) and, for anchor-shaped scenarios, the `ns3/tools/anchors.yml`
   floors (#59). A FAIL here means harness/channel regression (#51-class):
   do not compare, publish, or quote the numbers. It also echoes the cell's
   `##PROV##` commit (#365) and WARNs when a log cell has none — a number whose
   version is unknown is not publishable, and the usual cause is a tail cut
   above the line rather than a workflow that failed to emit it.
5. **Parse by script, never by eye**: `bench_parse.py` for harness cells,
   `sweep_summary.py` for campaign CSVs (below).
6. **Attribution control** when a campaign changes code between two
   generations: `bench_parse.py --control BEFORE AFTER` on a cell run with
   every protocol. Every baseline's `##RUN##` rows must be identical; stock
   AODV on a satellite cell may read ORDER-ONLY (#675, heap-ordered sockets),
   anything else that moved is a FAIL and the delta is not attributable.
7. **Record** the verdict + run IDs on the relevant issue (ADR-0013).
## Campaign CSVs and satellite cells

```bash
python3 tools/bench/sweep_summary.py results/campaign/*.csv          # anthocnet vs aodv per point
python3 tools/bench/sweep_summary.py AFTER.csv --vs BEFORE.csv       # what did our change do (AFTER first)
python3 ns3/tools/leo-summary.py walker16 starlink1 starlink1-storm  # leo-walker tables, paste verbatim
```

`--vs` exits 1 when a baseline moved between generations: the delta is not
attributable. Flags, the paired branch and the satellite tables:
[reference/sweeps.md](reference/sweeps.md).

## Scenario validation (`scenario_check.py`, #134)

```bash
S=tools/bench/scenario_check.py
python3 $S preflight [--knob value ...]           # before any dispatch; exit 1 on FAIL
python3 $S preflight --harness isl-grid ...       # satellite cell
python3 $S results cell.txt [--anchor NAME]       # before trusting any number
python3 tools/bench/test_scenario_check.py        # the gate's own tests (~1 s)
```

A `results` FAIL is a harness regression: fix the harness before you compare,
publish or quote anything from that run. Every rule, the path-diversity
churn floor, and what a new metric must ship with:
[reference/scenario-check.md](reference/scenario-check.md).
