# AGENTS.md — guide for AI coding agents

Operational guide for LLM agents working in this repository. Read this before
changing anything; [`CONTEXT.md`](CONTEXT.md) holds the background and history
(domain background, current state, glossary, open questions): read the section
the task needs, not the whole file. Design detail lives in
[`docs/architecture.md`](docs/architecture.md), maintenance detail in
[`docs/porting-notes.md`](docs/porting-notes.md), and the rationale behind the
structure in the [ADRs](docs/adr/).

This repository's own skills (`.claude/skills/`, procedures only, golden rule
10): `anthocnet-review` (golden-rule checks on a diff) and `benchmark-results`
(dispatch → fetch → validate → compare). The `learn-site` skill (WASM parity,
browser smoke, README figures) moved with the game to
`swarm-routing/swarm-routing.github.io` (#850). The caveman response style
below is vendored from https://github.com/JuliusBrussee/caveman and adapted for
the org.

## What this repo is

AntHocNet — an ant-colony-optimization MANET routing protocol — implemented
**once** as a simulator-agnostic algorithm core, with a thin **NS-3** adapter
(and a browser adapter for the learn site). The repo does **not** vendor a
simulator; the module installs onto *your* NS-3 tree. The NS-2 adapter was
removed in v2.0.0 ([#307](https://github.com/swarm-routing/AntHocNet/issues/307),
[ADR-0023](docs/adr/0023-one-core-one-simulator-adapter.md)); it lives on at the
v1.2.0–v1.9.0 tags.

```
core/    simulator-agnostic C++ (no simulator headers) + unit tests; links swarm-core's core/ from its vcpkg port
         (vcpkg.json, vcpkg-configuration.json; swarm-routing/.github#489) at the release swarm-core.lock names
         (swarm-routing/ns3-swarm-core#14, #134); installs as the CMake package anthocnet-core
         (ANTHOCNET_CORE_INSTALL, on swarm-core's package; core/tests/consumer-package/; #1037, the vcpkg port)
swarm-core.lock  the swarm-core release (tag, commit, tarball sha256) core/ builds on; written by org-bot, never by hand
ns3/     anthocnet/ (the ns-3 module, on swarm-core's SwarmRoutingBase; it links contrib/swarm-core, which make install-ns3
         installs at the same release, or keeps with a warning when another installer put a different one there;
         ADR-0029) + satellite harnesses, baselines/<arm>/ (vendored arms), tools/
ns3-tools.lock  the swarm-routing/ns3-tools release (tag, commit, sha256) the routing-bench harness and the ns3_tool readers come from (ns3-tools#29)
web/     (not tracked) the browser adapter + learn site, fetched from swarm-routing.github.io by tools/checks/fetch-site-web.sh (#850)
tools/   shipped scripts: ns3_tools.py (fetches the ns3-tools pin; `run MODULE` runs a reader), swarm_core.py (the swarm-core release swarm-core.lock names: `module` for make install-ns3, `path` its core/), bench/ (scenario_check, campaigns, controls), checks/ (CI gates), release/
docs/    architecture.md, porting-notes.md, adr/, benchmarks/ — full map in docs/README.md
results/ raw benchmark data: cells/ (harness runs), campaign/ (classified CSVs)
ci/      canary.sh: what sim-containers' downstream canary runs on a candidate ns3-dev image (install, suites, the release-sensitive ns3-gates steps)
api/     Doxygen config for the API reference (/api/)
paper/   JOSS-style paper source
.claude/ agent skills (procedures only; never shipped in a release)
```

## Build & verify

```bash
make test                                  # build core (CMake) + run ctest — fast, no simulator (swarm-core: the lock's release)
make test VCPKG_ROOT=/path/to/vcpkg        # the same on swarm-core's vcpkg port, as CI's core job
make ns3-doctor   NS3DIR=/path/to/ns-3-dev # check the tree (##DOCTOR## line)
make install-ns3  NS3DIR=/path/to/ns-3-dev # install onto a real NS-3 tree (BASELINES=0: alone)
make clean                                 # remove core/build
make test SWARM_CORE_DIR=../ns3-swarm-core/core   # offline / a local swarm-core instead of the pin
python3 tools/checks/check-swarm-core-dupes.py --gate --swarm-core "$(python3 tools/swarm_core.py path)"  # no primitive in both (CI gate, S9)
```

`make test` is the check CI runs on every push and PR and the one that runs
here without a simulator: **always run it before committing a core change.**
An NS-3 build needs an external simulator tree, so an adapter change is
validated through CI: push a branch, open a PR, read the job results; validate
the core half locally first. Three facts that hold for every change:

- coverage (`core-coverage`, `quality.yml`, Codecov, SonarQube Cloud) is
  report-only, with no threshold (#162): a map of untested paths, not a number
  to chase;
- a published number pins an ns3-dev digest and a campaign-image digest, never
  a rolling tag (#866), and is compared across images only by the cross-image
  rule in `docs/benchmarks/methodology.md`;
- every AODV-family and GPSR-family install (routing-bench's `aodv`, the
  vendored `aomdv` and `gpsr`/`gpcr`/`gpsr3d`, the examples' `aodv` and
  `gpsr3d`) goes through routing-bench's `IcmpErrorGuardHelper`, with its
  streams assigned through the node's `GetObject<…::RoutingProtocol>` (a
  helper's `AssignStreams` that casts `GetRoutingProtocol()` finds nothing
  behind the guard): ns-3.49 livelocks them without it (#843, NS3-001 in the org's
  [ns-3 bug register](https://github.com/swarm-routing/ns3-tools/blob/main/docs/ns3-bugs.md)).
  `tools/checks/check-icmp-guard.py` (CI gate, `make check`) fails on a site
  that bypasses it; a test of the livelock itself carries
  `// icmp-guard: unguarded on purpose (<reason>)`.

Read [`docs/development.md`](docs/development.md) before:

- changing anything under `ns3/` or a CI workflow: the ns-3 matrix and the
  older releases on demand, the sanitizer pass, the eight gate groups, the
  manual benchmark and release workflows (§ Validating adapter (NS-3) changes);
- changing the browser adapter or the learn site, or a `core/` change that
  must keep native-vs-WASM parity (§ Validating the browser adapter);
- pushing a docs or packaging change: the link, mermaid, header, mkdocs,
  bundle, format and clang-tidy commands (§ Docs and release-package checks);
- reading or producing coverage numbers (§ Test coverage).

<!-- org-sync:begin dependencies -->
## How this repository gets swarm-core and ns3-tools

- **swarm-core's `core/`**: through vcpkg in manifest mode (swarm-routing/.github#489),
  so CI builds against the published port. `vcpkg.json` depends on `swarm-core`
  and pins its version and port-version with an `overrides` entry;
  `vcpkg-configuration.json` takes the port from
  [swarm-routing/vcpkg-registry](https://github.com/swarm-routing/vcpkg-registry)
  at a `baseline` commit (the port's version and the SHA512 of its release
  asset) and vcpkg's helper ports from the builtin registry.
  `core/CMakeLists.txt` calls `find_package(swarm-core CONFIG REQUIRED)`
  (imported target `swarm::core`); configure it with the vcpkg toolchain,
  `-DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"`.
  The core CI job sets vcpkg up with swarm-routing/.github's `setup-vcpkg`
  action (the tool at a pinned commit, the binary cache on the org's GitHub
  Packages NuGet feed; the job needs `packages: write`).
- **Two locks**: `swarm-core.lock` names the swarm-routing/swarm-core release
  `core/` builds on (repository, tag, the commit the tag names, version, and
  the SHA256 of `swarm-core-<ver>.tar.gz` from the release's `SHA256SUMS`);
  the `Makefile` names its tag, `core-deps` (the licence boundary is pinned
  by commit, org rule 2) the commit the tag names.
  `swarm-adapter-ns3.lock` (prefix `ADAPTER_`) names the swarm-routing/swarm-adapter-ns3
  release whose ns-3 module (`swarm-core`, `SwarmRoutingBase`) the ns-3 build
  installs as `contrib/swarm-core`: the adapter release whose own
  `swarm-core.lock` names the same swarm-core release, so `core/` and the
  ns-3 module build on one swarm-core.
- **One version**: the `swarm-core / pin` check (swarm-routing/.github
  `tools/checks/check-swarm-core-pin.py`, vcpkg mode) verifies
  `swarm-core.lock` against the release, that the override pins the lock's
  version, and that the registry baseline is a commit on the registry's
  `main` holding exactly that version and port-version; it reports a newer
  swarm-core release (swarm-routing/.github#586: a warning on a PR or a merge
  group, red on `main` only while org-bot's bump PR is open).
- **Bumping**: never by hand. org-bot's `swarm-core-pin` task opens one PR
  (`org-bot/swarm-core-pin`) that writes `swarm-core.lock` from the new
  release, moves the tag and its commit on every line above, the override,
  and the registry baseline to the registry's `main`, once that `main` holds
  the release (its port PR merged; until then it waits). Its `release-lock`
  task opens the PR (`org-bot/release-lock`) that moves
  `swarm-adapter-ns3.lock` to the adapter release built on
  `swarm-core.lock`'s tag, not the newest one, and holds it while no adapter
  release ships that tag. Between releases nothing moves. With
  swarm-routing/.github checked out next to this repository,
  `python3 -I ../.github/tools/checks/release_lock.py verify swarm-adapter-ns3.lock`
  checks the adapter's lock against its release.
- **The ns-3 build**: this repository's own ns-3 CI (it does not call
  `reusable-ns3-ci.yml`) builds from the release locks alone, with no
  canary leg on `@main` (maintainer decision on swarm-routing/.github#411). A
  dependency's next release reaches it through org-bot's lock bump PR.
- **Working against an unreleased swarm-core**: `-DSWARM_CORE_DIR=<checkout>/core`
  builds `core/` on a local checkout instead of the port (development only).
<!-- org-sync:end dependencies -->

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

- C++23 for `core/`: it follows the C++ standard of the ns-3 release we support
  (ns-3's `CMAKE_CXX_STANDARD_MINIMUM`, 23 from ns-3.46; maintainer decision
  2026-11-14, swarm-routing/ns3-swarm-core#14). The ns-3 module needs ns-3.46+.
- Namespace `anthocnet::core` for shared code.
<!-- org-sync:begin conventions -->
- Issues follow the org taxonomy ([CONTRIBUTING.md](https://github.com/swarm-routing/.github/blob/main/CONTRIBUTING.md#issues), from
  [AntHocNet ADR-0013](https://github.com/swarm-routing/AntHocNet/blob/main/docs/adr/0013-track-bugs-and-findings-as-issues.md)): one type label
  (`bug`/`enhancement`/`chore`/`documentation`/`verification`, plus `epic` for
  umbrellas), area label(s) (`protocol`/`adapter`/`ns3`/`benchmark`/
  `observability`/`packaging`/`ci`/`learn-site`/`literature`), one `model:*`
  suggestion (a model on the Claude Pro plan), one `priority:P1|P2|P3` on
  non-epic issues (none on an epic), and a `release:vX.Y.Z` label only when
  a release in the repository's ladder (`docs/roadmap.md`, else the org
  ROADMAP's release train) gates the issue: that label set is the version's
  scope, released when its issues are done (CONTRIBUTING "Cutting a
  release"). Labels come from `.github`'s `labels.json`, applied to every
  repository as soon as it changes (never deleting one); a release version
  it lacks is added there (and to CONTRIBUTING's label table), or, in a
  repository that keeps its own release labels (AntHocNet), created with
  `gh label create release:vX.Y.Z`.
- Issue bodies: **Context** (evidence, numbers, links) · **Scope** ·
  **Acceptance criteria** (checkboxes) · **Depends on** (cross-repo form
  `swarm-routing/<repo>#N`) · **References**. Children of an epic say "Part of
  #N" and are linked as sub-issues. Comments you post end with the Claude Code
  footer.
- Issues migrated from AntHocNet carry `migrated` and start with *Migrated from
  `danieljoppi/AntHocNet#N`*.
- Epics are listed in a public repository's README `## Epics` and the org
  [ROADMAP](https://github.com/swarm-routing/.github/blob/main/ROADMAP.md#epics),
  generated from the `epic` issues by `.github`'s `tools/epics/epics.py`. An
  open epic is Planned (no sub-issue closed) or In progress (some closed); it
  is Done or Dropped only once the epic itself is closed (completed or not
  planned). The lists follow the issues with no hand step: an issue change
  that can move them makes the org-managed `epics-changed.yml` run org-bot's
  `epics` task, which opens the refresh PRs. Never edit a generated section
  by hand; private repositories keep none.
<!-- org-sync:end conventions -->
- A new comparator arm (a protocol we did not design) must clear three gates
  before its numbers count: a fidelity sheet from its paper, the per-PR
  delivery smoke (#439), and an anchor reproducing its own paper's trend. If it
  cannot, it ships as a written infeasibility verdict (see #563).
  The `.github/ISSUE_TEMPLATE/` forms preset the type label for issues filed
  from the GitHub UI.

## Git workflow

<!-- org-sync:begin git-workflow -->
- Branch from `main` (`claude/<short-topic>`), one focused change per PR.
- **Always open a PR to `main`** once the branch is pushed — every change is
  tracked through one.
- **PRs are squash-merged** (the only merge method enabled). The PR title is
  the commit on `main`: a lowercase Conventional Commit, `type(scope)!:
  subject`, type one of `feat fix docs style refactor perf test build ci
  chore revert`, subject starting lowercase.
- Push with `git push -u origin <branch>`; merge when CI is green.
- **`main` takes no direct push**, in any repo: its ruleset requires a PR and
  the required checks, not an up-to-date branch. Bots push `bot/<name>`
  branches and open PRs; a release is the bump PR from `bot/release`, tagged
  after it merges (CONTRIBUTING "Cutting a release").
- A PR behind `main` may merge as it is. Update it
  (`gh api -X PUT "repos/<owner>/<repo>/pulls/<n>/update-branch"`, the PR's
  base repository) only when `main` changed what it touches (a template, a
  workflow, a required check), then merge after its CI. Never update a
  merge-queue PR by hand: enable auto-merge and the queue tests it on the
  latest `main` (CONTRIBUTING "Merge when CI is green"). Never force-push a
  branch someone else pushed. A behind `bot/release` PR: dispatch the release
  `bump` again.
- **Settings are code.** Settings, rulesets, required checks and the Actions
  allowlist change only by a PR to the repo's `[settings]` in
  `.github/org-sync.toml` or to `templates/rulesets/` in
  `swarm-routing/.github`, never in the UI or by a direct API write. A
  rulesets change applies on merge, a `[settings]` change on its push through
  the org-managed `settings-apply.yml` (swarm-routing/.github#569). A required
  check runs on every PR (no `paths:` filter; skip work inside a gate job) and
  is named exactly as its check run. A new third-party action goes into
  `defaults.json` `selected_actions` (`templates/rulesets/`) in swarm-routing/.github
  before or with the workflow that uses it, or `repo-settings` refuses the repo.
<!-- org-sync:end git-workflow -->
- AntHocNet specifics: PR titles are enforced by CI (`pr-title.yml`), and since
  the title is the commit on `main` it drives the next Commitizen version bump
  (see `CONTRIBUTING.md`). `main` merges through the merge queue; only the
  ruleset's checks (`CI gate`, `check / org-sync` and the CodeQL scan) gate a merge.

<!-- org-sync:begin skills -->
## Agent skills

The org's procedures ship as the Claude Code plugin **`swarm-routing`**
([`swarm-routing/skills`](https://github.com/swarm-routing/skills)), which this
repository's `.claude/settings.json` enables; other agents install the same
skills with `npx skills add swarm-routing/skills` ([skills.sh](https://skills.sh)).
A skill is the procedure; its script does the work without it (golden rule 9),
so an agent without the skill, or a session that does not read this
repository's settings, starts from the script. Plugin missing (it was
`org-repo` before 2026-11-09)? Run `claude plugin marketplace update
swarm-routing` and start a new session.

| Task | Script (any agent) | Skill |
|------|--------------------|-------|
| Bring this repository onto the org templates, or fix their drift | `swarm-routing/.github` `tools/org-sync/org_sync.py` | `org-repo` |
| Write or review a protocol's paper-fidelity sheet (`docs/fidelity.md`) | `swarm-routing/.github` `tools/fidelity/fidelity.py` | `fidelity-sheet` |
| Check a diff against the org golden rules before a PR or a review | `swarm-routing/.github` `tools/protocol-review/check_invariants.py` | `protocol-review` |
| Bring the README, AGENTS/CLAUDE/CONTEXT and docs/ to the org standard | `swarm-routing/.github` `tools/docs-review/check_docs.py` | `docs-review` |
| Find new papers on a topic, or confirm a reference (free sources only) | `swarm-routing/.github` `tools/lit-search/lit_search.py` | `lit-search` |
| Give a repository its docs/references.bib, or review it (every paper and org release it relies on, confirmed from its record) | `swarm-routing/.github` `tools/fidelity/fidelity.py` (bib), `tools/zenodo/zenodo.py` | `references` |
| Take open pull requests from review to merge (CI, review threads, merge) | `swarm-routing/.github` `tools/pr-status/pr_status.py` | `pr-shepherd` |
| Measure what a session loads from the agent files, or where a finished session's tokens went | `swarm-routing/skills` `tools/token-audit/token_audit.py` | `token-audit` |
| Register a repository for the OpenSSF Best Practices badge, or check its answers | `swarm-routing/.github` `cmd/bestpractices` (Go) | `bestpractices` |
| Check or bump the release a repository builds on (`swarm-core.lock`, `ns3-tools.lock`) | `swarm-routing/.github` `tools/checks/release_lock.py`, `tools/checks/check-swarm-core-pin.py` | `dependency-bump` |
| Cut a release on the maintainer's request, and its aftercare | `swarm-routing/.github` `tools/release/release_check.py`, `tools/zenodo/zenodo.py` | `release` |
| Record a reviewed SonarQube Cloud false positive or accepted finding | `swarm-routing/.github` `tools/sonar-triage/sonar_triage.py` | `sonar-triage` |

A skill only this repository uses lives in its own `.claude/skills/`; one two
repositories need moves to the plugin. Skills that file, cite and extract
papers stay in the maintainer's private library; a public skill may point to
them, never carry their content.
<!-- org-sync:end skills -->

<!-- org-sync:begin agent-defaults -->
## Working defaults

Condensed from [andrej-karpathy-skills](https://github.com/forrestchang/andrej-karpathy-skills).

- **Assumptions:** state the ones that matter. Ask only when the choice
  belongs to the maintainer (a default, a published number, a cell, the
  scope), with your recommendation and its reason, never the options alone.
  Otherwise pick the sensible option, say which, and keep going: long
  unattended runs, like campaigns, depend on that.
- **Simplicity:** the least code that solves the task. No speculative
  features, configurability or abstractions for one use.
- **Surgical diffs:** every changed line traces to the request. Match the
  surrounding style. Mention unrelated dead code rather than deleting it, and
  remove only the orphans your own change created. Update the doc or ADR of a
  documented decision you change.
- **Verifiable goals:** a bug gets a reproducing test first; a refactor passes
  before and after. Finish when the check passes, not when the code looks right.

## Session habits

- **Handoffs are always a ticket**: work in flight at a session's end, or a
  handoff asked for, is an issue from the *Handoff* template (label
  `handoff`), never a file in the repo; a session continuing a handoff
  updates that issue. Continued work starts from the newest open `handoff`
  issue for it, a new task from the request; close the issue, and any older
  one it superseded, when its work is done ([AntHocNet ADR-0013](https://github.com/swarm-routing/AntHocNet/blob/main/docs/adr/0013-track-bugs-and-findings-as-issues.md) item 5).
- **Attach only the repositories the task needs**: every request loads each
  attached repository's agent files.
- **Start from the record, not from memory:** the matching `handoff` issue,
  then the issue's acceptance criteria, then `main`. Check a claim against its
  source before it goes into an issue or a PR.
- **Script what repeats**: a check or lookup run more than a few times
  becomes a script in `tools/` that prints a summary; read the verdict, not
  the raw data (AntHocNet ADR-0014).
- **Automate what can be automated** (org CONTRIBUTING invariant 8): a step
  stays manual only when no API or permission exists for it, or the
  maintainer reserves the decision; list it in swarm-routing/.github#568.
- **Measure before cutting or adding agent text** with the `token-audit`
  skill (`swarm-routing/skills` `tools/token-audit/token_audit.py`). MCP
  servers, plugins, token habits and the long form of each pitfall below:
  [`docs/agents.md`](https://github.com/swarm-routing/.github/blob/main/docs/agents.md)
  in swarm-routing/.github (ADR-0005).

## Response style — caveman (token economy)

Answer in tight, low-filler prose: no hedging, pleasantries or preamble; same
information, fewer output tokens. **The record stays full:** code, commands,
paths, error text and benchmark numbers byte-for-byte, and commit messages,
PR bodies, ADRs, `docs/` and issue threads detailed (AntHocNet's AC_BE_NQOS /
A2 investigation was recoverable only from thorough threads). Caveman the
chat, not the record.

## Agent pitfalls

- **Before every push, run `make check`** (none: the "Build & verify"
  commands; `make pre-push TITLE='…'` where it exists) **and check the PR
  title** (Git workflow): a skipped step costs a CI round
  (swarm-routing/.github#586).
- `gh api … -F body=@file` reads the file; `-f body=@file` posts the literal
  text `@file`.
- When the permission system refuses a command (a merge, say), stop and report
  it. Never retry it another way, and never ask another agent to run it.
- Edit files with the editor tools, never a shell heredoc or `sed -i`, and
  touch one repository per command (swarm-routing/.github#578).
- Stage files by name, never `git add -A`; keep build output in `.gitignore`
  (swarm-routing/.github#644).
- A Go test reads only files under `cmd/` or `internal/` (`testdata/`)
  (swarm-routing/.github#587).
- Clean up only your own directory, never a shared one
  (swarm-routing/.github#644).
- Search an output the harness saved to a file (`grep`, `head`, `sed -n`),
  never read it whole; wait on a PR with `pr_status.py wait` or `wait
  --merged`, not by subscribing to its activity (swarm-routing/.github#794).
- A PR body's `Closes #N` closes the issue on merge; write `Part of #N` while
  acceptance criteria remain open.
- Before saying a tool or check is missing, look at `main` (`git fetch`,
  `origin/main`), not a stale local checkout.
- Write another repository's issue in full, `swarm-routing/<repo>#N`, also in
  a list or range: a bare `#N` links to the repository the text is in
  (swarm-routing/.github#277). An ns-3 item is its GitLab URL (or link text
  `nsnam/ns-3-dev#N`); text bound for ns-3's GitLab names ours by full GitHub
  URL (CONTRIBUTING "ns-3 bugs (RCAPA)").
- An issue called done in `ROADMAP.md` or an AGENTS.md is not evidence: read
  its acceptance criteria (swarm-routing/.github#274).
- Change text inside `org-sync:begin/end` markers only through
  `swarm-routing/.github` `templates/`, then org-bot's refresh PRs; never add
  such commits to an unrelated PR (swarm-routing/.github#236).
- With a merge queue, merge by enabling auto-merge (squash); a direct merge is
  refused (HTTP 405). Only the required checks (`gh api
  'repos/{owner}/{repo}/rules/branches/main'`) gate a merge; a report-only
  check (`codecov/patch`, Sonar) is triaged, not waited on. Merge an outside
  contributor's PR only after `dco` passed on its head, and never push to it
  (swarm-routing/.github#288; CONTRIBUTING "Merge when CI is green").
- `review / coderabbit` is required (swarm-routing/.github#294), so
  auto-merge may be enabled at once; still fix or answer every CodeRabbit
  thread before resolving it, and call a PR thread-free only after
  `pulls/N/comments` and the reviews show none. Where the gate is not
  required yet, wait with `pr_status.py gate REPO#N`
  (swarm-routing/.github#238).
- Never add a `Signed-off-by` trailer, for the maintainer or anyone else
  (CONTRIBUTING "Outside contributions").
- Never delete a stale branch or ask the maintainer to: org-bot's daily
  `branch-sweep` does (swarm-routing/.github#595).
- Releases are cut on request only, once the version's `release:vX.Y.Z`
  issues and its ladder exit criteria are done; never approve a gated publish;
  recover a failed publish with "Re-run all jobs"; read a DOI from Zenodo,
  never type it; never dispatch the site's `release-sync.yml` by hand
  (swarm-routing/.github#571; CONTRIBUTING "Cutting a release").
- Cite a paper by its `https://doi.org/<DOI>` link, read from its record
  (`lit-search`), or a DOI-less work by its publisher's page; never point into
  or name the private paper library.
- Never dismiss a security alert or handle a leaked secret: both are the
  maintainer's; review `security-autofix.yml`'s PRs like any other
  (swarm-routing/.github#629; CONTRIBUTING "Security alerts").
- An ns-3 bug, or its investigation, gets an RCAPA ticket in
  swarm-routing/sim-tools first (`NS3-NNN`), then its doc and register row;
  every workaround links the ID, and upstream reports go out only through
  swarm-routing/ns3-upstream-report. A regression in a published library
  version gets one in that library's repository (`<PREFIX>-NNN`), open until a
  release carries the fix; the pins and `git tag --contains` decide, never
  memory (CONTRIBUTING "RCAPA", "ns-3 bugs (RCAPA)").
<!-- org-sync:end agent-defaults -->

## Where to look

One line per task. Each row has a full entry in [`docs/where-to-look.md`](docs/where-to-look.md)
(the files, commands, decisions and issues behind it): read it before working
on what the row names.

| You want to… | Go to |
|--------------|-------|
| Understand the design & decision flow | `docs/architecture.md` |
| Check a claim/parameter against the papers | `docs/publications/` (digests of [1] + thesis status) |
| See what v1.0 reproduces / deviates from the paper | `docs/fidelity.md` |
| Understand a structural decision / its "why" | `docs/adr/` |
| **Pick up open work** | GitHub issues — start with the highest open `priority:P*` label (query live; see `CONTEXT.md` §10) |
| **Know which release a piece of work serves, or why something is *not* planned** | [`docs/roadmap.md`](docs/roadmap.md): the release ladder, exit criteria, non-goals; issues carry `release:` labels |
| Know what research a plan item rests on (families, mechanisms, comparators, secure protocols) | [`docs/research-landscape-2026.md`](docs/research-landscape-2026.md) (abstract-level: read the paper before citing a number) |
| Work on the learn-site game / campaign | [`docs/learn-campaign.md`](docs/learn-campaign.md), the `learn-site` skill in `swarm-routing/swarm-routing.github.io` |
| Change what a release ships, or the install path | `tools/release/` (assemble + gate), `release.yml`, `ns3/Makefile`; infrastructure epic #328 (was #603) |
| Understand the ant types (what each one is for, what it writes, which switch gates it) | [`docs/ant-types.md`](docs/ant-types.md) |
| See the whole stack, or which mechanism is live/inert in a given regime | [`docs/software-layers.md`](docs/software-layers.md) |
| **Add support for a new network family** (FANET, VANET, …) | [ADR-0019](docs/adr/0019-network-families-change-the-evaluation-not-the-protocol.md): a family is a scenario concern, never family-specific protocol defaults |
| Work on security / trust mechanisms | [ADR-0020](docs/adr/0020-security-is-a-default-off-profile.md): behind attributes, default off, default path byte-identical, no fork |
| Handle an AI review finding (CodeRabbit: verify, fix or decline with the reason on the thread, resolve it; auto-merge only after its review) | [`CONTRIBUTING.md` "AI review"](CONTRIBUTING.md#ai-review), `.coderabbit.yaml` |
| Record a bug / finding, or hand off across sessions | [ADR-0013](docs/adr/0013-track-bugs-and-findings-as-issues.md): always an issue; a handoff is an issue labelled `handoff`, never a file |
| Maintain the wire format | `docs/wire-format.md`, `docs/porting-notes.md` |
| Change the algorithm | `core/src/`, `core/include/anthocnet/core/` |
| Change routing policy / decision flow | `core/src/ant_router_logic.cpp` |
| Change pheromone math | `core/src/pheromone_engine.cpp`, `pheromone_table.cpp` |
| Change the wire format | `docs/wire-format.md` → `core/include/.../ant_message_codec.h` (+ the ns-3 header; bump `kWireVersion`) |
| Find the removed NS-2 adapter | [`docs/ns2-support.md`](docs/ns2-support.md) — the v1.2.0 / v1.9.0 tags and images |
| Work on the NS-3 adapter | `ns3/anthocnet/model/`, `ns3/anthocnet/helper/`, `ns3/anthocnet/examples/`; it derives from swarm-core's `SwarmRoutingBase` (sockets, ports, pending queue, MAC failures, peer map, observer): [ADR-0029](docs/adr/0029-anthocnet-builds-on-swarm-core.md), #1019 |
| Work on the browser adapter (the learn site's simulation) | `swarm-routing/swarm-routing.github.io` `site/anthocnet/` ([ADR-0021](docs/adr/0021-the-browser-is-an-adapter.md)); fetched here as `web/`; parity must stay byte-identical |
| Run / read benchmarks | `docs/benchmarks.md` and its index, `ns3/tools/` (run, charts, update) |
| Inspect protocol internals | NS-3 trace sources and swarm-core's observer; core counters via `IRouterObserver` (`core/include/anthocnet/core/ports.h`) |
| Run the benchmark campaign loop (dispatch → fetch → parse) | `benchmark-results` skill (SKILL.md documents the whole procedure) |
| Run a campaign (cells × arms, throttled so PR CI keeps runners) | `ns3-campaign` (swarm-routing/ns3-tools) + a plan in `tools/bench/campaigns/` pinning `repo`, `source_sha`, `image`; the `benchmark-results` skill |
| Run the comparison harness, or add a protocol (arm) to it | [`docs/comparison-harness.md`](docs/comparison-harness.md), [ADR-0027](docs/adr/0027-the-comparison-harness-is-protocol-agnostic.md); the harness is ns3-tools' `routing-bench` at the `ns3-tools.lock` pin |
| Find or suspect an upstream ns-3 bug (a stock arm hangs, crashes, or moves with unrelated code) | the org's register in swarm-routing/ns3-tools: [`docs/ns3-bugs.md`](https://github.com/swarm-routing/ns3-tools/blob/main/docs/ns3-bugs.md) (index, workarounds, upstream status) and [`docs/ns3-rcapa/`](https://github.com/swarm-routing/ns3-tools/tree/main/docs/ns3-rcapa) (one root-cause record per bug); a new one starts with its `[rcapa]` ticket there |
| Compare benchmark A/B runs (deltas + noise verdict) | `benchmark-results` skill (`python3 tools/ns3_tools.py run bench_parse`: ns3-tools' reader at the pin) |
| Build, test or screenshot the learn site | the `learn-site` skill in `swarm-routing/swarm-routing.github.io` (parity, smoke, README figures) |
| Change the site's top bar, places or the docs/API look | the site repo's `site/anthocnet/shell/`, `docs/places/`, the nav in `mkdocs.yml` |
| Add a world, a mission or a core class | it needs its docs links: `python3 tools/checks/check-site-links.py` says what is missing (fetch `web/` first: `tools/checks/fetch-site-web.sh`) |
| Summarize / export campaign sweep CSVs | `benchmark-results` skill (`python3 tools/ns3_tools.py run sweep_summary`; `--export-sweeps` feeds the paper drafts' plot data) |
| Validate a scenario config or result plausibility | `benchmark-results` skill (`tools/bench/scenario_check.py`: `preflight`, then `results`) |
| Re-choose a gate's seeds after an ns-3 release moved its field | `tools/bench/reanchor.py` (oracle survey + #1222's rule; never a floor); `docs/benchmarks/methodology.md` "Re-anchoring a gate's seeds" |
| Pre-push invariant check on a diff | `anthocnet-review` skill (`.claude/skills/anthocnet-review/check_invariants.sh`) |
| Understand why skills are script-first (and add a new analysis loop) | [ADR-0014](docs/adr/0014-agent-skills-are-script-first.md): raw data stays out of LLM context |
| Work on satellite / ISL topologies | [ADR-0015](docs/adr/0015-satellite-substrate-lives-in-the-image.md) (one binary, no separate satellite build), [ADR-0022](docs/adr/0022-satellite-substrate-is-stock-ns3-leo.md) (stock ns-3.48 LEO) |
| **Understand why satellite ≠ MANET** (before assuming a MANET intuition transfers) | [`docs/network-regimes.md`](docs/network-regimes.md), before transferring a MANET intuition |
| Cut a release | the `Release` workflow, `mode: bump`, on request only; then record the version DOI read from Zenodo, never typed (`CONTRIBUTING.md`) |
| Change an org-managed section (`AGENTS.md` conventions/git-workflow/skills, `CLAUDE.md` org section, `SECURITY.md` reporting, `.coderabbit.yaml` head/tail, README badges) | the templates in `swarm-routing/.github` `templates/`, never between the `org-sync:begin/end` markers here |
| Tune defaults | `core/include/anthocnet/core/config.h` |
| **Look up a parameter's default, its provenance, or how to calibrate it** | [`docs/configuration.md`](docs/configuration.md): every `Config` field with its source |
