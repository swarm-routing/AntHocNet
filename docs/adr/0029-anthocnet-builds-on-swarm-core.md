# ADR-0029: AntHocNet builds on swarm-core

- **Status:** Accepted. Maintainer decisions:
  - 2026-11-12, on [swarm-routing/ns3-swarm-core#14](https://github.com/swarm-routing/ns3-swarm-core/issues/14): AntHocNet consumes swarm-core.
  - 2026-11-14: C++23, and "try to be ready to go to swarm-core 1.0.0".
  - 2026-11-15: Q2 to Q4 below; `FuzzyMetric` and `IslLinkMonitor` are experimental; the 1.0.0 sequencing.

  Implemented in slices S0 to S10 of [#1019](https://github.com/swarm-routing/AntHocNet/issues/1019). Supersedes the observer bridge of [#917](https://github.com/swarm-routing/AntHocNet/issues/917).
- **Date:** 2026-11-15
- **Builds on:**
  - [ADR-0003](0003-pure-core-returns-route-decisions.md): the core returns decisions;
  - [ADR-0021](0021-the-browser-is-an-adapter.md): the browser adapter and its parity gate;
  - [ADR-0023](0023-one-core-one-simulator-adapter.md): one core, one simulator adapter;
  - [ADR-0028](0028-core-is-dual-licensed.md): the dual-licensed core.

## Context

[swarm-routing/ns3-swarm-core](https://github.com/swarm-routing/ns3-swarm-core) holds what every swarm-routing protocol shares. That split comes from the organisation's golden rule 4 ("every protocol builds on swarm-core, AntHocNet included: `core/` on its `core/`, the ns-3 adapter on `SwarmRoutingBase`") and from swarm-core's golden rule 11.

swarm-core has two layers:
- a simulator-free `core/`: types, ports, `RouteDecision`, `VisitedPath`, `DedupHistory`, the little-endian wire helpers, `PheromoneTable`, the link-metric seam and the energy metric;
- the ns-3 glue: `SwarmRoutingBase`, `DeferredPacketQueue`, the Wi-Fi and ISL link monitors and the router observer.

Most of it was extracted from AntHocNet, the reference implementation. AntHocNet still carried its own copies, so the same primitive lived in two repositories and could drift. swarm-core v1.0.0 freezes its API, and its exit criterion is "AntHocNet's core and ns-3 adapter run on swarm-core with no API change". So this move is on swarm-core's critical path.

Three constraints shaped how to move:

1. **Nothing published may move.** Every number in `results/` and the papers comes from this code (golden rule 8). The browser adapter is held to byte-identical native/WASM traces (golden rule 9).
2. **The C++ standard.** swarm-core's `core/` needs C++20 or later; AntHocNet's `core/` was C++14.
3. **The licence boundary.** `core/` is dual-licensed and may include only the standard library and other dual-licensed `core/` code ([ADR-0028](0028-core-is-dual-licensed.md)).

## Decision

**AntHocNet's `core/` builds on swarm-core's `core/`, and its ns-3 adapter derives from `SwarmRoutingBase`.**

AntHocNet keeps only what is protocol logic, or what no second protocol shares:
- `AntRouterLogic`, `Config`, `AntMessage` and its codec layout;
- the pheromone arithmetic and next-hop selection;
- `GenerationTracker`, `ClassicMetric` and `SinrMetric`;
- `IKinematics`, `shortest_path`;
- the adapter's own link-state, link-quality and energy implementations.

### 1. The C++ standard (S0)

`core/` and the browser adapter moved to **C++23**, the standard of the ns-3 release the organisation supports. ns-3's `CMAKE_CXX_STANDARD_MINIMUM` is 23 from 3.46, so the ns-3 module's floor is ns-3.46.

Measured before the move: the core compiles at C++20/23 with no warning, and its native and WASM decision traces are byte-identical to the C++14 build's.

### 2. Slices, each byte-identical

The move went in one PR per slice:

| slice | what moved | PR |
|---|---|---|
| S0 | C++23 | #1024 |
| S1 | swarm-core as a locked dependency, the licence and pin checks, a duplicate report | #1029 |
| S2 | types, ports, `RouteAction`, `KindName`, `IRouterObserver` | #1034 |
| S3 | `RouteDecision<AntMessage>` | #1035, #1040 |
| S4 | `DedupHistory` | #1042 |
| S5 | `VisitedPath`/`Hop` | #1048 |
| S6 | the codec on `WireWriter`/`WireReader` | #1045 |
| S7 | pheromone storage on two `PheromoneTable`s | #1049 |
| S8 | `ILinkMetric`, `EnergyMetric` | #1047 |
| S9 | the duplicate check as a CI gate | #1050 |
| S10a | the adapter on `SwarmRoutingBase`: sockets, the clock and RNG ports, the observer | #1054 |
| S10b | the pending queue on `DeferredPacketQueue` | |
| S10c | MAC failures through `OnTxFailure` | |
| S10d | multi-interface routes on the base's peer map | |

Every slice changes no arithmetic and no order. Each had to show:
- `make test`;
- the core probes byte-identical;
- `web/test/parity.sh` PASS with the traces' hashes unchanged;
- every AntHocNet `##RUN##` row of the CI ns-3 jobs identical to `main` before it.

One row moved that is not AntHocNet's: the stock AODV row of the leo-walker gate. Its result depends on heap layout, because stock AODV keys per-interface sockets on heap addresses ([swarm-routing/ns3-tools#198](https://github.com/swarm-routing/ns3-tools/issues/198)). #1059 runs that arm in its own process.

**Q2 (maintainer, 2026-11-15):** where a slice cannot be byte-identical, its PR names the cause. A paired control (20 seeds per family, the `ns3-tool` noise verdict) then stands in for byte identity for that slice only. No slice needed it.

Where swarm-core and AntHocNet differed in arithmetic, AntHocNet kept its own. For example, AntHocNet's roulette subtracts `p / sum` from a uniform draw, while swarm-core's `rouletteSelect` draws `u * sum`. Replacing one with the other would move every trajectory, so selection stays AntHocNet's and reads swarm-core's table rows.

Where AntHocNet needed something swarm-core lacked, the gap was added to swarm-core first, additively, before its v1.0.0 freeze, and AntHocNet then used it unchanged:

| swarm-core PR or issue | what AntHocNet needed |
|---|---|
| swarm-routing/ns3-swarm-core#124 | evaporation over current neighbours |
| swarm-routing/ns3-swarm-core#125 | the visited path's storage |
| swarm-routing/ns3-swarm-core#126 | the pending queue's hold classes |
| swarm-routing/ns3-swarm-core#127 | the peer map |
| swarm-routing/ns3-swarm-core#128 | one hook per Wi-Fi retry-limit drop |
| swarm-routing/ns3-swarm-core#166 (swarm-routing/ns3-swarm-core#168, swarm-routing/ns3-swarm-core#171) | purge on use only; a protected `ResolveNextHop`; a per-interface control broadcast; a late `WifiLinkMonitor` connected |

### 3. Consumption by release lock

`core/` and the ns-3 module get swarm-core from **one release**, named in `swarm-core.lock` (tag, commit, the release tarball and its SHA256; swarm-routing/ns3-swarm-core#134).

- `core/CMakeLists.txt` fetches the tarball with `FetchContent` (`URL` + `URL_HASH`).
- `tools/swarm_core.py` unpacks the same tarball for `make install-ns3` and the coverage job.
- The licence check's `core-deps` names the commit the tag names.
- The `swarm-core / pin` check verifies all of them, and org-bot opens the bump PR when a newer release is out.

`SWARM_CORE_DIR=<checkout>/core` builds against a local swarm-core (offline, or an unreleased change).

### 4. The adapter on `SwarmRoutingBase`

`ns3::anthocnet::RoutingProtocol` derives from `SwarmRoutingBase`. It takes from the base:
- the control sockets (`ControlPort`, `RecvControl`, `UnicastControl`, `BroadcastControlOn`);
- the `Clock()` and `Rng()` ports;
- the pending queue (`DeferredPacketQueue`, `Defer`, `PeriodicPurge` off), with AntHocNet's hold reasons as hold classes;
- MAC failures from the node's `WifiLinkMonitor` (`OnTxFailure`);
- the multi-interface peer map (`SetPeerRoute`, `ResolveNextHop`);
- the router observer.

It keeps its own `RouteOutput` and `RouteInput`. They carry the core's decision vector, and they make the RNG draws every outgoing packet has always made, ants included; the base's direct route for a control message would skip those draws. It also keeps its ns-3 `Timer`s: moving them onto `Timers()` would round their `Time` arithmetic through seconds as `double`.

**Q3 (maintainer, 2026-11-15):** `RouteChanged` is `(Ipv4Address dest, Ipv4Address nextHop, bool added)` and `DataDecision` is `(Ipv4Address dest, swarm::core::RouteAction action)`, the base's signatures. `Tx`, `Rx`, `MacReinject` and `MacReinjectSkip` stay AntHocNet's. The base's `GetRouterStats()` now counts AntHocNet's control traffic.

**Q4 (maintainer, 2026-11-15):** `make install-ns3` installs the whole swarm-core ns-3 module as `contrib/swarm-core` (with a marker file), and the anthocnet module links `libswarm-core`.
- A `contrib/swarm-core` it did not install, such as another protocol's, is kept, with a warning when its version differs; `make uninstall-ns3` keeps it too.
- The drop-in tarball, the campaign and release images, the coverage job and the bake recipe carry the module. Bake is paused until bake ships ns-3.46 ([#1026](https://github.com/swarm-routing/AntHocNet/issues/1026)).

### 5. What AntHocNet does not adopt, and why

- **`FuzzyMetric`:** AntHocNet has no fuzzy metric (its registry is classic, SINR and energy).
- **`IslLinkMonitor`:** AntHocNet's inline ISL detection stays (`P2pHelloEvery`, the interface-down detector, the per-interface backlog).

The maintainer marked both pieces experimental in swarm-core 1.0, outside its stability promise, with their second user decided after 1.0.0 (swarm-routing/ns3-swarm-core#160). AntHocNet's adoption of `IslLinkMonitor` is [#1051](https://github.com/swarm-routing/AntHocNet/issues/1051).

- **The base's link-state, link-quality and energy ports:** `WifiLinkMonitor`'s service-time EWMA, backlog and SINR rules differ from AntHocNet's, and AntHocNet's energy probe is the scenario's. Using them would move output, so they would come only as a gated 1.x change.

### 6. The 1.0.0 sequencing

The maintainer decided on 2026-11-15 that swarm-core 1.0.0 already contains every API AntHocNet uses: no 0.x release in between. So the order is:
1. S10a merged on v0.4.0.
2. The remaining base additions merged before the freeze (swarm-routing/ns3-swarm-core#168, swarm-routing/ns3-swarm-core#171).
3. swarm-core 1.0.0 is cut, together with BeeAdHoc v0.2.0, the other criterion.
4. S10b moves `swarm-core.lock` to v1.0.0, and S10b to S10d finish on it.

Anything later is additive and comes in a swarm-core 1.x minor.

## Alternatives considered

- **Keep `core/` at C++14 and use swarm-core in the adapter only.** Rejected: the core keeps its private copies, so neither "no primitive defined in both repositories" nor golden rule 4 can be met.
- **One big switch-over PR.** Rejected: a difference could not be traced to its cause. Slices keep each difference attributable, and S2 to S8 could proceed in parallel.
- **Pin swarm-core by commit (S1's first form).** Replaced in #1044 by the release lock: one artifact with a hash, the same for CMake, the installer and the licence check.
- **Install only swarm-core's `core/` into `contrib/anthocnet` (S2's light variant).** Enough for the core, not for the adapter, which needs the glue. Replaced by the full `contrib/swarm-core` module (Q4).
- **Adopt the base's `RouteOutput`/`RouteInput` too.** Not byte-identical: the base's direct route for a control message skips the per-packet draws AntHocNet makes. So AntHocNet keeps its own, and resolves next hops with the base's rule.

## Consequences

- A primitive defined both here and in swarm-core is a CI failure (`tools/checks/check-swarm-core-dupes.py --gate`, S9).
- A change to swarm-core reaches AntHocNet only through a lock bump PR. That PR runs every gate and the parity check.
- The ns-3 module's floor is ns-3.46. The on-demand 3.36–3.45 legs are gone, and bake is paused until its catalogue reaches the floor.
- A user's own `TraceConnect` on `RouteChanged` or `DataDecision` needs the new argument types (release note).
- Throughput costs measured on the way:
  - encode about 50 → 290 ns and decode 57 → 184 ns per ant (swarm-routing/ns3-swarm-core#157);
  - `history_dedup` 77 → 85 ns;
  - `evaporate_all` about 6.0 → 7.8 µs;
  - selection about +10 %.

  These are tracked there and in [#918](https://github.com/swarm-routing/AntHocNet/issues/918), not here.
