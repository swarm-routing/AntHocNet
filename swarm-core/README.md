# swarm-adapter-ns3

<!-- org-sync:begin badges -->
| Group | Badges |
| --- | --- |
| Build | [![CI](https://github.com/swarm-routing/swarm-adapter-ns3/actions/workflows/ci.yml/badge.svg)](https://github.com/swarm-routing/swarm-adapter-ns3/actions/workflows/ci.yml) [![Built on: ns3-dev](https://img.shields.io/badge/built%20on-ns3--dev%20image-blue?logo=docker)](https://github.com/orgs/swarm-routing/packages/container/package/ns3-dev) |
| Quality | [![Quality Gate Status](https://sonarcloud.io/api/project_badges/measure?project=swarm-routing_swarm-adapter-ns3&metric=alert_status)](https://sonarcloud.io/summary/new_code?id=swarm-routing_swarm-adapter-ns3) [![Bugs](https://sonarcloud.io/api/project_badges/measure?project=swarm-routing_swarm-adapter-ns3&metric=bugs)](https://sonarcloud.io/summary/new_code?id=swarm-routing_swarm-adapter-ns3) [![Security Rating](https://sonarcloud.io/api/project_badges/measure?project=swarm-routing_swarm-adapter-ns3&metric=security_rating)](https://sonarcloud.io/summary/new_code?id=swarm-routing_swarm-adapter-ns3) [![Maintainability Rating](https://sonarcloud.io/api/project_badges/measure?project=swarm-routing_swarm-adapter-ns3&metric=sqale_rating)](https://sonarcloud.io/summary/new_code?id=swarm-routing_swarm-adapter-ns3) [![codecov](https://codecov.io/gh/swarm-routing/swarm-adapter-ns3/branch/main/graph/badge.svg)](https://codecov.io/gh/swarm-routing/swarm-adapter-ns3/tree/main) [![CodeRabbit Pull Request Reviews](https://img.shields.io/coderabbit/prs/github/swarm-routing/swarm-adapter-ns3?utm_source=oss&utm_medium=github&utm_campaign=swarm-routing%2Fswarm-adapter-ns3&labelColor=171717&color=FF570A&link=https%3A%2F%2Fcoderabbit.ai&label=CodeRabbit+Reviews)](https://coderabbit.ai) |
| Release | [![Release](https://img.shields.io/github/v/release/swarm-routing/swarm-adapter-ns3?sort=semver&cacheSeconds=1800)](https://github.com/swarm-routing/swarm-adapter-ns3/releases) [![DOI](https://zenodo.org/badge/DOI/10.5281/zenodo.24138628.svg)](https://doi.org/10.5281/zenodo.24138628) [![Cite](https://img.shields.io/badge/cite-CITATION.cff-blue)](CITATION.cff) [![Licence: GPL-2.0-only, tools/ MIT OR GPL-2.0-only](https://img.shields.io/badge/licence-GPL--2.0--only%2C%20tools%2F%20MIT%20OR%20GPL--2.0--only-blue)](LICENSING.md) |
| Trust | [![OpenSSF Scorecard](https://api.securityscorecards.dev/projects/github.com/swarm-routing/swarm-adapter-ns3/badge)](https://scorecard.dev/viewer/?uri=github.com/swarm-routing/swarm-adapter-ns3) [![OpenSSF Best Practices](https://www.bestpractices.dev/projects/16816/badge)](https://www.bestpractices.dev/projects/16816) |
| Stack | [![C++23](https://img.shields.io/badge/C%2B%2B-23-blue?logo=cplusplus)](AGENTS.md) [![Runs on ns-3](https://img.shields.io/badge/runs%20on-ns--3-informational)](https://www.nsnam.org) |
| Community & docs | [![Colony Arena: ns3-swarm-core](https://img.shields.io/badge/Colony%20Arena-project%20page-orange)](https://swarm-routing.github.io/projects/ns3-swarm-core/) [![Open RCAPA](https://img.shields.io/github/issues/swarm-routing/swarm-adapter-ns3/rcapa?label=open%20RCAPA&color=blue)](https://github.com/swarm-routing/swarm-adapter-ns3/blob/main/docs/rcapa/README.md) [![Conventional Commits](https://img.shields.io/badge/Conventional%20Commits-1.0.0-yellow)](https://www.conventionalcommits.org) |
<!-- org-sync:end badges -->

The shared ns-3 contrib module `swarm-core` that every swarm-routing protocol
links against: the ns-3 adapter of swarm-core's ports. **Status:** the ns-3
glue is in `model/` and `helper/` (`GPL-2.0-only`): `SwarmRoutingBase`, the
`Ipv4RoutingProtocol` a protocol's adapter derives from;
`DeferredPacketQueue`; the link monitors `WifiLinkMonitor`, `IslLinkMonitor`
and `EnergyMetricHook`, which implement the core's ports;
`SwarmRoutingHelper`, which installs a protocol and its monitors in one call
and collects each node's counters (`CollectStats`); the router observer
(`IRouterObserver`), through which a protocol reports its control packets,
route changes, data decisions and state size as trace sources; and
`SwarmLifecycleHelper`, which scripts node shutdown, crash and restart the
same way for every protocol. The simulator-agnostic C++23 primitives (visited
path, little-endian wire helpers, pheromone table, route decision, link-state
and link-metric seams, the testbench; namespace `swarm::core`,
`MIT OR GPL-2.0-only`) live in
[swarm-routing/swarm-core](https://github.com/swarm-routing/swarm-core) since
#231: this module builds on its release. CI runs every org quality gate.

How to work here, for people and AI agents alike: [AGENTS.md](AGENTS.md) has
the rules and how to build and verify; the release plan is [docs/roadmap.md](docs/roadmap.md); the org conventions are in
[CONTRIBUTING.md](https://github.com/swarm-routing/.github/blob/main/CONTRIBUTING.md).

## swarm-core's primitives: a PUBLIC dependency

The module's `CMakeLists.txt` reads [`swarm-core.lock`](swarm-core.lock),
which names one swarm-routing/swarm-core release (repository, tag, commit,
version, asset and its SHA256 from the release's `SHA256SUMS`), fetches that
release's tarball with CMake `FetchContent` (`URL` + `URL_HASH`, `SOURCE_SUBDIR
core`), and links its target `swarm::core` to `libswarm-core` as a `PUBLIC`
dependency (#231). So the ns-3 configure step needs network access once, and
a module that links `${libswarm-core}` gets, with no edit of its own:

- the primitives' objects;
- their include directory: `#include "swarm-ports.h"`, and
  `#include "testbench/swarm-testbench.h"` in a core test;
- their C++ standard (`cxx_std_23`, the standard of the ns-3 release swarm-core
  supports, [docs/api-policy.md](docs/api-policy.md#c-standard));
- their headers as `ns3/swarm-*.h`, and in `ns3/swarm-core-module.h`, as
  before.

A module that used to find swarm-core's primitives at
`contrib/swarm-core/core/` (an include directory, or a check that the
directory exists) drops that: the directory is gone, and the link above
replaces it. To build against an unreleased swarm-core, pass its `core/` to
the configure step: `./ns3 configure ... -- -DSWARM_CORE_DIR=<swarm-core
checkout>/core`. The lock moves only to a published release, written by
swarm-routing/.github `tools/checks/release_lock.py`; swarm-routing/.github
CONTRIBUTING "Dependencies" has the convention.

`core/` without ns-3 (FetchContent of a release, the installed CMake package
`swarm-core`, the vcpkg port in
[swarm-routing/vcpkg-registry](https://github.com/swarm-routing/vcpkg-registry),
the discrete-event testbench for a protocol core's tests): see
[swarm-routing/swarm-core](https://github.com/swarm-routing/swarm-core). The
releases of this repository up to v1.0.0 carried `core/` themselves, and the
`swarm-core` port built them until swarm-core's own release.

## API stability

**From v1.0.0 the API is frozen** (#83): a consumer rebuilds against any
1.x release unchanged. Additions come in minor releases, fixes in patches; a
breaking change waits for 2.0.0, and anything retired is deprecated first,
for at least two minor releases, with its replacement in the same release.
`ns3::kSwarmCoreApiVersion` is 2 for all of 1.x, so a consumer can
`static_assert` it. Moving `core/` to swarm-routing/swarm-core's release
(#231) is the breaking change of v2.0.0 (proposed).

The module's API is recorded item by item in [docs/api-surface.txt](docs/api-surface.txt),
generated from its headers and the `TypeId`s (swarm-core's primitives are in
swarm-core's own record); the required `api-surface`
check fails a PR that removes or changes an item without a breaking mark.
What counts as the API, which change is breaking and how an API is
deprecated: [docs/api-policy.md](docs/api-policy.md).
`IslLinkMonitor` (with the base API only it drives) ships
as **experimental**, outside that promise until a second protocol adopts
it (api-policy "Experimental", #160).
Which protocol uses which piece: [docs/adopters.md](docs/adopters.md).

<!-- org-sync:begin citing -->
## Releases & citing

Versions follow [SemVer](https://semver.org) ([CHANGELOG.md](CHANGELOG.md)). Each release is listed under [Releases](https://github.com/swarm-routing/swarm-adapter-ns3/releases), built by the org's release workflow and archived on [Zenodo](https://zenodo.org) with its own DOI.

To cite this repository, use the “Cite this repository” button (from [CITATION.cff](CITATION.cff)):

| DOI | resolves to |
|---|---|
| [`10.5281/zenodo.24138628`](https://doi.org/10.5281/zenodo.24138628) | **concept DOI**: always the latest release. This is the badge above, and the right citation when you mean this project. |

Each release also has its own **version DOI**, listed on [its Zenodo record](https://doi.org/10.5281/zenodo.24138628): cite that one when you reproduce a number measured at that release.

Please also cite the work it builds on:

- D. H. Joppi, *swarm-core*, 2026, [doi:10.5281/zenodo.24476619](https://doi.org/10.5281/zenodo.24476619). The ports and wire envelope this repository is the ns-3 adapter of (swarm-routing/swarm-core).
- D. H. Joppi, *AntHocNet for NS-3*, 2026, [doi:10.5281/zenodo.20981979](https://doi.org/10.5281/zenodo.20981979). The source of the ns-3 glue in model/ and of swarm-core's primitives (swarm-routing/swarm-core, which this module builds on): each file names its AntHocNet source and commit.
- G. Di Caro, F. Ducatelle, L. M. Gambardella, *AntHocNet: An Ant-Based Hybrid Routing Algorithm for Mobile Ad Hoc Networks*, Parallel Problem Solving from Nature - PPSN VIII, pp. 461–470, 2004, [doi:10.1007/978-3-540-30217-9_47](https://doi.org/10.1007/978-3-540-30217-9_47). The algorithm swarm-core's primitives were extracted from: the pheromone table, its evaporation and the roulette next-hop selection follow it.
- G. F. Riley, T. R. Henderson, *The ns-3 Network Simulator*, Modeling and Tools for Network Simulation, pp. 15–34, 2010, [doi:10.1007/978-3-642-12331-3_2](https://doi.org/10.1007/978-3-642-12331-3_2). The simulator the glue in model/ and helper/ is written for.

Using it in your work? Contributions are welcome, and citing the DOI helps just as much.
<!-- org-sync:end citing -->

<!-- org-sync:begin develop -->
## Develop

New here? [docs/development.md](docs/development.md) is the developer guide:
the tools to install, how to get the code and its dependencies, how to
build, test, run and lint it, the Docker images, what CI checks and how to
read a red run. How changes are proposed, reviewed and released is the org's
[CONTRIBUTING.md](https://github.com/swarm-routing/.github/blob/main/CONTRIBUTING.md).
Whether a regression here gets an RCAPA (root cause analysis with corrective
and preventive actions), and how: [docs/rcapa/README.md](docs/rcapa/README.md).
<!-- org-sync:end develop -->

## Epics

<!-- epics:begin -->
<!-- Generated by swarm-routing/.github tools/epics/epics.py from the issues labelled `epic`; edit the issues, then re-run it. -->

What is planned and in progress here: the issues labelled [`epic`](https://github.com/swarm-routing/swarm-adapter-ns3/issues?q=label%3Aepic). An epic's open sub-issues carry a `priority:*` and a `model:*` label, so a contributor or an AI agent can pick one up ([how issues work](https://github.com/swarm-routing/.github/blob/main/CONTRIBUTING.md#issues)). The whole org's plan: the [roadmap](https://github.com/swarm-routing/.github/blob/main/ROADMAP.md#epics).

| Epic | Status | Sub-issues closed |
|------|--------|-------------------|
| [#177](https://github.com/swarm-routing/swarm-adapter-ns3/issues/177) developer experience: a newcomer can build, test and contribute from the docs alone | In progress | 1 / 2 |
| [#228](https://github.com/swarm-routing/swarm-adapter-ns3/issues/228) Post-rename stabilization & v2.0.0 migration to swarm-core PUBLIC dependency | In progress | 1 / 4 |
| [#82](https://github.com/swarm-routing/swarm-adapter-ns3/issues/82) swarm-core 2.0.0: the host contract, so a protocol core runs on any host | Planned | 0 / 5 |

<details><summary>Closed: 1 epic</summary>

| Epic | Closed |
|------|--------|
| [#6](https://github.com/swarm-routing/swarm-adapter-ns3/issues/6) swarm-core: the shared contrib module every swarm protocol links (Phase 2) | Done 2026-11-11 |

</details>

<!-- epics:end -->

## Licence

See [LICENSING.md](LICENSING.md).
