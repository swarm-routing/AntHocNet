# RCAPA: regressions in a release of swarm-routing/AntHocNet

An **RCAPA** (root cause analysis with corrective and preventive actions) is
the full record of one **regression in a published release** of this
project: something that worked in a released version and broke after a
change. A bug or change that never reached a release needs none (an
ordinary `bug` issue is enough), and an upstream ns-3 bug is an `NS3-NNN` in
swarm-routing/ns3-tools. The org's rule and its decision table are in
[CONTRIBUTING.md, "RCAPA"](https://github.com/swarm-routing/.github/blob/main/CONTRIBUTING.md#rcapa)
([ADR-0003](https://github.com/swarm-routing/.github/blob/main/docs/adr/0003-rcapa-for-ns3-bugs-and-released-regressions.md)).
This page is the project's flow and its index.

## The flow for this project

A **published release** of AntHocNet is a `v*` tag of this repository: its
GitHub release (the core and ns-3 module tarballs, `release.yml`'s `publish` job), archived on Zenodo (concept DOI
[10.5281/zenodo.20981979](https://doi.org/10.5281/zenodo.20981979)), the `anthocnet` ns-3 image on ghcr.io (`release.yml`'s `images` job), and, once its port leaves swarm-routing/vcpkg-registry `templates/`, the `anthocnet-core` vcpkg port. A
regression is something that worked in one of those releases and broke
after a change, whenever it is found, also on `main` before the next tag.
A defect introduced after the last tag and fixed before the next one never
reached a release: it needs no RCAPA, an ordinary `bug` issue is enough.
Whether a commit reached a release is read from
`git tag --contains <commit>`.

| | |
|---|---|
| Published as | `v*` tags: GitHub release + Zenodo DOI, the `anthocnet` release image on ghcr.io, the `anthocnet-core` vcpkg port (planned: still in swarm-routing/vcpkg-registry `templates/`) |
| ID prefix | `AHN-NNN` |
| Ticket | an issue in swarm-routing/AntHocNet, form *Regression in a release (RCAPA)*, title `[rcapa] AHN-NNN <short description>`, labels `rcapa` and `bug` |
| Verified by | the reproducer against the failing tag and the last good one: `make test` (core ctest), the ns-3 suite and gate groups of `ci-ns3.yml`, native-vs-WASM parity, and for a changed number the standard cells (`benchmark-cell.yml`, a campaign plan in `tools/bench/campaigns/`, `tools/bench/harness_control.py` on the committed cells); the determinism check (same seed, byte-identical) where output changed |
| Corrective action | the fix PR with a test that fails without it: a core unit test in `core/tests/` for any core logic change (golden rule 7), an ns-3 test case only for a fix outside core logic (the adapter); a fix that adds a mechanism or moves a default follows golden rule 8: the mechanism ships behind a default-off `Config` switch, the default path stays byte-identical (the determinism check), and a default moves only after an identical-seed A/B in every family; then a release bump |
| Preventive action | the gate, standard cell or determinism case that would have caught it before the tag, added to CI or to the release checks; a published number it touched is re-run and the record in `docs/benchmarks/` corrected |
| Closes when | the fix is merged **and** a `v*` release carries it (tag and version DOI read from Zenodo, recorded in the index) |

```mermaid
sequenceDiagram
    participant U as Reporter or campaign
    participant T as RCAPA ticket
    participant G as CI gates and standard cells
    participant M as main
    participant R as Release and Zenodo
    U->>T: a released version behaves differently
    T->>G: reproduce against the failing and last good release
    T->>M: fix with a test that fails without it
    M->>G: the same check passes on main
    M->>R: release bump carries the fix
    R->>T: tag and DOI recorded, ticket closed
```

## Index

| ID | RCAPA | RCAPA ticket | Fixed in |
|----|-------|--------------|----------|

## Adding one: the ticket first

1. Open the RCAPA ticket (the table above). `NNN` is the next free number in
   the index.
2. Copy [`_template.md`](_template.md) to `rcapa-<short-description>.md`
   (lowercase, hyphens: what breaks, not where it was found), and add its row
   to the index in the same pull request.
3. The ticket stays open until the corrective action is merged **and a
   release carries the fix**; record that release in the index.

## What an RCAPA answers

1. **What broke, in which release, and who did it touch?** The symptom, the
   releases that fail and the last that passes, the users, numbers or
   downstream pins it affects.
2. **How do I see it myself?** Exact commands, the release or commit, an
   image by tag and digest when one is involved, seeds, expected against
   actual.
3. **Why did it happen, and why did our checks let it into a release?** Five
   whys, each citing its evidence: a source line at a named commit, a log, a
   run, a comment. An answer without a source is a hypothesis and says so.
4. **What does it look like?** Mermaid diagrams: the failing call chain, the
   types or components involved.
5. **What did we do about it?** Corrective actions (the fix, its PR, the
   release that carries it) and preventive actions (the test or check that
   catches a recurrence). An action not done yet is an open issue, linked.
6. **When?** A dated timeline and every link.

## Writing one

- Every issue and PR reference is fully qualified: `swarm-routing/<repo>#N`,
  also for this repository's own items; an ns-3 item is a link whose text is
  `nsnam/ns-3-dev#N` to its full gitlab.com URL. Never a bare number with a
  hash. In a mermaid block, which cannot hold a hash, name the item in words.
- Mermaid: no HTML entities inside a block; write template arguments as
  words.
- An RCAPA is never deleted. Later findings update its status and timeline.
