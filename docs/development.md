# Build, test and verify in detail

The long form of "Build & verify" in [`AGENTS.md`](../AGENTS.md): how coverage is
measured, how an adapter change is validated through CI (the ns-3 matrix, the
sanitizer pass, the gate groups, the heavier manual workflows), how the browser
adapter and the learn site are checked, and the checks to run before pushing a
docs or packaging change. AGENTS.md keeps the commands every change needs and
says when to read each section here.

# Test coverage

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

# Validating adapter (NS-3) changes — use CI

Because you cannot build a simulator here, the **CI matrix is how you validate
an adapter change**: develop, push a branch, open a PR, and read the job
results.

- `ci-ns3.yml` (called by `ci.yml`) builds the **NS-3 module against the prebuilt GHCR image of the
  ns-3 release ns-3 supports** (e2e delivery smoke, and every gate):
  `ghcr.io/swarm-routing/ns3-dev:latest`, built by
  [`swarm-routing/ns3-containers`](https://github.com/swarm-routing/ns3-containers) and moved to each new
  ns-3 release (ns-3 maintains only its latest; org CONTRIBUTING invariant 7). Older releases
  (3.40–3.48, the `ns3-dev` images ns3-containers builds) run **on demand**: `ci.yml`
  `workflow_dispatch` with `ns3-versions` (e.g. `["3.42","3.46"]`) and optionally
  `ns3-gate-version`. 3.40 is the floor: the 3.36 leg was retired with this repo's frozen
  `ghcr.io/swarm-routing/ns3` package (#987). **Temporary exception (#843, option A):** ns-3.49's stock AODV
  and the vendored AOMDV livelock, and nearly every gate, the package checks and the kits run
  an AODV-family arm, so those run on `ns3-dev:3.48` (the `gate-version` leg) until a fixed
  ns-3 release reaches `latest`; the `latest` leg keeps the build, unit tests and the
  AntHocNet-only smokes. Revert the pin (`ci.yml`, `ci-ns3.yml`, `ci-web.yml` comments name
  it) when #843 closes. Published numbers pin an ns3-dev digest and a
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
  comments only, never approves, and is not a required check (#811, #838; the
  Claude Code Action review was removed by maintainer decision, #864). Releases are version-bumped by Commitizen from
  Conventional-Commit history; **PR titles must be Conventional Commits** (CI
  enforces it — see `CONTRIBUTING.md`).

# Validating the browser adapter and the learn site

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

# Docs and release-package checks (run before pushing a docs or packaging change)

In a Claude Code on the web session, `.claude/hooks/session-start.sh` (#618)
already installed these tools at CI's pins; set `AHN_WEB=1` in the environment
to also get emsdk and playwright for the browser checks above. CI's
required checks are the `CI gate` job and `check / org-sync` (org-sync.yml); docs-only PRs skip the simulator jobs (#607)
but not the `pages` job, which builds the whole site, crawls it
(`tools/checks/check-site.py`) and runs the browser smoke with axe on every docs page.

```bash
python3 docs/tools/check-links.py .     # relative links resolve
python3 docs/tools/check-mermaid.py .   # mermaid blocks render on GitHub
python3 docs/tools/check-headers.py .   # SPDX headers on every source file
python3 tools/checks/check-wire-doc.py   # docs/wire-format.md names the codec's kWireVersion (#981)
mkdocs build --strict                   # nav + anchors (needs web/shell: run tools/checks/fetch-site-web.sh first)
tools/release/check-bundle.sh           # release package: no agent files, tools present
# C++ formatting (.clang-format, clang-format-18; lint.yml fails on a diff, #35):
make format
# C++ static checks (.clang-tidy; lint.yml runs them warn-only, #619):
cmake -S core -B build-tidy/core -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
for f in core/src/*.cpp; do clang-tidy -p build-tidy/core --quiet "$f"; done
```
