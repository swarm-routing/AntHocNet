# AGENTS.md — guide for AI coding agents

Operational guide for LLM agents working in this repository. Read this before
changing anything; [`CONTEXT.md`](CONTEXT.md) holds the background and history
when a task needs it. Design detail lives in
[`docs/architecture.md`](docs/architecture.md), maintenance detail in
[`docs/porting-notes.md`](docs/porting-notes.md), and the rationale behind the
structure in the [ADRs](docs/adr/).

## What this repo is

AntHocNet — an ant-colony-optimization MANET routing protocol — implemented
**once** as a simulator-agnostic algorithm core, with a thin **NS-3** adapter
(and a browser adapter for the learn site). The repo does **not** vendor a
simulator; the module installs onto *your* NS-3 tree. The NS-2 adapter was
removed in v2.0.0 ([#307](https://github.com/swarm-routing/AntHocNet/issues/307),
[ADR-0023](docs/adr/0023-one-core-one-simulator-adapter.md)); it lives on at the
v1.2.0–v1.9.0 tags.

```
core/    simulator-agnostic C++ (no simulator headers) + unit tests
ns3/     anthocnet/ (the ns-3 module + satellite harnesses), routing-bench/ (the comparison harness), baselines/<arm>/ (vendored arms), tools/
web/     (not tracked) the browser adapter + learn site, fetched from swarm-routing.github.io by tools/checks/fetch-site-web.sh (#850)
tools/   shipped scripts: bench/ (stats, parsing, scenario_check), checks/ (CI gates), release/
docs/    architecture.md, porting-notes.md, adr/, benchmarks/ — full map in docs/README.md
results/ raw benchmark data: cells/ (harness runs), campaign/ (classified CSVs)
api/     Doxygen config for the API reference (/api/)
paper/   JOSS-style paper source
.claude/ agent skills (procedures only; never shipped in a release)
```

## Build & verify

```bash
make test                                  # build core (CMake) + run ctest — fast, no simulator
make doctor       NS3DIR=/path/to/ns-3-dev # check the tree (##DOCTOR## line)
make install-ns3  NS3DIR=/path/to/ns-3-dev # install onto a real NS-3 tree (BASELINES=0: alone)
make clean                                 # remove core/build
```

### Test coverage

`core/` has an opt-in gcov build (default build is untouched):

```bash
cmake -S core -B core/build-coverage -DCMAKE_BUILD_TYPE=Debug -DANTHOCNET_COVERAGE=ON
cmake --build core/build-coverage -j
cd core/build-coverage && ctest --output-on-failure && cd -
gcovr --root . core/build-coverage --filter 'core/src/' --filter 'core/include/' \
      --exclude 'core/tests/' --txt --print-summary   # pip install gcovr
```

CI's `core-coverage` job reports the same line/branch numbers **on every PR**
(per-file table in the job log, HTML + Cobertura XML as a downloadable
artifact). It is **report-only — there is no threshold** ([#162](https://github.com/swarm-routing/AntHocNet/issues/162)); a floor gets
chosen from evidence later. Read it as a map of untested paths to aim tests at,
not as a number to chase (CLAUDE.md rule 2).

`quality.yml` (Quality) measures the wider picture on every push to `main` and
every non-prose PR: the core **plus** the ns-3 module and the routing-bench
harness (`test.py -s anthocnet`, `-s routing-bench` in
`ns3-dev:latest-debug`, gcov on those two modules only through
`ns3-build-contrib --coverage`), merged into one gcovr report that goes to
[Codecov](https://codecov.io/gh/swarm-routing/AntHocNet) (`.codecov.yml`) and,
with the merged compilation database, to
[SonarQube Cloud](https://sonarcloud.io/summary/new_code?id=swarm-routing_AntHocNet)
(`sonar-project.properties`; `ns3/baselines/` is not analysed). It is also
report-only: the `CI gate` does not wait for it, and neither service's status
is a required check. The Sonar step skips itself (with a notice) without the
`SONAR_TOKEN` secret, as on fork PRs, and (with a warning) while the
SonarQube Cloud project still has Automatic Analysis on.

`make test` is the check that runs in CI (`.github/workflows/ci.yml`) on every
push/PR and is runnable here. An actual NS-3 build needs an external simulator
tree and is **not** possible from this repo alone. **Always run `make test`
before committing a core change.**

### Validating adapter (NS-3) changes — use CI

Because you cannot build a simulator here, the **CI matrix is how you validate
an adapter change**: develop, push a branch, open a PR, and read the job
results.

- `ci-ns3.yml` (called by `ci.yml`) builds the **NS-3 module against the prebuilt GHCR image of the
  ns-3 release ns-3 supports** (e2e delivery smoke, and every gate):
  `ghcr.io/swarm-routing/ns3-dev:latest`, built by
  [`swarm-routing/ns3-containers`](https://github.com/swarm-routing/ns3-containers) and moved to each new
  ns-3 release (ns-3 maintains only its latest; org CONTRIBUTING invariant 7). Older releases
  (3.36–3.48) run **on demand**: `ci.yml` `workflow_dispatch` with `ns3-versions` (e.g.
  `["3.36","3.42"]`) and optionally `ns3-gate-version`; 3.36 uses this repo's own frozen
  `ghcr.io/swarm-routing/ns3:3.36` image (see `docker/README.md`). **Temporary exception (#843, option A):** ns-3.49's stock AODV
  and the vendored AOMDV livelock, and nearly every gate, the package checks and the kits run
  an AODV-family arm, so those run on `ns3-dev:3.48` (the `gate-version` leg) until a fixed
  ns-3 release reaches `latest`; the `latest` leg keeps the build, unit tests and the
  AntHocNet-only smokes. Revert the pin (`ci.yml`, `ci-ns3.yml`, `ci-web.yml` comments name
  it) when #843 closes. Published numbers pin an ns3-dev digest and a
  campaign-image digest, never a rolling tag (#866): `campaign-image.yml` builds
  one image per (commit, ns3-dev digest), every `benchmark-cell` pulls it by
  digest and records both in `##PROV##`; compare post-move numbers with
  published ones only after the control campaign #867. Iterate on failures from the job logs —
  cross-version API drift is the usual cause (e.g. ns-3.36's non-`const`
  `Histogram` accessors, `RouteInput` by-value vs const-ref, scoped TestSuite
  enums). The `AHN_*`/`ANTHOCNET_NS3_*` macros in the NS-3 headers + CMake
  already gate several of these by version — extend that pattern, don't fork.
- Sanitizer pass on the adapter (#130): the `ns3-asan` job rebuilds the
  ns-3 module under ASan/LSan (suppressions: `tools/checks/lsan.supp`). It is
  `continue-on-error` while suppression calibration accumulates — read its log
  even when it shows green.
- The ns-3 gates run as 8 parallel `gates <group>` jobs (`disaster`,
  `arm-delivery`, `seeds`, `partition`, `fanet`, `vanet`, `satellite`, `misc`)
  that restore the one build of the `gate-version` leg (#876): a red gate names
  its group, so read that job's log.
- Validate the **core** half locally first (`make test`); only the adapter/build
  half needs CI.
- Heavier, manual workflows: `benchmark-cell` (one harness cell, any
  harness; campaigns dispatch it through `tools/bench/campaign.py`; it replaced
  `paper-benchmark` / `scenario-matrix` / `satellite-benchmark`, removed in
  v2.5.0, #746),
  `release.yml` (Commitizen bump + install-bundle; its images job publishes
  `anthocnet-ns3:<tag>-{optimized,debug}` through swarm-routing/.github's
  `reusable-ns3-image.yml`, FROM a pinned ns3-dev digest; this repo builds no
  ns-3 images of its own, `images.yml` was retired in #866), `campaign-image.yml` (one campaign
  image per commit; `campaign.py dispatch --build-image` runs it). Automatic: `charts.yml` (re-renders charts from
  committed data), `core-bench.yml` (core ns/op: PR vs base as a warning, history
  on main through a `bot/core-bench` PR; `make bench-core` locally), `links.yml` (weekly external link check; a
  red week opens a `[docs]` issue), `quality.yml` (SonarQube Cloud + Codecov,
  report-only, not part of the CI gate; see *Test coverage*), no Pages deploy here: [`swarm-routing/swarm-routing.github.io`](https://github.com/swarm-routing/swarm-routing.github.io) builds the game, docs and API from this repo at the commit in its `protocols.lock` and serves them at `/AntHocNet/`, `/AntHocNet/docs/`, `/AntHocNet/api/` (#850), `ns3-dev.yml`
  (nightly build against ns-3-dev; a red night opens a `[ci]` issue). The AI
  reviewer is CodeRabbit, a GitHub App configured by `.coderabbit.yaml` (the org's
  settings plus this file's golden rules): it reviews each PR once when it opens,
  comments only, never approves, and is not a required check (#811, #838; the
  Claude Code Action review was removed by maintainer decision, #864). Releases are version-bumped by Commitizen from
  Conventional-Commit history; **PR titles must be Conventional Commits** (CI
  enforces it — see `CONTRIBUTING.md`).

### Validating the browser adapter and the learn site

The browser adapter, the game and the site shell live in
[`swarm-routing/swarm-routing.github.io`](https://github.com/swarm-routing/swarm-routing.github.io)
under `site/anthocnet/` (its ADR-0001). This repo's CI fetches them into a
git-ignored `web/` (`tools/checks/fetch-site-web.sh`, the site's `main`) and
keeps every gate: the `web-parity` job still fails a `core/` PR that breaks
native-vs-WASM parity, and the `pages` job still builds the docs and API under
the shell and smoke-tests them. A change to the game itself is a PR to the site
repo; a newer core reaches the published site through a `protocols.lock` bump
there. Locally (needs an activated emsdk, pinned in `.github/actions/setup-emsdk`):

```bash
tools/checks/fetch-site-web.sh          # web/ <- site/anthocnet (SITE_DIR=<checkout> to copy a local one)
web/test/parity.sh 120 "1 2 7 42"      # native vs WASM decision traces, byte-identical
web/build-site.sh                       # -> web/build-site/ (the Pages root)
node web/test/smoke.mjs web/build-site  # needs playwright-core + axe-core in cwd
```

The site repo's `learn-site` skill has the full procedure, including
regenerating the README figures from the game.

### Docs and release-package checks (run before pushing a docs or packaging change)

In a Claude Code on the web session, `.claude/hooks/session-start.sh` (#618)
already installed these tools at CI's pins; set `AHN_WEB=1` in the environment
to also get emsdk and playwright for the browser checks above. CI's one
required check is the `CI gate` job; docs-only PRs skip the simulator jobs (#607)
but not the `pages` job, which builds the whole site, crawls it
(`tools/checks/check-site.py`) and runs the browser smoke with axe on every docs page.

```bash
python3 docs/tools/check-links.py .     # relative links resolve
python3 docs/tools/check-mermaid.py .   # mermaid blocks render on GitHub
python3 docs/tools/check-headers.py .   # SPDX headers on every source file
mkdocs build --strict                   # nav + anchors (needs web/shell: run tools/checks/fetch-site-web.sh first)
tools/release/check-bundle.sh           # release package: no agent files, tools present
# C++ formatting (.clang-format, clang-format-18; lint.yml fails on a diff, #35):
make format
# C++ static checks (.clang-tidy; lint.yml runs them warn-only, #619):
cmake -S core -B build-tidy/core -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
for f in core/src/*.cpp; do clang-tidy -p build-tidy/core --quiet "$f"; done
```

## Golden rules (invariants — do not break)

1. **`core/` must never include a simulator header** (ns-3, or the NS-2 it
   once also ran on). Time comes through
   `IClock`, randomness through `IRng`, neighbours through `INeighborProvider`,
   deferred work through `ITimerScheduler` (see `core/include/.../ports.h`).
   This is the property that keeps one algorithm working on every adapter —
   ns-3 and the browser today (ADR-0021, ADR-0023).
2. **Adapters must not reimplement routing logic.** An adapter only: converts
   its packet header ⇄ `core::AntMessage`, carries out the `RouteDecision`s the
   core returns, owns the periodic timers and the pending-packet queue.
   Behaviour belongs in `core/`.
3. **All randomness via `IRng`, all time via `IClock`.** Never call libc
   `rand()` or read the simulator clock directly from shared logic —
   reproducibility depends on this.
4. **One canonical wire format, versioned.** `core/ant_message_codec` defines the
 little-endian layout, prefixed by a 1-byte `kWireVersion` (see
 `docs/wire-format.md` and ADR-0006). If you change `AntMessage` fields — or the
 units/semantics of an existing field — you must **bump `kWireVersion`** and
 update, in the same field order: the codec, the NS-3 header (`ns3/anthocnet/model/anthocnet-packet`
 `AntHeader`), the round-trip tests (`core/tests/test_codec.cpp`, NS-3 test
 suite), and the layout table in `docs/wire-format.md`.
5. **Keep bounded structures bounded.** The visited path and the `(src,seq)`
   dedup history are capped by `Config::maxPathLength` / `maxHistory`; do not
   reintroduce unbounded growth.
6. *(Retired at v2.0.0.)* NS-2 patching was anchor-based, never line-numbered
   ([ADR-0005](docs/adr/0005-ns2-idempotent-anchor-patch.md), now historical);
   the NS-2 adapter is gone. The number is kept so "golden rule 7" keeps
   meaning what every existing reference says.
7. **Cover core logic changes with a core unit test** in `core/tests/`.
8. **Per-network behaviour is a gated mechanism, never a preset** (ADR-0019,
   ADR-0020). A new mechanism ships behind a default-off `Config` switch, the
   default path stays byte-identical (determinism check), and it is A/B'd on
   identical seeds in *every* family before any default moves.
9. **The browser adapter stays deterministic and decision-free** (ADR-0021).
   No routing logic in the browser adapter (`web/`, now
   `swarm-routing.github.io` `site/anthocnet/`; this repo's CI still runs its parity on every PR). Draw randomness into named locals (argument
   evaluation order differs between compilers), keep mobility to arithmetic
   (`detSin`/`detCos`, never libm trig), keep `-ffp-contract=off`.
   `web/test/parity.sh` must stay byte-identical.
10. **Nothing shipped depends on `.claude/`** (ADR-0014 amendment, #604). A
    script that a shipped tool, workflow or doc needs lives in `tools/`; skills
    hold procedures. `tools/release/check-bundle.sh` enforces it in CI.

## Conventions

- C++14 for `core/` (aggregate init with default member initializers).
- Namespace `anthocnet::core` for shared code.
- Epics are listed where contributors look. In a public repository, this
  README's `## Epics` section and the org
  [ROADMAP](https://github.com/swarm-routing/.github/blob/main/ROADMAP.md#epics)
  are generated from the issues labelled `epic` by `swarm-routing/.github`'s
  `tools/epics/epics.py`. An open epic is Planned (no sub-issue closed) or In
  progress (some closed); it becomes Done or Dropped only when you close the
  epic itself (as completed or as not planned), even if every sub-issue is
  closed. Whenever you change an epic (open, close, rename or re-scope it, add
  or remove its `epic` label) or one of its sub-issues (close, reopen, add,
  unlink), refresh the lists in that PR or the next one. You need
  `swarm-routing/.github` checked out next to this repository and `gh` logged
  in (or `GH_TOKEN` set). Run `python3 ../.github/tools/epics/epics.py readme .`
  in the repository that owns the epic (for a sub-issue of another
  repository's epic, that repository, not this one), then
  `python3 tools/epics/epics.py roadmap ROADMAP.md` and
  `python3 tools/epics/epics.py profile profile/README.md` in `.github`. Never
  edit a generated section by hand; the weekly `epics` workflow in `.github`
  only reports drift. Private repositories (such as `papers`) are never listed
  and keep no generated section.
- Make minimal, reviewable changes; update the relevant doc/ADR when you change
  a documented decision.
- When you open or update an issue, apply the label taxonomy from
  [ADR-0013](docs/adr/0013-track-bugs-and-findings-as-issues.md#labelling-convention):
  one type label (`bug`/`enhancement`/`chore`/`documentation`/`verification`,
  plus `epic` for umbrellas), area label(s) (`protocol`/`adapter`/`ns3`/
  `benchmark`/`observability`/`packaging`/`learn-site`), one `model:*`
  recommendation, one `priority:P1|P2|P3` label on non-epic issues, and a
  `release:vX.Y.Z` label only when the roadmap ladder gates the issue.
- Issue bodies follow one shape: **Context** (the evidence, with numbers and
  links), **Scope**, **Acceptance criteria** (checkboxes), **Depends on**
  (issue numbers), **References**. Children of an epic say "Part of #N" and are
  linked as sub-issues. Comments you post end with the Claude Code footer.
- A new comparator arm (a protocol we did not design) must clear three gates
  before its numbers count: a fidelity sheet from its paper, the per-PR
  delivery smoke (#439), and an anchor reproducing its own paper's trend. If it
  cannot, it ships as a written infeasibility verdict (see #563).
  The `.github/ISSUE_TEMPLATE/` forms preset the type label for issues filed
  from the GitHub UI.

## Git workflow

- Branch from `main` (`claude/<short-topic>`), one focused change per PR.
- **Always open a PR to `main`** once the branch is pushed — every change is
  tracked through one (org rule; maintainer decision 2026-11-10).
- **PR titles are Conventional Commits** (`feat:`/`fix:`/`docs:`/…) — CI enforces
  it, and since PRs are squash-merged the title is the commit on `main` and
  drives the next version bump. See `CONTRIBUTING.md`.
- Push with `git push -u origin <branch>`; open a PR; merge when CI is green.
- `main` takes no direct push, bots and releases included: they open PRs from
  `bot/<name>` branches (#879, #884; `CONTRIBUTING.md` "Branch protection").
  When a PR falls behind `main`, update it
  (`gh api -X PUT repos/swarm-routing/AntHocNet/pulls/<n>/update-branch`) and
  merge PRs one at a time; never force-push a branch someone else pushed.

## Where to look

| You want to… | Go to |
|--------------|-------|
| Understand the design & decision flow | `docs/architecture.md` |
| Check a claim/parameter against the papers | `docs/publications/` (digests of [1] + thesis status) |
| See what v1.0 reproduces / deviates from the paper | `docs/fidelity.md` |
| Understand a structural decision / its "why" | `docs/adr/` |
| **Pick up open work** | GitHub issues — start with the highest open `priority:P*` label (query live; see `CONTEXT.md` §10) |
| **Know which release a piece of work serves, or why something is *not* planned** | [`docs/roadmap.md`](docs/roadmap.md) — the ladder v2.1.0 → v3.3.0 ([replan accepted 2026-10-09](docs/roadmap.md#replan-after-v200-accepted-2026-10-09)), per-release exit criteria and the non-goals; issues carry `release:` labels. Epics: #626 the whole site is the game (v2.1.0), #634 the Workshop (API + ns-3 in the game), #562 game campaign, #563 comparators, #564 wider threat model, #328 packaging and infrastructure (#603 closed into it). History and the original gap analysis: [#298](https://github.com/swarm-routing/AntHocNet/issues/298) |
| Know what research a plan item rests on (families, mechanisms, comparators, secure protocols) | [`docs/research-landscape-2026.md`](docs/research-landscape-2026.md) — abstract-level; read the paper before citing a number |
| Work on the learn-site game / campaign | [`docs/learn-campaign.md`](docs/learn-campaign.md): campaign design, levels, UI, adapter features; §10 the whole site as the game; §11 the Workshop (API + ns-3 in the game). Plus the `learn-site` skill. Epics #562 (campaign), #626 (site), #634 (Workshop) |
| Change what a release ships, or the install path | `tools/release/` (assemble + gate), `release.yml`, `ns3/Makefile`; infrastructure epic #328 (was #603) |
| Understand the ant types (what each one is for, what it writes, which switch gates it) | [`docs/ant-types.md`](docs/ant-types.md) — comparison table + lifecycle diagrams for Hello / Reactive / Proactive / Repair / LinkFail and the backward ant |
| See the whole stack, or which mechanism is live/inert in a given regime | [`docs/software-layers.md`](docs/software-layers.md) — core → ports → adapters → harnesses, mechanisms × their config switches, and per-regime support |
| **Add support for a new network family** (FANET, VANET, …) | [ADR-0019](docs/adr/0019-network-families-change-the-evaluation-not-the-protocol.md) — a family is a *scenario* concern: mobility model, preset, preflight rules, anchor, metrics. **Never** family-specific protocol defaults; a mis-sized constant is an issue + A/B, not a preset. Tracks: [#300](https://github.com/swarm-routing/AntHocNet/issues/300), [#301](https://github.com/swarm-routing/AntHocNet/issues/301) |
| Work on security / trust mechanisms | [ADR-0020](docs/adr/0020-security-is-a-default-off-profile.md) — same implementation, behind attributes, **default off**, default path provably byte-identical; no fork. Track: [#302](https://github.com/swarm-routing/AntHocNet/issues/302) (v3.0.0) |
| Record a bug / finding, or hand off across sessions | [ADR-0013](docs/adr/0013-track-bugs-and-findings-as-issues.md): always open or update an issue. **A handoff is always an issue**, labelled `handoff` (template: *Handoff*), never a file in the repo: before a session ends with work in flight, or when asked for a handoff, open one with the state, the ordered next steps, the procedures and the pitfalls. A new session starts from the newest open `handoff` issue. |
| Maintain the wire format | `docs/wire-format.md`, `docs/porting-notes.md` |
| Change the algorithm | `core/src/`, `core/include/anthocnet/core/` |
| Change routing policy / decision flow | `core/src/ant_router_logic.cpp` |
| Change pheromone math | `core/src/pheromone_engine.cpp`, `pheromone_table.cpp` |
| Change the wire format | `docs/wire-format.md` → `core/include/.../ant_message_codec.h` (+ the ns-3 header; bump `kWireVersion`) |
| Find the removed NS-2 adapter | [`docs/ns2-support.md`](docs/ns2-support.md) — the v1.2.0 / v1.9.0 tags and images |
| Work on the NS-3 adapter | `ns3/anthocnet/model/`, `ns3/anthocnet/helper/`, `ns3/anthocnet/examples/` |
| Work on the browser adapter (the learn site's simulation) | `swarm-routing/swarm-routing.github.io` `site/anthocnet/` (its README, [ADR-0021](docs/adr/0021-the-browser-is-an-adapter.md)); fetched here as `web/` by `tools/checks/fetch-site-web.sh`; `web/test/parity.sh` must stay byte-identical native vs WASM (CI job `web-parity`) |
| Run / read benchmarks | `docs/benchmarks.md` (index → `docs/benchmarks/{metrics,methodology}.md`, `scenarios/<name>.md`, `sweeps/<name>.md`), `ns3/tools/run-scenarios.py` + `make-charts.py` + `update-benchmarks.py`; family/cross-family + sweep charts from committed data: `ns3/tools/family-charts.py` (re-rendered by `charts.yml`, which proposes them in a `bot/charts` PR; no workflow pushes to `main`, CONTRIBUTING "Branch protection"), `anthocnet-compare --diag` |
| Inspect protocol internals | NS-3 `Tx`/`Rx`/`RouteChanged` trace sources; core counters via `IRouterObserver` |
| Run the benchmark campaign loop (dispatch → fetch → parse) | `benchmark-results` skill (SKILL.md documents the whole procedure) |
| Run a campaign (cells × arms, throttled so PR CI keeps runners) | `tools/bench/campaign.py` + a plan in `tools/bench/campaigns/` (#662): `plan`, `dispatch`, `status`, `collect`, `verdicts` |
| Run the comparison harness, or add a protocol (arm) to it | [`docs/comparison-harness.md`](docs/comparison-harness.md) — running `routing-compare` (alias `anthocnet-compare`), markers, readers, the `adaptive` `RoutingBase` hooks, registering an arm, the three gates. The harness is the `routing-bench` module (`ns3/routing-bench/`, its README); each arm module registers through `helper/<module>-bench-arms.cc`. The design (arm contract, hook order, explicit registration, compatibility promise): [ADR-0027](docs/adr/0027-the-comparison-harness-is-protocol-agnostic.md) (epic #764) |
| Compare benchmark A/B runs (deltas + noise verdict) | `benchmark-results` skill (`tools/bench/bench_parse.py`) |
| Build, test or screenshot the learn site | the `learn-site` skill in `swarm-routing/swarm-routing.github.io` (parity, smoke, README figures) |
| Change the site's top bar, places or the docs/API look | the site repo's `site/anthocnet/shell/` (one source for game, docs and API; `inject.py`), `docs/places/`, the nav in `mkdocs.yml`; checked by `web/test/smoke.mjs` on `web/build-pages.sh` output |
| Add a world, a mission or a core class | it needs its docs links (`read:` / `links:`), a family page needs `try:` front matter, a public core class needs a chamber in the site's `site/anthocnet/site/js/nest-data.js`, added by the site PR that bumps `protocols.lock` (CI here only warns about it, `--warn-nest`): `python3 tools/checks/check-site-links.py` says what is missing |
| Summarize / export campaign sweep CSVs | `benchmark-results` skill (`tools/bench/sweep_summary.py`; `--export-sweeps` feeds the papers repo) |
| Validate a scenario config or result plausibility | `benchmark-results` skill (`tools/bench/scenario_check.py`, #134): `preflight` before dispatching, `results [--anchor …]` before trusting numbers |
| Pre-push invariant check on a diff | `anthocnet-review` skill (`.claude/skills/anthocnet-review/check_invariants.sh`) |
| Understand why skills are script-first (and add a new analysis loop) | [ADR-0014](docs/adr/0014-agent-skills-are-script-first.md) — raw data stays out of LLM context; extend a skill script, don't eyeball |
| Work on satellite / ISL topologies | [ADR-0015](docs/adr/0015-satellite-substrate-lives-in-the-image.md) — **there is no separate satellite build**: one binary, scenario differences live in examples and flags (`isl-grid` static, `leo-walker` moving). [ADR-0022](docs/adr/0022-satellite-substrate-is-stock-ns3-leo.md) — the substrate is stock ns-3.48's LEO model, so moving-constellation work is 3.48-only. Track: [#192](https://github.com/swarm-routing/AntHocNet/issues/192) |
| **Understand why satellite ≠ MANET** (before assuming a MANET intuition transfers) | [`docs/network-regimes.md`](docs/network-regimes.md) — the inversion in one line: MANET hides the *topology* and gives you the traffic; a constellation gives you the topology and hides the *traffic*. Diagrams, and a difference table where every row traces to a defect, parameter or control in this repo |
| Cut a release | run the `Release` workflow (Commitizen) with `mode: bump`; it opens the bump PR, and merging that PR tags and publishes (#882); see `CONTRIBUTING.md`. **Post-release, record the version DOI** — Zenodo mints it async a few minutes after publish; read it from the public API (`curl -sL https://zenodo.org/api/records/20981980/versions/latest \| python3 -c "import json,sys; d=json.load(sys.stdin); print(d['metadata']['version'], d['doi'])"`; address it through a *version* record — the concept ID 20981979 returns 404 there — and check the version printed is the one you released), or ask the maintainer if `zenodo.org` is proxy-blocked (403, as it was until 2026-09). Never invent one. Then append a row to the DOI table in `CONTRIBUTING.md` via a `docs:` PR — the README badge and `CITATION.cff` stay on the concept DOI (#116) |
| Tune defaults | `core/include/anthocnet/core/config.h` |
| **Look up a parameter's default, its provenance, or how to calibrate it** | [`docs/configuration.md`](docs/configuration.md) — all 38 `Config` fields with a `source` column ([1] §/thesis/repo choice/**unknown**), the ns-3 attribute for each, the sweep→A/B→noise loop, and the checklist for adding a parameter. The `unknown` rows in §3.2 are the next #88/#169 candidates |
