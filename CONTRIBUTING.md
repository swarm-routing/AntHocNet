# Contributing to AntHocNet

Thanks for your interest. This is a research/educational implementation of the
AntHocNet MANET routing protocol with a simulator-agnostic core and a thin NS-3
adapter (the NS-2 adapter was removed in v2.0.0; see `docs/ns2-support.md`).
Please read this before opening a PR.

## Architecture invariant (do not break)

```
core/   simulator-agnostic algorithm. No simulator headers. Pure: returns
        RouteDecisions, performs no I/O. Time & randomness come via ports
        (IClock, IRng). THIS is where protocol logic lives.
ns3/    native Ipv4RoutingProtocol module. Translates header <-> AntMessage,
        executes RouteDecisions, owns timers + the pending-packet queue.
web/    browser adapter: the same core compiled to WebAssembly (learn site).
```

**If a change affects routing decisions, it belongs in `core/`** and must be
covered by a `core/tests/` unit test. Adapters change only to feed the core new
inputs or carry out a new `RouteAction`. See `CONTEXT.md` for the deeper map.

## Building & testing

```bash
# core library + unit tests (fast, no simulator needed)
make test

# NS-3 module (needs an ns-3 tree)
make install-ns3 NS3DIR=/path/to/ns-3-dev
cd /path/to/ns-3-dev && ./ns3 configure --enable-examples --enable-tests && ./ns3 build
./test.py -s anthocnet
```

