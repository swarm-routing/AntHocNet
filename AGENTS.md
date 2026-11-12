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
core/    simulator-agnostic C++ (no simulator headers) + unit tests
ns3/     anthocnet/ (the ns-3 module + satellite harnesses), baselines/<arm>/ (vendored arms), tools/
ns3-tools.lock  the swarm-routing/ns3-tools release (tag, commit, sha256) the routing-bench harness and the ns3_tool readers come from (ns3-tools#29)
web/     (not tracked) the browser adapter + learn site, fetched from swarm-routing.github.io by tools/checks/fetch-site-web.sh (#850)
tools/   shipped scripts: ns3_tools.py (fetches the ns3-tools pin; `run MODULE` runs a reader), bench/ (scenario_check, campaigns, controls), checks/ (CI gates), release/
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
- until #843 closes, the gates, package checks and kits run on `ns3-dev:3.48`
  (ns-3.49's AODV livelocks); the `latest` leg keeps the build and unit tests.

Read [`docs/development.md`](docs/development.md) before:

- changing anything under `ns3/` or a CI workflow: the ns-3 matrix and the
  older releases on demand, the sanitizer pass, the eight gate groups, the
  manual benchmark and release workflows (§ Validating adapter (NS-3) changes);
- changing the browser adapter or the learn site, or a `core/` change that
  must keep native-vs-WASM parity (§ Validating the browser adapter);
- pushing a docs or packaging change: the link, mermaid, header, mkdocs,
  bundle, format and clang-tidy commands (§ Docs and release-package checks);
- reading or producing coverage numbers (§ Test coverage).

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
<!-- org-sync:begin conventions -->
- Issues follow the org taxonomy ([CONTRIBUTING.md](https://github.com/swarm-routing/.github/blob/main/CONTRIBUTING.md#issues), from
  [AntHocNet ADR-0013](https://github.com/swarm-routing/AntHocNet/blob/main/docs/adr/0013-track-bugs-and-findings-as-issues.md)): one type label
  (`bug`/`enhancement`/`chore`/`documentation`/`verification`, plus `epic` for
  umbrellas), area label(s) (`protocol`/`adapter`/`ns3`/`benchmark`/
  `observability`/`packaging`/`learn-site`/`literature`), one `model:*`
  recommendation, one `priority:P1|P2|P3` on non-epic issues, a
  `release:vX.Y.Z` label only when a release in the repository's ladder
  (`docs/roadmap.md`; until it lands, the org ROADMAP's release train)
  gates the issue: that label set is the version's scope,
  and the version is released when its issues are done (CONTRIBUTING
  "Cutting a release"). Labels come from `labels.json` via the `sync-labels`
  workflow, the shared `release:v0.1.0` to `release:v0.4.0` and
  `release:v1.0.0` among them; a version outside that set is added to
  `swarm-routing/.github`'s `labels.json` (and CONTRIBUTING's label table)
  when a ladder needs it, and reaches a repository when its `sync-labels` runs
  (monthly, or dispatched after the change merges),
  or, in a repository that keeps its own release labels (AntHocNet), created
  there with `gh label create release:vX.Y.Z` when its ladder adds the version.
- Issue bodies: **Context** (evidence, numbers, links) · **Scope** ·
  **Acceptance criteria** (checkboxes) · **Depends on** (cross-repo form
  `swarm-routing/<repo>#N`) · **References**. Children of an epic say "Part of
  #N" and are linked as sub-issues. Comments you post end with the Claude Code
  footer.
- Issues migrated from AntHocNet carry `migrated` and start with *Migrated from
  `danieljoppi/AntHocNet#N`*.
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
  the commit on `main`, so it is a lowercase Conventional Commit
  (`feat:`/`fix:`/`docs:`/`chore:`/`ci:`…, optional scope).
- Push with `git push -u origin <branch>`; merge when CI is green.
- **`main` takes no direct push**, in any repo: its ruleset requires a PR and
  the required checks, not an up-to-date branch. Bots push `bot/<name>` branches
  and open PRs; a release is the bump PR from `bot/release`, tagged after it
  merges (CONTRIBUTING "Cutting a release").
- A PR behind `main` may merge as it is: the ruleset does not require an
  up-to-date branch. Update it first
  (`gh api -X PUT "repos/<owner>/<repo>/pulls/<n>/update-branch"`, with
  `<owner>/<repo>` the PR's base repository and `<n>` its number) only when
  `main` changed what it touches (a template, a workflow, a required check),
  then merge after its CI on the updated branch. A
  repository with a merge queue (`merge_queue = true` in its `[settings]`)
  is not updated by hand: enable auto-merge and the queue tests each PR on
  the latest `main` and merges it (CONTRIBUTING "Merge when CI is green").
  Never force-push a branch someone else pushed. A behind `bot/release` PR is the exception:
  dispatch the release `bump` again, so the version and changelog are rebuilt
  on the new `main`.
- **Settings are code.** Repository settings, rulesets, required checks and the
  Actions allowlist change only by a PR to the repo's `[settings]` in
  `.github/org-sync.toml` or to `templates/rulesets/` in `swarm-routing/.github`,
  never in the UI or by a direct API write. A `templates/rulesets/` change
  applies on merge; a repo's `[settings]` change applies when `repo-settings`
  in `swarm-routing/.github` is dispatched with mode `apply` after the merge
  (the daily run only checks). A required check runs on every PR (no `paths:`
  filter; skip work inside a gate job instead) and is named exactly as its
  check run. A new third-party action goes into `defaults.json`
  `selected_actions` (`templates/rulesets/`) before or with the workflow that
  uses it: in the same PR in `swarm-routing/.github`, otherwise merged there
  first, or `repo-settings` refuses the repo.
<!-- org-sync:end git-workflow -->
- AntHocNet specifics: PR titles are enforced by CI (`pr-title.yml`), and since
  the title is the commit on `main` it drives the next Commitizen version bump
  (see `CONTRIBUTING.md`). `main` merges through the merge queue; only the
  ruleset's checks (`CI gate`, `check / org-sync` and the CodeQL scan) gate a merge.

<!-- org-sync:begin skills -->
## Agent skills

The org's procedures ship as the Claude Code plugin **`swarm-routing`**, from
[`swarm-routing/skills`](https://github.com/swarm-routing/skills). This
repository's `.claude/settings.json` enables it, so Claude Code offers a skill
when the task matches its description. Other agents (Codex, Cursor, Copilot, …)
install the same skills with `npx skills add swarm-routing/skills`
([skills.sh](https://skills.sh)). A skill is the procedure; the script does the
work and runs without it (golden rule 9), so an agent without the skill starts
from the script; so does a session that does not read this repository's
settings (some hosted sessions with several repositories do not). If Claude
Code says the plugin is missing (it was called
`org-repo` before 2026-11-09, and a cached marketplace keeps the old list), run
`claude plugin marketplace update swarm-routing` and start a new session.

| Task | Script (any agent) | Skill |
|------|--------------------|-------|
| Bring this repository onto the org templates, or fix their drift | `swarm-routing/.github` `tools/org-sync/org_sync.py` | `org-repo` |
| Write or review a protocol's paper-fidelity sheet (`docs/fidelity.md`) | `swarm-routing/.github` `tools/fidelity/fidelity.py` | `fidelity-sheet` |
| Check a diff against the org golden rules before a PR or a review | `swarm-routing/.github` `tools/protocol-review/check_invariants.py` | `protocol-review` |
| Bring the README, AGENTS/CLAUDE/CONTEXT and docs/ to the org documentation standard | `swarm-routing/.github` `tools/docs-review/check_docs.py` | `docs-review` |
| Find new papers on a topic, or confirm a reference (Crossref, Semantic Scholar, OpenAlex; free sources only) | `swarm-routing/.github` `tools/lit-search/lit_search.py` | `lit-search` |
| Measure what a Claude Code session loads from the agent files (`CLAUDE.md` and its `@` imports, or `AGENTS.md` alone), or where a finished Claude Code session's tokens went, before cutting or adding agent text | `swarm-routing/skills` `tools/token-audit/token_audit.py` | `token-audit` |

A skill only this repository uses lives in its own `.claude/skills/`; a skill
two repositories need moves to the plugin. Skills that file, cite and extract
papers stay in a private library the maintainer keeps; finding
new papers and confirming a reference is public (`lit-search`). A public skill
may point to that library's skills, never carry its content. This section is managed by org-sync: add
a row in `swarm-routing/.github` `templates/agents/skills.md`, not here (an edit
here is undone by the next `org_sync.py apply`).
<!-- org-sync:end skills -->

<!-- org-sync:begin agent-defaults -->
## Working defaults

Condensed from [andrej-karpathy-skills](https://github.com/forrestchang/andrej-karpathy-skills).

- **Assumptions:** state the ones that matter. Ask only when the choice
  belongs to the maintainer: a default, a published number, a cell, the scope.
  Otherwise pick the sensible option, say which, and keep going. Long
  unattended runs, like campaigns, depend on that.
- **Simplicity:** the least code that solves the task. No speculative
  features, configurability or abstractions for one use.
- **Surgical diffs:** every changed line traces to the request. Match the
  surrounding style. Mention unrelated dead code rather than deleting it, and
  remove only the orphans your own change created.
- **Verifiable goals:** a bug gets a reproducing test first; a refactor passes
  before and after. Finish when the check passes, not when the code looks right.

## Session habits

- **Handoffs are always a ticket.** When a session ends with work in flight, or a
  handoff is asked for, open a GitHub issue from the *Handoff* template (label
  `handoff`). Never write a handoff file in the repo. Start a new session from
  the newest open `handoff` issue, and close it when the work it hands off is
  done ([AntHocNet ADR-0013](https://github.com/swarm-routing/AntHocNet/blob/main/docs/adr/0013-track-bugs-and-findings-as-issues.md) item 5).
- **Attach only the repositories the task needs.** Each attached repository's
  agent files are loaded into every request, whether the task touches it or
  not.
- **Start from the record, not from memory:** the newest open `handoff` issue,
  then the issue's acceptance criteria, then `main`. Check a claim against its
  source before it goes into an issue or a PR.
- **Script what repeats.** A check or a lookup run more than a few times
  becomes a script in `tools/` that prints a summary; the model reads the
  verdict, not the raw data (AntHocNet ADR-0014).
- **Measure before cutting or adding agent text:** the `token-audit` skill
  (`swarm-routing/skills` `tools/token-audit/token_audit.py`) prints what a
  session loads and where a finished session's tokens went.

## Response style — caveman (token economy)

Answer in tight, low-filler prose: drop hedging, pleasantries, and preamble; keep
the substance. Fewer output tokens on every reply, same information.

**Carve-out (the record stays full).** Keep byte-for-byte exact — code,
commands, file paths, error text, benchmark numbers. And keep the *durable
record* detailed, never terse: commit messages, PR bodies, ADRs, `docs/`, and
issue / finding threads. Cross-session traceability depends on those being
complete — AntHocNet's AC_BE_NQOS / A2 investigation was only recoverable
because the issue threads were thorough. Caveman the chat, not the record.

## Agent pitfalls

- `gh api … -F body=@file` reads the file; `-f body=@file` posts the literal
  text `@file`.
- When the permission system refuses a command (a merge, say), stop and report
  it. Never retry it another way, and never ask another agent to run it.
- A PR body's `Closes #N` closes the issue on merge; write `Part of #N` while
  acceptance criteria remain open.
- Before saying a tool or check is missing, look at `main` (`git fetch`,
  `origin/main`), not a stale local checkout.
- In an issue, PR or comment, write every issue of another repository in
  full, `swarm-routing/<repo>#N`, also inside a list or a range. A bare `#N`
  links to the repository the text is in: in
  `swarm-routing/AntHocNet#65, #151` the second number points at this
  repository's #151 (swarm-routing/.github#277).
- A sentence in `ROADMAP.md` or an AGENTS.md that calls an issue done is not
  evidence: read the issue's acceptance criteria. On 2026-11-12 nine issues
  described as done had open criteria (swarm-routing/.github#274).
- Change text inside `org-sync:begin/end` markers only through the template
  in `swarm-routing/.github` `templates/`: one PR there, then org-bot's
  `bot/org-sync` refresh PR in each repository. A hand edit per repository
  stays red on `check / org-sync` (swarm-routing/.github#236).
- In a repository with a merge queue, merge by enabling auto-merge (squash);
  a direct merge (`PUT pulls/<n>/merge`) is refused with HTTP 405. Only the
  required checks (`gh api 'repos/{owner}/{repo}/rules/branches/main'`, which
  `gh` fills from the checkout) gate a merge: a red or pending report-only
  check (`codecov/patch`, Sonar) is triaged, not waited on. A red `dco` on an
  outside contributor's commit still holds the PR until that author signs
  off; on the maintainer's or an agent's commits it is expected
  (swarm-routing/.github#212). `dco` is not a required check and runs on
  `pull_request` only, so merge an outside contributor's PR, by auto-merge or
  directly, only after `dco` passed on the head commit that merges, and
  disable auto-merge before anyone pushes to that PR.
- Enable auto-merge only after CodeRabbit has reviewed and its threads are
  resolved: it is not a required check, so auto-merge does not wait for it.
  `python3 -I ../.github/tools/pr-status/pr_status.py wait --review REPO#N`
  (a `swarm-routing/.github` checkout next to the repository) waits for the
  review and goes on with a warning 30 minutes after the PR opened
  (swarm-routing/.github#238).
- Never add a `Signed-off-by` trailer, for the maintainer or anyone else: a
  DCO sign-off is a person's own certification (CONTRIBUTING "Outside
  contributions").
- Releases are cut on request only: dispatch the release `bump` when the
  maintainer asks, once the version's `release:vX.Y.Z` issues are done and
  its exit criteria in the ladder (`docs/roadmap.md`, or the org ROADMAP's
  release train until it lands) are met; never approve a gated publish. A
  failed publish is recovered with "Re-run all jobs", never "Re-run failed
  jobs" (CONTRIBUTING "Cutting a release"). Never type a release's DOI: read
  it from Zenodo.
- Cite a paper in a public issue, PR or doc by its `https://doi.org/<DOI>`
  link, read from its record (`lit-search` confirms it), or, for a work with
  no DOI, by its publisher's or institution's page; never point into or name
  the private paper library there.
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
| Work on the NS-3 adapter | `ns3/anthocnet/model/`, `ns3/anthocnet/helper/`, `ns3/anthocnet/examples/` |
| Work on the browser adapter (the learn site's simulation) | `swarm-routing/swarm-routing.github.io` `site/anthocnet/` ([ADR-0021](docs/adr/0021-the-browser-is-an-adapter.md)); fetched here as `web/`; parity must stay byte-identical |
| Run / read benchmarks | `docs/benchmarks.md` and its index, `ns3/tools/` (run, charts, update) |
| Inspect protocol internals | NS-3 trace sources and swarm-core's observer; core counters via `IRouterObserver` (`core/include/anthocnet/core/ports.h`) |
| Run the benchmark campaign loop (dispatch → fetch → parse) | `benchmark-results` skill (SKILL.md documents the whole procedure) |
| Run a campaign (cells × arms, throttled so PR CI keeps runners) | `ns3-campaign` (swarm-routing/ns3-tools) + a plan in `tools/bench/campaigns/` pinning `repo`, `source_sha`, `image`; the `benchmark-results` skill |
| Run the comparison harness, or add a protocol (arm) to it | [`docs/comparison-harness.md`](docs/comparison-harness.md), [ADR-0027](docs/adr/0027-the-comparison-harness-is-protocol-agnostic.md); the harness is ns3-tools' `routing-bench` at the `ns3-tools.lock` pin |
| Compare benchmark A/B runs (deltas + noise verdict) | `benchmark-results` skill (`python3 tools/ns3_tools.py run bench_parse`: ns3-tools' reader at the pin) |
| Build, test or screenshot the learn site | the `learn-site` skill in `swarm-routing/swarm-routing.github.io` (parity, smoke, README figures) |
| Change the site's top bar, places or the docs/API look | the site repo's `site/anthocnet/shell/`, `docs/places/`, the nav in `mkdocs.yml` |
| Add a world, a mission or a core class | it needs its docs links: `python3 tools/checks/check-site-links.py` says what is missing |
| Summarize / export campaign sweep CSVs | `benchmark-results` skill (`python3 tools/ns3_tools.py run sweep_summary`; `--export-sweeps` feeds the papers repo) |
| Validate a scenario config or result plausibility | `benchmark-results` skill (`tools/bench/scenario_check.py`: `preflight`, then `results`) |
| Pre-push invariant check on a diff | `anthocnet-review` skill (`.claude/skills/anthocnet-review/check_invariants.sh`) |
| Understand why skills are script-first (and add a new analysis loop) | [ADR-0014](docs/adr/0014-agent-skills-are-script-first.md): raw data stays out of LLM context |
| Work on satellite / ISL topologies | [ADR-0015](docs/adr/0015-satellite-substrate-lives-in-the-image.md) (one binary, no separate satellite build), [ADR-0022](docs/adr/0022-satellite-substrate-is-stock-ns3-leo.md) (stock ns-3.48 LEO) |
| **Understand why satellite ≠ MANET** (before assuming a MANET intuition transfers) | [`docs/network-regimes.md`](docs/network-regimes.md), before transferring a MANET intuition |
| Cut a release | the `Release` workflow, `mode: bump`, on request only; then record the version DOI read from Zenodo, never typed (`CONTRIBUTING.md`) |
| Change an org-managed section (`AGENTS.md` conventions/git-workflow/skills, `CLAUDE.md` org section, `SECURITY.md` reporting, `.coderabbit.yaml` head/tail, README badges) | the templates in `swarm-routing/.github` `templates/`, never between the `org-sync:begin/end` markers here |
| Tune defaults | `core/include/anthocnet/core/config.h` |
| **Look up a parameter's default, its provenance, or how to calibrate it** | [`docs/configuration.md`](docs/configuration.md): every `Config` field with its source |
