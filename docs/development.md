# Developer guide

This page is how to work on AntHocNet: the tools to install, how to get the
code and its dependencies, build, test, run and lint it, the dev container,
what CI checks and how a release is cut. It is also the long form of "Build
& verify" in [`AGENTS.md`](../AGENTS.md): how coverage is measured, how an
adapter change is validated through CI (the ns-3 matrix, the sanitizer pass,
the gate groups, the heavier manual workflows), how the browser adapter and
the learn site are checked, and the checks to run before pushing a docs or
packaging change. AGENTS.md keeps the commands every change needs and says
when to read each section here; how issues and pull requests work is the
org's [CONTRIBUTING.md](https://github.com/swarm-routing/.github/blob/main/CONTRIBUTING.md).

## Prerequisites

| Tool | Version | Needed for |
| --- | --- | --- |
| Linux or macOS, `git`, `make` | any recent | everything |
| C++ compiler with C++23, CMake | a C++23 compiler (CI: GCC 13.3 on Ubuntu 24.04); CMake 3.20 or newer | `core/` and its tests (`make test`) |
| clang-format, clang-tidy | 18 (`clang-format-18`) | `make lint`, `make format`, the static checks |
| ns-3 | 3.46 or newer (the C++23 floor); CI builds on the release ns-3 supports, 3.49 today | the ns-3 module: your own tree, or the `ns3-dev` image (below) |
| Docker | any recent; about 4 GB free for an `ns3-dev` image | the dev container, `make shell` |
| Python | 3.11 or newer, with the pins in `requirements/` (`gcovr.txt`, `mkdocs-material.txt`, `ruff.txt`, `pyyaml.txt`, `matplotlib.txt`) | coverage, the docs site, the Python tools and charts |
| doxygen | any recent (CI: Ubuntu 24.04's) | the API reference |
| emsdk | 4.0.10, pinned in `.github/actions/setup-emsdk` | only the browser checks (parity, the site) |
| Node.js | 22 | only the browser smokes |

Install a pinned Python set by its file, for example `pip install
--require-hashes -r requirements/gcovr.txt` for coverage; the other files in
`requirements/` work the same way.

## Get the code

```bash
git clone https://github.com/swarm-routing/AntHocNet.git
cd AntHocNet
```

There are no submodules. The browser adapter and the learn site live in
swarm-routing/swarm-routing.github.io; `tools/checks/fetch-site-web.sh` copies
them into a git-ignored `web/` when you need the browser checks (see
"Validating the browser adapter and the learn site" below).

## Dependencies

- **swarm-core's `core/`**: its vcpkg port, in manifest mode
  (swarm-routing/.github#489): `vcpkg.json` pins the version `swarm-core.lock`
  names, `vcpkg-configuration.json` the swarm-routing/vcpkg-registry
  baseline, and `core/CMakeLists.txt` finds it with
  `find_package(swarm-core CONFIG REQUIRED)`. CI's core job builds on the
  port: `make test VCPKG_ROOT=/path/to/vcpkg` does the same locally (a
  microsoft/vcpkg checkout, bootstrapped). Without `VCPKG_ROOT`, `make test`
  compiles the same release's sources, which `tools/swarm_core.py` downloads
  and checks against the lock's SHA256 (so does every other core build in CI:
  coverage, fuzzing, CodeQL, clang-tidy, the speed benchmark). Against an
  unreleased swarm-core: `make test SWARM_CORE_DIR=/path/to/ns3-swarm-core/core`.
- **The comparison harness and the result readers**: `ns3-tools.lock` names
  one swarm-routing/ns3-tools release; `tools/ns3_tools.py` fetches it
  (`python3 tools/ns3_tools.py run bench_parse ...` runs a reader at the pin).
- **ns-3**: your own tree (`make install-ns3 NS3DIR=...`), or the org's
  prebuilt `ghcr.io/swarm-routing/ns3-dev` images, which CI and the dev
  container use.
- **The swarm-core ns-3 module**: `swarm-adapter-ns3.lock` names the
  swarm-routing/swarm-adapter-ns3 release `make install-ns3` installs as
  `contrib/swarm-core` (`tools/swarm_core.py module`). It is a lock of its
  own because swarm-core's `core/` moved out of the adapter
  (swarm-routing/swarm-adapter-ns3#231): from swarm-core v2.0.0,
  `swarm-core.lock` names swarm-routing/swarm-core's release, and the adapter
  releases on its own line. org-bot's `release-lock` task bumps it.
- **The adapter's `main`**: the ns-3 CI here builds only on the release
  `swarm-adapter-ns3.lock` names, with no canary leg (swarm-routing/.github#411). The other way
  round, swarm-routing/swarm-adapter-ns3's `consumers` job builds this
  repository's `main` on every adapter change and runs `test.py -s anthocnet`
  (report-only, swarm-routing/swarm-adapter-ns3#261), so a breaking adapter
  change shows there. By hand: `SWARM_CORE_MODULE=/path/to/swarm-adapter-ns3
  make install-ns3 NS3DIR=...` installs an adapter checkout instead of the
  lock's release (with or without a `core/` of its own).
- No lock is edited by hand: the `swarm-core / pin` check and org-bot's
  `swarm-core-pin` task move `swarm-core.lock`, the vcpkg override and
  registry baseline with it (CONTRIBUTING "Dependencies", vcpkg mode);
  org-bot's `release-lock` task moves `ns3-tools.lock` and
  `swarm-adapter-ns3.lock`.

## Build

```bash
make build                                   # core/ only (CMake), into core/build
make ns3-doctor  NS3DIR=/path/to/ns-3        # check the ns-3 tree (##DOCTOR## line)
make install-ns3 NS3DIR=/path/to/ns-3        # BASELINES=0: AntHocNet alone, no comparison arms
cd /path/to/ns-3 && ./ns3 configure --enable-examples --enable-tests && ./ns3 build
```

`make uninstall-ns3 NS3DIR=...` removes the modules again;
[ns3/README.md](../ns3/README.md) has the details. In the dev container the
modules are installed and built for you (see "Docker and dev containers").

## Test

`make test` builds `core/` and runs its ctest suite: no simulator, a few
seconds, and the check CI runs on every PR. **Always run it before
committing a core change.** The ns-3 half (the module's `test.py` suites,
the delivery smokes and the gates) is validated through CI, as below; in the
dev container `cd /opt/ns-3 && ./test.py -s anthocnet` runs the module's
suite.

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
every non-prose PR: the core **plus** the ns-3 module (`test.py -s anthocnet`,
`-s routing-bench` in `ns3-dev:latest-debug`, gcov on the anthocnet module
only through `ns3-build-contrib --coverage`; the routing-bench harness is
ns3-tools' code and measured there), merged into one gcovr report that goes to
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
  (3.46–3.48) run **on demand**: `ci.yml`
  `workflow_dispatch` with `ns3-versions` (e.g. `["3.42","3.46"]`) and optionally
  `ns3-gate-version`. 3.46 is the floor: core/ follows the C++ standard of the ns-3 release we support (its CMAKE_CXX_STANDARD_MINIMUM),
  23 from ns-3.46, 20 for 3.41–3.45, 17 for 3.40; maintainer decision 2026-11-14,
  swarm-routing/ns3-swarm-core#14). The 3.36 leg was retired earlier with this
  repo's frozen `ghcr.io/swarm-routing/ns3` package (#987). ns-3.49's stock AODV, the
  vendored AOMDV and the vendored GPSR (all three modes) livelock in a zero-time ICMP storm
  (#843, NS3-001), so every such install (routing-bench's `aodv` arm, the `aomdv`, `gpsr`,
  `gpcr` and `gpsr3d` arms, the `aodv` of `manet-baselines`, `isl-grid` and `leo-walker`,
  `leo-walker`'s `gpsr3d`) runs behind routing-bench's `IcmpErrorGuardHelper`
  (nsnam/ns-3-dev!3101's rules, swarm-routing/sim-tools#322), its streams assigned through
  the node's `GetObject<…::RoutingProtocol>`; the gates, package checks and kits run on
  `latest` with them. Published numbers pin an ns3-dev digest and a
  campaign-image digest, never a rolling tag (#866): `campaign-image.yml` builds
  one image per (commit, ns3-dev digest), every `benchmark-cell` pulls it by
  digest and records both in `##PROV##`; compare post-move numbers with
  published ones only by the cross-image rule the control campaign #867
  produced (accepted 2026-11-11, `docs/benchmarks/methodology.md`
  "Cross-image rule"): same ns-3 release, different build: compare the
  measured pair directly; for a later image, base or toolchain, first check
  byte identity with one control cell per family at the same `source_sha`,
  and run a full paired control only if that check finds a difference;
  a different ns-3 release (3.42 → 3.48 moves grid, static mesh, FANET,
  VANET, disaster and the ISL rows): re-run the published side on the new
  release, paired, and compare the two; whether that move is upstream or a
  regression is #923. Iterate on failures from the job logs —
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
  harness, on one commit pinned by its `source_sha` input whatever the
  dispatch ref, #905; with `image`, a thin caller of swarm-routing/.github's
  `reusable-benchmark-cell.yml`, the campaign-image path every new number
  takes; without, the legacy in-job build in a frozen ns-3 image, for replay
  traces and a control's legacy arm; campaigns dispatch it through
  `ns3-campaign` from swarm-routing/ns3-tools, legacy arms through
  `tools/bench/campaign.py`; it replaced
  `paper-benchmark` / `scenario-matrix` / `satellite-benchmark`, removed in
  v2.5.0, #746),
  `release.yml` (Commitizen bump + install-bundle; its images job publishes
  `anthocnet:<tag>-{optimized,debug}` through swarm-routing/.github's
  `reusable-ns3-image.yml`, FROM a pinned ns3-dev digest; this repo builds no
  ns-3 images of its own, `images.yml` was retired in #866), `campaign-image.yml` (one campaign
  image per commit; dispatch it with `commit=<40 hex>` and put the digest in
  the plan). Automatic: `charts.yml` (re-renders charts from
  committed data), `core-bench.yml` (core ns/op: PR vs base as a warning, history
  on main through a `bot/core-bench` PR; `make bench-core` locally), `links.yml` (weekly external link check; a
  red week opens a `[docs]` issue), `quality.yml` (SonarQube Cloud + Codecov,
  report-only, not part of the CI gate; see *Test coverage*), no Pages deploy here: [`swarm-routing/swarm-routing.github.io`](https://github.com/swarm-routing/swarm-routing.github.io) builds the game, docs and API from this repo at the commit in its `protocols.lock` and serves them at `/AntHocNet/`, `/AntHocNet/docs/`, `/AntHocNet/api/` (#850), `ns3-dev.yml`
  (nightly build against ns-3-dev; a red night opens a `[ci]` issue). The AI
  reviewer is CodeRabbit, a GitHub App configured by `.coderabbit.yaml` (the org's
  settings plus this file's golden rules): it reviews each PR once when it opens,
  comments only and never approves (#811, #838; the
  Claude Code Action review was removed by maintainer decision, #864). Its
  gate, `review / coderabbit`, is a required check (swarm-routing/.github#294):
  it passes once CodeRabbit has reviewed the PR or said it will not, or 30
  minutes after the PR opened, and the ruleset holds a merge while a
  CodeRabbit thread is unresolved. Releases are version-bumped by Commitizen from
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
node web/test/smoke.mjs "$PWD/web/build-site"  # needs playwright-core + axe-core in cwd
```

The site repo's `learn-site` skill has the full procedure, including
regenerating the README figures from the game.

## Run

In the dev container, or on an ns-3 tree with the modules installed:

```bash
cd /opt/ns-3                                          # or your NS3DIR
./ns3 run anthocnet-example                           # a first simulation
./ns3 run "anthocnet-compare --nNodes=20 --time=30"   # vs AODV / OLSR / DSDV
```

`anthocnet-compare` is ns3-tools' `routing-compare` under its old name
([comparison-harness.md](comparison-harness.md) has the flags and the
marker lines it prints). Benchmark numbers come from CI, not from a
laptop: one cell is the `benchmark-cell` workflow, a campaign is a plan run
by ns3-tools' `ns3-campaign` (AGENTS.md "Where to look", the
`benchmark-results` skill).

## Docker and dev containers

- **Dev container** ([`.devcontainer/`](../.devcontainer/)): open the
  repository in GitHub Codespaces or VS Code ("Dev Containers: Reopen in
  Container"). It starts from `ghcr.io/swarm-routing/ns3-dev:latest-debug`,
  pinned by digest, and `post-create.sh` installs and builds this checkout's
  modules on the image's warm ns-3 tree.
- **`make shell`**: an interactive shell in the dev image with this checkout
  at `/src`.
- **Images**: this repository builds no ns-3 image of its own; release and
  campaign images are built `FROM` an `ns3-dev` digest
  ([docker/README.md](../docker/README.md)).

## Lint and format

```bash
make lint      # clang-format-18 dry run over core/ and ns3/anthocnet/, as lint.yml
make format    # apply it
```

### Docs and release-package checks (run before pushing a docs or packaging change)

In a Claude Code on the web session, `.claude/hooks/session-start.sh` (#618)
already installed these tools at CI's pins; set `AHN_WEB=1` in the environment
to also get emsdk and playwright for the browser checks above. CI's
required checks are the `CI gate` job, `check / org-sync` (org-sync.yml) and the
review gate `review / coderabbit` (the CI map below); docs-only PRs skip the simulator jobs (#607)
but not the `pages` job, which builds the whole site, crawls it
(`tools/checks/check-site.py`) and runs the browser smoke with axe on every docs page.

```bash
python3 docs/tools/check-links.py .     # relative links resolve
python3 docs/tools/check-mermaid.py .   # mermaid blocks render on GitHub
python3 docs/tools/check-headers.py .   # SPDX headers on every source file
python3 tools/checks/check-wire-doc.py   # docs/wire-format.md names the codec's kWireVersion (#981)
python3 tools/checks/check-icmp-guard.py # every AODV/AOMDV/GPSR install behind the ICMP guard (NS3-001, #843; CI gate)
mkdocs build --strict                   # nav + anchors (needs web/shell: run tools/checks/fetch-site-web.sh first)
tools/release/check-bundle.sh           # release package: no agent files, tools present
# C++ formatting (.clang-format, clang-format-18; lint.yml fails on a diff, #35):
make format
# C++ static checks (.clang-tidy; lint.yml runs them warn-only, #619):
cmake -S core -B build-tidy/core -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
for f in core/src/*.cpp; do clang-tidy -p build-tidy/core --quiet "$f"; done
```

## CI map

| Workflow | Job (check name) | What it checks | Required |
| --- | --- | --- | --- |
| `ci.yml` (calls `ci-core.yml`, `ci-ns3.yml`, `ci-web.yml`) | `CI gate` | the one gate over `core`, `ns3` (build, unit tests, smokes, the 8 gate groups), `web` (parity, the docs and API build and its browser smoke) and the allowlists job (with `check-icmp-guard.py`, NS3-001); docs-only PRs skip the simulator jobs | yes |
| `org-sync.yml` | `check / org-sync` | the org-managed files match the templates | yes |
| `review-gate.yml` | `review / coderabbit` | CodeRabbit has reviewed the PR | yes |
| `lint.yml` | | clang-format (fails on a diff), clang-tidy (warn-only), ruff, the release-package gate, the ns3-tools pin | no |
| `pr-title.yml` | | the PR title is a lowercase Conventional Commit | no (enforced by CI) |
| `quality.yml`, `codeql.yml`, `core-bench.yml` | | coverage to Codecov and SonarQube Cloud; CodeQL; core ns/op against the base | no (report-only) |
| `licence.yml`, `swarm-core-pin.yml` | | licence headers; the swarm-core lock | no |
| `benchmark-cell.yml`, `campaign-image.yml`, `benchmarks.yml` | | one harness cell, a campaign image (dispatched) | no (manual) |
| `charts.yml`, `links.yml`, `ns3-dev.yml`, `bake.yml` | | charts from committed data; weekly link check; nightly ns-3-dev build; the bake recipe (paused) | no |
| `release.yml` | | the release (dispatched; the merged bump PR publishes) | no |

Outside this repository, swarm-routing/sim-containers' downstream canary runs
[`ci/canary.sh`](../ci/canary.sh) on every candidate `ns3-dev` image (Ubuntu
`debug`) before its `latest-*` tags move: the install, the module suites, the
example, and the `ns3-gates` steps whose verdict a release can move
(arm-delivery, every `check-anchors.sh` anchor, the determinism gates, the
satellite anchors and arm smoke; the header says which are left out for the
consumer's 3600 s budget on the `-O0` image), so a release that moves a
gate's field (#1222) holds `latest` back instead of turning this
repository's CI red. A change to one of those steps in `ci-ns3.yml` goes into
`GATES` there too. To run it
locally, as the canary does: `test/downstream.sh <image>` in sim-containers
with `DOWNSTREAM_LOCAL=AntHocNet=<this checkout>`.

The required checks are the ones the `main` ruleset lists: `gh api
repos/swarm-routing/AntHocNet/rules/branches/main`. `main` merges through a
merge queue, so a pull request is merged by enabling auto-merge.

## Release

Releases follow the org procedure: [CONTRIBUTING "Cutting a release"](https://github.com/swarm-routing/.github/blob/main/CONTRIBUTING.md#cutting-a-release),
cut only on request once the version's `release:vX.Y.Z` issues are done
([roadmap.md](roadmap.md)). Run the `Release` workflow with `mode: bump`; it
opens the bump PR, and merging that PR tags, publishes the install bundle and
the `anthocnet:<tag>` images. Record the version DOI afterwards by reading it
from Zenodo, never by typing it (AGENTS.md "Where to look", "Cut a
release").

## Troubleshooting

- **A gate or an arm hangs on ns-3.49.** ns-3.49's stock AODV, the vendored
  AOMDV and the vendored GPSR livelock in a zero-time ICMP storm (#843,
  NS3-001) unless they run behind routing-bench's `IcmpErrorGuardHelper`; a
  protocol that defers or drops unroutable packets can do the same. Install
  such an arm or example through the guard, and assign its streams through the node's
  `GetObject<…::RoutingProtocol>` (the helper's own `AssignStreams` casts
  `GetRoutingProtocol()`, which is then the guard, and assigns nothing).
- **An adapter change builds on one ns-3 release and not another.** The usual
  cause is API drift between releases; extend the `AHN_*`/`ANTHOCNET_NS3_*`
  version macros rather than forking the code (see "Validating adapter
  (NS-3) changes" above).
- **`make test` fails at configure while offline.** It downloads the
  swarm-core release; pass `SWARM_CORE_DIR=/path/to/ns3-swarm-core/core`.
- **`mkdocs build --strict` cannot find the site shell.** Run
  `tools/checks/fetch-site-web.sh` first: `web/` is fetched, not tracked.
- **`web/test/parity.sh` reports a difference.** The browser build must stay
  byte-identical to native (golden rule 9): randomness drawn into named
  locals, no libm trigonometry in mobility, `-ffp-contract=off`.
- **The `ns3-asan` job is green but its log shows leaks.** It is
  `continue-on-error` while the suppressions are calibrated; read the log.