`make test` must stay green, and any new behaviour needs a test. CI runs the
core tests (incl. ASan/UBSan), the codec fuzzer, the native-vs-WASM parity
gate, and the NS-3 build/test on the ns-3 release ns-3 supports (older
releases, back to ns-3.36, on demand through the CI workflow's manual run).

## Code style

- **C++14.** Match the surrounding code: 4-space indent, `lowerCamelCase`
  members with a trailing `_`, `namespace anthocnet::core`. An `.editorconfig`
  encodes the whitespace rules.
- Comment **non-obvious intent**, not the obvious; don't add narrating comments.
- Prefer extending `Config` over new magic constants.
- **Python tooling** (`ns3/tools/*.py`) is linted with [ruff](https://docs.astral.sh/ruff/);
  run `ruff check ns3/tools` before pushing (CI enforces it).

## Wire format

Any new `AntMessage` field must be added to **all** of: the struct
(`core/include/anthocnet/core/ant_message.h`), the codec
(`core/src/ant_message_codec.cpp`), the ns-3 packet header
(`ns3/anthocnet/model/anthocnet-packet.*`; the browser adapter uses the core codec), and
`core/tests/test_codec.cpp` — and the round-trip test must still pass. Bump the
wire-version byte per [`docs/wire-format.md`](docs/wire-format.md) (ADR-0006).

## Commit / PR titles (Conventional Commits)

PRs are **squash-merged**, so the **PR title becomes the commit on `main`** and
drives release versioning. Titles must follow
[Conventional Commits](https://www.conventionalcommits.org) — CI enforces this:

```
<type>: <lowercase summary>      e.g.  feat: add the DSR baseline
                                       fix: stop the linkfail broadcast storm
```

Types: `feat` `fix` `docs` `style` `refactor` `perf` `test` `build` `ci`
`chore` `revert`. A `feat:` bumps the minor, `fix:` the patch; a
`BREAKING CHANGE:` footer (or a `!` after the type) bumps the minor while
pre-1.0.

Releases are cut with the **Release** workflow, in two steps, because `main`
takes no direct push (#789, #882). Don't bump the version by hand.

1. Run it once with `mode: dryrun`. That run bumps, builds, attests and
   verifies on the runner, and pushes nothing.
2. Run it with `mode: bump`.
   [Commitizen](https://commitizen-tools.github.io/commitizen/) computes the
   next version from the commit history and updates `VERSION` / `.cz.toml` /
   `CITATION.cff` / the CMake version + `CHANGELOG.md`. The workflow then opens
   the PR `chore(release): bump version A to B` from `bot/release`. It tags
   nothing.
3. Merge that PR when `CI gate` is green. The merge starts the workflow again:
   it builds, attests and verifies the merged commit, tags it `v<version>`
   and publishes the install bundle. If `main` moved before the merge,
   dispatch `mode: bump` again first, so the changelog covers it. (The `major_version_zero` gate in `.cz.toml` was flipped to
`false` at v1.0.0, so from 1.0 on a breaking `feat!`/`fix!` bumps the **major**.)

### Post-release: Zenodo DOI (manual — the release's second step)

The Release workflow does **not** set the DOI. With the repo linked to
[Zenodo](https://zenodo.org), Zenodo archives each published GitHub release
**asynchronously** and mints a new DOI a few minutes *after* publish. That value
must then be pasted, by hand, into two places:

1. the **README DOI badge** (top of `README.md`), and
2. the **`doi:`** field in `CITATION.cff`.

So a release is two steps: **(1)** run the Release workflow (automated), then
**(2)** once Zenodo has archived the tag, update the DOI in those two files via a
small `docs:` PR. Keep the two in sync — they have drifted apart before.

**Prefer the concept DOI.** Zenodo issues a per-version DOI *and* a stable
**concept DOI** ("all versions", which auto-resolves to the latest release).
Point the badge and `CITATION.cff` at the **concept DOI** so step 2 becomes a
one-time setup instead of a per-release chore.

**Note for AI agents:** the new DOI does not exist until Zenodo has archived the
tag, a few minutes after publish. Read it from Zenodo's public API — no token
needed. The versions endpoint must be addressed through a **version** record:
the concept ID `20981979` returns HTTP 404 there, as measured 2026-10-02. Any
version record lists every version of the concept, for example v0.1.1's:

```
curl -sL "https://zenodo.org/api/records/20981980/versions/latest"   # the newest version only
curl -s "https://zenodo.org/api/records/20981980/versions?size=25&page=2"   # or page through all
```

(`sort=version` is ignored, as measured 2026-10-26: hits come oldest first, so
with more than 25 versions the newest are on the last page.)

Each hit carries `metadata.version` (the tag) and `doi`. `size` above 25 is
rejected unauthenticated (HTTP 400). `zenodo.org` has been proxy-blocked from
the agent sandbox before (HTTP 403, until 2026-09); if it is, ask the
maintainer for the DOI instead. Either way, open the `docs:` PR only with a
value read from Zenodo or supplied by the maintainer. **Never invent a DOI.**

#### DOI record

Because the badge and `CITATION.cff` intentionally carry the **concept** DOI,
the per-version DOIs are not recorded anywhere else — and those are what you
cite when referencing a *specific* version rather than "the software". This
table is that record.

| Target | DOI | Resolves to |
|---|---|---|
| **Concept ("all versions")** | `10.5281/zenodo.20981979` | always the latest release — this is the value in the README badge and `CITATION.cff` |
| v0.1.1 | `10.5281/zenodo.20981980` | [record 20981980](https://zenodo.org/records/20981980) — the first archived release |
| v0.2.0 | `10.5281/zenodo.21007725` | [record 21007725](https://zenodo.org/records/21007725) |
| v0.3.0 | `10.5281/zenodo.21219572` | [record 21219572](https://zenodo.org/records/21219572) |
| v0.4.0 | `10.5281/zenodo.21435553` | [record 21435553](https://zenodo.org/records/21435553) |
| v0.5.0 | `10.5281/zenodo.21453208` | [record 21453208](https://zenodo.org/records/21453208) |
| v1.0.0 | `10.5281/zenodo.21502372` | [record 21502372](https://zenodo.org/records/21502372) |
| v1.1.0 | `10.5281/zenodo.21583731` | [record 21583731](https://zenodo.org/records/21583731) |
| v1.2.0 | `10.5281/zenodo.21762983` | [record 21762983](https://zenodo.org/records/21762983) — the last release in which NS-2 was actively supported (also cited in [`docs/ns2-support.md`](docs/ns2-support.md)) |
| v1.3.0 | `10.5281/zenodo.21795253` | [record 21795253](https://zenodo.org/records/21795253) — the provenance pin for the published benchmark corpus (also in the README and `CITATION.cff`) |
| v1.4.0 | `10.5281/zenodo.21863774` | [record 21863774](https://zenodo.org/records/21863774) |
| v1.5.0 | `10.5281/zenodo.21956001` | [record 21956001](https://zenodo.org/records/21956001) |
| v1.6.0 | `10.5281/zenodo.22970218` | [record 22970218](https://zenodo.org/records/22970218) |
| v1.7.0 | `10.5281/zenodo.23108019` | [record 23108019](https://zenodo.org/records/23108019) — the FANET + static-mesh family release |
| v1.8.0 | `10.5281/zenodo.23187497` | [record 23187497](https://zenodo.org/records/23187497) — the CBR-source fix-and-restatement roll-up ([#521](https://github.com/swarm-routing/AntHocNet/issues/521)) |
| v1.9.0 | `10.5281/zenodo.23223132` | [record 23223132](https://zenodo.org/records/23223132) — the VANET family release ([#301](https://github.com/swarm-routing/AntHocNet/issues/301)) |
| v2.0.0 | `10.5281/zenodo.23250180` | [record 23250180](https://zenodo.org/records/23250180) — the moving-constellation result ([#297](https://github.com/swarm-routing/AntHocNet/issues/297)), the learn site, and the NS-2 removal ([#307](https://github.com/swarm-routing/AntHocNet/issues/307)) |
| v2.1.0 | `10.5281/zenodo.23265537` | [record 23265537](https://zenodo.org/records/23265537) — the restructure: the whole site is the game ([#626](https://github.com/swarm-routing/AntHocNet/issues/626)), the new `ns3/` layout ([#608](https://github.com/swarm-routing/AntHocNet/issues/608)) and the install path |
| v2.2.0 | `10.5281/zenodo.23423439` | [record 23423439](https://zenodo.org/records/23423439) — the v2.2.0 re-baseline campaign ([#565](https://github.com/swarm-routing/AntHocNet/issues/565): four knobs A/B'd in every family, no default moves), the learn-site campaign chapters 1–2 and the Workshop, the infrastructure track |
| v2.3.0 | `10.5281/zenodo.23456208` | [record 23456208](https://zenodo.org/records/23456208) — the v2.3.0 mechanism campaign (six regime-gated mechanisms in every family; quiet mode moves on by default, [#571](https://github.com/swarm-routing/AntHocNet/issues/571)), the restated v2.3.0 corpus, learn-site chapters 3–5, upgrades and the ns-3 replay, signed release artifacts and the bake recipe |
| v2.4.0 | `10.5281/zenodo.23653720` | [record 23653720](https://zenodo.org/records/23653720) — family-native baselines (ADR-0026: HWMP, GPCR, GPSR-3D, OPSPF), the disaster and SAGIN families, the 50–500-node scale sweep with its known limitation ([#740](https://github.com/swarm-routing/AntHocNet/issues/740)), learn-site chapters 6–8 |
| v2.5.0 | `10.5281/zenodo.23748858` | [record 23748858](https://zenodo.org/records/23748858) — the adaptive-routing comparators ([#563](https://github.com/swarm-routing/AntHocNet/issues/563): ARA passes its anchor and is compared on the grid; Termite and Q-routing ship as infeasibility verdicts), the #740 scale diagnosis (the adapter's retry flood; gated fix in [#760](https://github.com/swarm-routing/AntHocNet/issues/760)), campaigns on `benchmark-cell.yml` only ([#746](https://github.com/swarm-routing/AntHocNet/issues/746)) |
| v2.6.0 | `10.5281/zenodo.23777868` | [record 23777868](https://zenodo.org/records/23777868) — the protocol-agnostic comparison harness ([#764](https://github.com/swarm-routing/AntHocNet/issues/764), [ADR-0027](docs/adr/0027-the-comparison-harness-is-protocol-agnostic.md)): the `routing-bench` module and `routing-compare` program on stock ns-3, self-registering arms, a byte-identical control on every committed MANET-family cell ([#769](https://github.com/swarm-routing/AntHocNet/issues/769)), and the per-packet drop book for re-routing arms ([#761](https://github.com/swarm-routing/AntHocNet/issues/761)) |
| v2.6.1 | `10.5281/zenodo.24060921` | [record 24060921](https://zenodo.org/records/24060921) — core/ and the standalone tooling dual-licensed MIT OR GPL-2.0-only ([#783](https://github.com/swarm-routing/AntHocNet/issues/783), [ADR-0028](docs/adr/0028-core-is-dual-licensed.md)); Zenodo's GitHub integration missed it, so the org's `zenodo` workflow archived it on 2026-11-08 ([swarm-routing/.github#66](https://github.com/swarm-routing/.github/pull/66)) |

**Adding a row is the whole per-release chore.** Append the new version DOI
here; do **not** touch the README badge or `CITATION.cff`, which already point
at the concept DOI and therefore need no update. Pinning either of them to a
version DOI is the regression [#116](https://github.com/swarm-routing/AntHocNet/pull/116)
fixed — the badge had been left on an earlier release's version DOI and had
silently gone stale.

Cite the **concept** DOI for the software in general, and a **version** DOI when
reproducing a specific result — benchmark numbers in `docs/` are tied to a
release, so a paper reproducing them should cite that release's DOI.

## Supply-chain & trust signals

What a stranger can check about this repository without reading its code, and —
where a signal needs a human — exactly what the maintainer has to do. Landed in
[#337](https://github.com/swarm-routing/AntHocNet/issues/337), the last ticket of
the packaging epic [#328](https://github.com/swarm-routing/AntHocNet/issues/328).

### Automated: OpenSSF Scorecard

[`.github/workflows/scorecard.yml`](.github/workflows/scorecard.yml) runs
[OpenSSF Scorecard](https://scorecard.dev) weekly (Mondays 06:23 UTC), on every
merge to `main`, and on manual dispatch. It publishes three ways: the README
badge, GitHub's code-scanning dashboard (SARIF), and the public OpenSSF API.

It is a **report, not a gate** — a low check does not fail a build; it becomes
an issue (ADR-0013). The heaviest deductions (`Pinned-Dependencies`,
`Dangerous-Workflow`, `Token-Permissions`) were already paid down by
[#338](https://github.com/swarm-routing/AntHocNet/pull/338), which SHA-pinned
every action and added Dependabot, so the score reports a posture that already
exists.

**The badge is blank until the first run publishes.** After merging, run the
`Scorecard` workflow once by hand (Actions → Scorecard → *Run workflow*).

Checks this repo will not score full marks on, by design, so nobody chases
them. `Signed-Releases` ramps up one release at a time over a five-release
window (see *Provenance* below). `Pinned-Dependencies` stays at 9: every
action, base image, pip and npm install is pinned by SHA, digest or hash
(#338, #787), except the org's reusable workflows
(`swarm-routing/.github/.github/workflows/*.yml`), which this repo calls at
`@main`. That is the org's contract for them (each caller pins `@main` and
gets fixes without a PR; `swarm-routing/.github` keeps every input backward
compatible), so Scorecard flags each such `uses:` line and it stays. Two are
**accepted findings** (#789): a low score there is a known state, not a
regression to open an issue about.

- **`Code-Review` (0): accepted.** It counts changesets approved by someone
  other than their author. AntHocNet has one maintainer, and GitHub does not
  let an author approve their own PR, so every merge is unreviewed by a second
  human. The AI reviewer (CodeRabbit, see [AI review](#ai-review)) comments
  but never approves, and a bot that auto-approves would game the check rather than meet
  it, so none is added. What changes it: a second maintainer or a regular
  outside reviewer approving PRs.
- **`Contributors` (3): accepted.** It counts the organisations among recent
  contributors; a solo project has one. What changes it: contributors from
  other organisations landing changes.

### Branch protection: a ruleset on `main` (#789)

A repository ruleset protects `main`: a pull request is required (no
approvals, as a solo project), the `CI gate` and `check / org-sync` checks must pass, the branch must
be up to date with `main`, merges are squash-only, and force-pushes and
deletion are blocked. That is what Scorecard's `Branch-Protection` reads; its
higher tiers need required reviews, which a solo project cannot meet.

PRs merge through a **merge queue** (#616): a green PR is added to the queue
(the merge button, or `gh pr merge --squash` / auto-merge), GitHub builds a
merge group on `main`'s tip, and the PR is squash-merged only once that group
passes `CI gate`, `check / org-sync` and its CodeQL scan. The queue's settings (squash, every PR of
a group green, groups of up to 5, 120 minutes for the checks) come from
`merge_queue = true` in [`.github/org-sync.toml`](.github/org-sync.toml),
applied by `swarm-routing/.github`'s `tools/repo-settings`. `ci.yml`,
`codeql.yml` and `org-sync.yml` run `on: merge_group` for it: a workflow that produces a required
check and lacks that trigger leaves the queue waiting until it times out.
`ci.yml`'s `changes` job filters a merge group like a PR, diffing against
`github.event.merge_group.base_sha` (a merge group has no
`pull_request.base.sha`), so a docs-only group still skips the heavy jobs.

Nothing pushes to `main` directly, the workflows included. The ones that
write generated files open a PR from a fixed bot branch through
[`.github/actions/bot-pr`](.github/actions/bot-pr/action.yml), rebuilt on
`main`'s tip and force-pushed on each run, so the PR is up to date and holds
the latest output:

| Workflow | Bot branch | What it proposes |
|---|---|---|
| `charts.yml` | `bot/charts` | charts re-rendered from committed data |
| `core-bench.yml` | `bot/core-bench` | a `results/core-bench/history.csv` row and `core-speed.png` (rows waiting in an open PR are kept) |
| `benchmarks.yml` | `bot/benchmarks` | the regenerated taxonomy table, pages and chart |
| `rescue-artifacts.yml` | `bot/rescue-<run id>` | rescued per-merge CSVs |
| `benchmark-cell.yml` | `bench/<cell>-<run id>` | a benchmark cell (#612) |

The PRs are opened with the first token that works: the org GitHub App
(org secrets `BOT_APP_ID` / `BOT_APP_PRIVATE_KEY`, swarm-routing/.github#21),
this repository's bot app (`AHN_BOT_APP_ID` / `AHN_BOT_APP_KEY`, #705), then
`ORG_BOT_TOKEN` (`benchmark-cell.yml` takes the two apps only), so CI runs on
them and `CI gate` can report. An app whose token cannot be made is a warning
and the next is tried; the job log names the token used, and an app's commits
are made as its bot user. With neither, they fall back to the workflow's
`GITHUB_TOKEN`: GitHub runs no CI on such a PR, so a maintainer closes and
reopens it. If `main` moves while a bot PR waits, the next run rebuilds it, or
use the PR's *Update branch*.

**The release goes through a PR too** (#882): `mode: bump` opens the bump PR
from `bot/release`, and merging it is the release. The tag is pushed after the
merge, onto the merged commit; tags and the `ns3-module` branch are outside
the ruleset, so nothing in the release needs a bypass actor.

### Automated: SonarQube Cloud and Codecov

[`.github/workflows/quality.yml`](.github/workflows/quality.yml) runs on every
push to `main` and every PR that changes more than prose. It measures coverage
of AntHocNet's own C++ (the core and the ns-3 module; not `ns3/baselines/`, and
not the routing-bench harness, which swarm-routing/ns3-tools measures) under the core's unit tests and the module's `test.py`
suites, uploads it to [Codecov](https://codecov.io/gh/swarm-routing/AntHocNet)
([`.codecov.yml`](.codecov.yml): project status within 1% of the base, 80% on
the patch), and runs a [SonarQube Cloud](https://sonarcloud.io/summary/new_code?id=swarm-routing_AntHocNet)
analysis with that coverage ([`sonar-project.properties`](sonar-project.properties)).

Like Scorecard it is a **report, not a gate**: ci.yml's `CI gate` does not wait
for it, and the Codecov and SonarQube statuses stay informational unless the
maintainer adds them to branch protection.

What the maintainer sets up once:

- the `SONAR_TOKEN` secret (an org secret), and **Automatic Analysis off** for
  the project (*Administration* → *Analysis Method*). Until both hold the Sonar
  step skips itself, with a notice or a warning that says which.

Codecov needs no secret: the upload authenticates through GitHub OIDC. A fork
PR gets no OIDC token; the pinned Codecov action then falls back to a tokenless
upload, which Codecov accepts from forks of public repositories. Either way, a
failed upload never fails the job.

### Release integrity: SHA-256 checksums

Every release publishes `anthocnet-<version>.zip.sha256` alongside the bundle,
and the same digest is inlined in the release body — twice on purpose, so a
reader can verify a download against the release *page* without downloading and
trusting a second file. Verification:

```bash
sha256sum -c anthocnet-<version>.zip.sha256   # both files in one directory
```

This is generated by the `Checksum the bundle` step in
[`.github/workflows/release.yml`](.github/workflows/release.yml); there is
nothing for the maintainer to do per release.

### Provenance, signatures and SBOM — **adopted (2026-10, #614)**

Since v2.3.0, every release ships with three things. The verification
commands are in [SECURITY.md](SECURITY.md#verifying-a-release).
- **Build provenance.** Signed SLSA provenance for the install bundle and
  the module tarball, made by `actions/attest`. Its Sigstore bundle is also
  attached as `anthocnet-<version>.provenance.sigstore.json`. From v2.7.0
  the bundle's signed in-toto envelope is also attached as
  `anthocnet-<version>.intoto.jsonl`: the name Scorecard's
  `Signed-Releases` looks for to count a release as having provenance
  (#788).
- **An SBOM.** An SPDX file of the bundle (`anthocnet-<version>.spdx.json`),
  attested against the zip.
- **Image signatures.** cosign keyless signatures on the GHCR images, plus
  provenance for `anthocnet-ns3`.

In 2026-08 this was evaluated and **not adopted**. Here is each objection
from then, and how it is answered now, so the decision is not re-derived:

1. *"The action is mid-migration; the wrapper is about to be replaced."*
   The workflows use `actions/attest` itself (v4.2.2), not the
   `attest-build-provenance` wrapper. One action covers provenance and
   SBOM.
2. *"`actions/attest` documents a third permission, `artifact-metadata:
   write`, that could not be validated; an unknown key would invalidate
   `release.yml`, which pushes the tag before it publishes."*
   - That permission is only for registry *storage records*. Those need an
     organization-owned repository, and this one is user-owned. So the
     workflows never declare it, and the image attestation sets
     `create-storage-record: false`.
   - `release.yml` now pushes the bump commit and the tag **after** the
     bundle is built, attested and verified. A failure in the new steps
     leaves nothing to clean up by hand.
   - `release.yml` also has a `dryrun` mode: bump, build, attest and verify
     on the runner, then stop. Run it once before a real release. It is the
     "exercise it somewhere harmless first" step the old revisit condition
     asked for.
3. *"It would not count for Scorecard's `Signed-Releases`: the attestation
   is not a release asset."* The Sigstore bundle is now attached as a
   `*.sigstore.json` asset, which the check recognises. The score ramps in
   over the next five releases, as predicted.
4. *"The audience is thin: researchers unzip a source tree."* Still true,
   and the checksum stays the first line of defence. Provenance is the one
   signal that binds a download to *this* workflow's identity, and it now
   costs nothing per release.

Each check runs inside the workflow that makes the signature, before
anything is published:
- `gh attestation verify` on both downloads and on the SBOM;
- `cosign verify` on both images.

That is the "checked in CI" acceptance criterion of #614.

### OSS-Fuzz — **not applying (2026-08)**

`core/tests/fuzz_codec.cpp` is already libFuzzer-shaped and CI fuzzes the
untrusted decode path for 60 s per run (`codec-fuzz` in `ci.yml`), so an
OSS-Fuzz integration would be nearly free to write. The blocker is not
technical: OSS-Fuzz's [acceptance
criterion](https://google.github.io/oss-fuzz/getting-started/accepting-new-projects/)
is that a project "must have a significant user base and/or be critical to the
global IT infrastructure", and a research MANET routing implementation that is
installed onto a simulator tree — never onto a production network — plainly
meets neither. Applying would consume reviewer time on a predictable rejection.

The proportionate answer if the 60 s CI budget ever proves too small is
[ClusterFuzzLite](https://google.github.io/clusterfuzzlite/): the same libFuzzer
target, run in this repo's own CI with a persisted corpus and longer batch runs,
no acceptance gate — and it is one of the two integrations Scorecard's `Fuzzing`
check recognises. Reopen the question if the protocol ever ships outside
simulation (the security profile of
[ADR-0020](docs/adr/0020-security-is-a-default-off-profile.md) would be the
trigger), since that is what would change the answer.

### OpenSSF Best Practices badge

AntHocNet is registered at bestpractices.dev as
[project 16656](https://www.bestpractices.dev/en/projects/16656) (#790). Its
badge sits in the README's badge block right after the OpenSSF Scorecard badge,
rendered by org-sync from `bestpractices_project = "16656"` in
`.github/org-sync.toml` (the `bestpractices` badge kind); it shows the
project's live level, *in progress* until the questionnaire is answered.

The remaining step is the maintainer's, since the questionnaire is tied to the
account that owns the project: answer the passing criteria at
<https://www.bestpractices.dev/en/projects/16656/passing>. The repo very likely
already qualifies for *passing* (CI on every PR, a unit-test suite, sanitizer +
fuzz jobs, `SECURITY.md`, a documented release process, an OSI licence, a
public issue tracker). The evidence links are all in-tree:

- `README.md` (description, install);
- `CONTRIBUTING.md` (contribution process, this file);
- `SECURITY.md` (vulnerability reporting);
- `LICENSE` (GPL-2.0-only) and `LICENSING.md` (`core/` and the tooling are
  `MIT OR GPL-2.0-only`, ADR-0028);
- `.github/workflows/ci.yml` (automated build + test on every change);
- `CHANGELOG.md` (release notes), and the release checksums documented above.

## AI review

One AI reviewer comments on pull requests: **CodeRabbit**, a GitHub App on
the free open-source plan, configured by [`.coderabbit.yaml`](.coderabbit.yaml)
(the org's settings plus this repository's golden rules, #838). It reviews
each PR once, when it opens (not drafts or Dependabot bumps), and again only
on request. It only comments: it never approves or requests changes, and it
is not a required check, so only `CI gate` and the CodeQL scan decide whether
a PR can merge. The Claude Code Action review (`claude-review.yml`) was
removed by maintainer decision (#864); CodeRabbit is the only AI reviewer.

How a finding is handled, by humans and agents alike:

1. **A finding is a claim to verify, not an order.** Trace a realistic path
   to the failure before changing anything. On #829 the first round's
   findings were real and fixed in `b09b275`
   ([thread](https://github.com/swarm-routing/AntHocNet/pull/829#discussion_r4427507353));
   the second round's conflicted with #791's equivalence contract, were
   declined with the reason, and CodeRabbit withdrew them
   ([thread](https://github.com/swarm-routing/AntHocNet/pull/829#discussion_r4433674689)).
2. **Answer every thread on the thread itself:** fix it (name the commit) or
   decline it with the reason, so the decision stays next to the code. Then
   resolve the thread. The `main` ruleset requires every review thread
   resolved before a merge, so an unanswered finding holds the PR.
3. **Severity decides whether a finding is worth a push.** A Major, Critical
   or security finding is fixed now, or put to the maintainer; a security
   thread stays open until the maintainer decides. A Minor, nitpick or
   suggestion finding does not trigger a push by itself: answer it, resolve
   it, and let the next code push carry any change. A confirmed bug that is
   not fixed in the PR also gets an issue, or a comment on an existing one,
   linked from the thread before it is resolved
   ([ADR-0013](docs/adr/0013-track-bugs-and-findings-as-issues.md)). On #832 both Major
   findings were fixed in `219bd83`
   ([thread](https://github.com/swarm-routing/AntHocNet/pull/832#discussion_r4433828978)),
   and the Minor "PASS overstates what ran" wording was deferred to a later
   push ([thread](https://github.com/swarm-routing/AntHocNet/pull/832#discussion_r4433879819)).
4. **An unverified finding is still a claim to check.** CodeRabbit marks a
   finding its own verification could not confirm; check it like any other.
   #832's `--filter-module-examples-and-tests` finding was one, and it was
   real ([thread](https://github.com/swarm-routing/AntHocNet/pull/832#discussion_r4433828991)).
5. **Pushes cost review budget.** The plan allows a limited number of
   reviews per hour (CodeRabbit's footer said "up to 10 included reviews per
   hour" on #832). Batch fixes; do not push one commit per finding.
6. **Echoes are not requests.** CodeRabbit edits its summary comment and
   repeats finding text after a push, and an agent's own replies come back
   to it as events. These need no action.
7. **Wait for the review before enabling auto-merge.** `main` merges through
   the merge queue, and auto-merge does not wait for a check that is not
   required: a PR whose CI is quick can merge before CodeRabbit has
   reviewed it, and the review then fails with "The pull request is
   closed" (swarm-routing/.github#238). Enable auto-merge once CodeRabbit's
   review is posted and every thread is answered and resolved.

**Commands.** Post them as a PR comment, addressed to CodeRabbit's handle
(`coderabbitai`, with the at-sign): `review` (review the commits since the
last review; the way to get a second review, as pushes are not reviewed
automatically), `full review` (re-review every file), `pause` and `resume`
(stop and restart automatic reviews), `resolve` (resolve all of its threads,
only once each one is answered), `rate limit` (whether another review is
available now), and `help`. Never use `approve`: an approval from a bot would
game Scorecard's `Code-Review` rather than meet it (see above). The full list
is CodeRabbit's
[review commands](https://docs.coderabbit.ai/reference/review-commands)
page (checked 2026-11-12).

**Learnings.** CodeRabbit stores "learnings" from replies to its findings;
they live in CodeRabbit's app, where the maintainer manages them, and are not
reviewed in a PR. A convention the reviewer should apply belongs in
`.coderabbit.yaml` instead, where a PR changes it (its head and tail sections
come from `swarm-routing/.github`'s templates through org-sync).

## Pull requests

- Branch from `main`; keep PRs focused.
- **Every change is a PR to `main`**, opened as soon as the branch is pushed,
  so all work is tracked. AntHocNet follows the org rule
  ([`swarm-routing/.github` CONTRIBUTING.md](https://github.com/swarm-routing/.github/blob/main/CONTRIBUTING.md#pull-requests))
  as of the maintainer decision of 2026-11-10; this replaces the earlier
  agent instruction "do not open a pull request unless explicitly asked".
- Make sure CI is green. New tunables get NS-3 `TypeId`
  attributes; update `CONTEXT.md` / `docs/porting-notes.md` if behaviour or the
  wire format changed.
- Picking up work? The backlog lives in GitHub issues (each ticket carries
  evidence, a fix sketch, and acceptance criteria).
- **A change to protocol behaviour needs an A/B benchmark verdict**, not a
  plausibility argument — the sweep → A/B → noise-verdict loop is
  [`docs/configuration.md` §5](docs/configuration.md#5-how-to-calibrate-a-parameter).
  Record the verdict and run IDs on the relevant issue.
- **Bugs and findings become GitHub issues** — including partial findings and
  dead ends. The discipline (and the label taxonomy: one type label, area
  label(s), a priority) is
  [ADR-0013](docs/adr/0013-track-bugs-and-findings-as-issues.md); it is what
  keeps investigations recoverable across sessions.

## Reporting issues

Bugs and parser/security concerns: see [`SECURITY.md`](SECURITY.md). For
behaviour questions, open a GitHub issue with the scenario and observed output
(the `--diag` flag on `anthocnet-compare` and the trace sources help).

By contributing you agree your work is licensed under the licence of the
directory it lands in ([LICENSING.md](LICENSING.md), ADR-0028): under
`MIT OR GPL-2.0-only` for anything in `core/`, `tools/`, `ns3/tools/` or
`docs/tools/` (the one exception is `docs/tools/mkdocs_hooks.py`), and under
the repository's [LICENSE](LICENSE) (GPL-2.0-only) everywhere else. A change
to those paths that cannot be offered under both licences belongs elsewhere. You also agree
to abide by our
[Code of Conduct](CODE_OF_CONDUCT.md).
