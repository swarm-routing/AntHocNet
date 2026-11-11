# The comparison harness: running it and adding a protocol

`routing-compare` (still also built as `anthocnet-compare`) is the ns-3
program behind every MANET-family number in this repository. It runs several routing protocols ("arms") on the **same
seeds**, with the same mobility, the same traffic and the same channel. It
reports per-seed rows that pair across arms, so a difference between two
protocols carries a confidence interval rather than an anecdote.

This page is for anyone who wants to use the harness: to rerun a published
cell, to compare an existing protocol, or to add their own. The decisions
behind it are in [methodology.md](benchmarks/methodology.md) and the metric
definitions are in [metrics.md](benchmarks/metrics.md).

[← Benchmark index](benchmarks.md) · [Methodology](benchmarks/methodology.md) · [Metrics](benchmarks/metrics.md) · [v2.5.0 comparators](benchmarks/v2.5.0-comparators.md)

> **Status: protocol-agnostic since v2.6.0**
> ([ADR-0027](adr/0027-the-comparison-harness-is-protocol-agnostic.md)). The
> harness is its own ns-3 module, `routing-bench`, which links only stock ns-3
> and names no protocol. Every arm registers itself from its own module, so
> the harness builds and runs without AntHocNet. `anthocnet-compare` is the
> same program under its old name, kept at least through v3.x. The move
> reproduced every committed MANET-family cell byte for byte
> ([control](benchmarks/v2.6.0-harness-control.md)).
>
> **Since v2.7.0 the harness and the readers live in
> [swarm-routing/ns3-tools](https://github.com/swarm-routing/ns3-tools)**
> ([ns3-tools#29](https://github.com/swarm-routing/ns3-tools/issues/29)):
> `routing-bench` and the `ns3_tool` Python package (`bench_parse`,
> `stats_util`, `native_view`, `comparator_view`, `pool_runs`,
> `sweep_summary`). This repository builds and reads with the release that
> [`ns3-tools.lock`](#the-ns3-tools-pin) pins and keeps no copy of either.

## What it measures

One invocation runs every arm in `--protocols` on seeds
`--firstRun … --firstRun+--runs−1`. For each seed and arm it builds a fresh
ns-3 world: nodes, mobility, Wi-Fi channel and CBR or TCP flows. It installs
the arm, runs it and measures it with FlowMonitor and the harness's own trace
sinks. Arms never share a process state that could leak between them. Their
randomness is pinned through `AssignStreams`, so seed *k* gives every arm the
same node trajectories and flow schedule
([#352](https://github.com/swarm-routing/AntHocNet/issues/352)).

Built-in arms, as of v2.5.0:

| arm | what it is | module |
|---|---|---|
| `anthocnet` | this project's protocol | `anthocnet` |
| `aodv`, `olsr`, `dsdv` | stock ns-3 | `routing-bench` (stock arms) |
| `aomdv` | multipath AODV | `aomdv` (vendored) |
| `gpsr`, `gpcr`, `gpsr3d` | geographic routing and its VANET/FANET modes | `gpsr` (vendored) |
| `hwmp` | 802.11s mesh path selection | `routing-bench` (stock ns-3 `mesh`) |
| `oracle` | shortest-path ground truth, an upper bound | `oracle` |
| `ara`, `termite`, `qrouting`, `beeadhoc` | ant-colony, stigmergic, learned and bee-inspired comparators (`beeadhoc`: v2.7.0, #589) | `adaptive` |

`routing-compare --PrintHelp` lists the arms actually installed: the
`--protocols` help is built from the registry. With only `routing-bench`
installed, the harness has the four stock arms.

Scenario presets (`--scenario=paper|fanet|vanet|disaster|…`) set the field,
mobility and channel of each network family. Any flag overrides a preset.

## Running it

With the modules installed into an ns-3 tree (`make install-ns3
NS3DIR=/path/to/ns-3`, which fetches `routing-bench` at the
[pin](#the-ns3-tools-pin), or the release's `anthocnet-ns3-modules-<ver>.tar.gz`
unpacked into `contrib/`; `routing-bench` alone, ns3-tools'
`routing-bench-<ver>.tar.gz`, is enough for the stock arms):

```bash
cd /path/to/ns-3
./ns3 configure --enable-examples --enable-tests && ./ns3 build
./ns3 run "routing-compare --scenario=paper --runs=10 --time=900 \
    --protocols=anthocnet,aodv,olsr --diag"
```

Useful flags:

- `--nNodes`, `--areaX/--areaY`, `--range`;
- `--mobility=rwp|ssrwp|gaussmarkov|manhattan|trace`;
- `--propagation=range|tworay|nakagami|urban`;
- `--flows`, `--cbrBps`, `--transport=udp|tcp`, `--flowsFile` (explicit flows);
- `--firstRun` to split seeds across jobs;
- any `--ns3::<TypeId>::<Attribute>=<value>` to change one protocol's parameter.

`./ns3 run "routing-compare --PrintHelp"` lists everything.

The output is a summary table plus machine-readable marker lines:

- `##CONFIG##` is the effective configuration.
- `##RUN## <seed> <arm> pdr delay delay99 thrput nrl …` is the per-seed row
  that pairing uses.
- `##ROUTE##`, `##DROPID##`, `##COMMON##` and `##GOODPUT##` cover route
  stability, the drop-cause identity, the identity-matched packet set and
  TCP goodput.
- `# drops` is the drop-cause book, which must sum to about 100 %.

[metrics.md](benchmarks/metrics.md) defines every one of them. Treat the
markers as the interface: the tools below read them, never the human table.

## Reading the results

Never compare numbers by eye. The scripts do the arithmetic and the noise call.
The readers are ns3-tools' `ns3_tool` package; `tools/ns3_tools.py run MODULE`
runs one at the pinned version (it fetches and checks the wheel first), and
`pip install` of the same wheel gives the `ns3-bench-parse`, `ns3-native-view`,
`ns3-comparator-view`, `ns3-pool-runs` and `ns3-sweep-summary` commands.
`scenario_check.py` stays here: it carries this repository's own rules
(listed on [ns3-tools#29](https://github.com/swarm-routing/ns3-tools/issues/29)).

| tool | does |
|---|---|
| `tools/bench/scenario_check.py preflight …` | checks a configuration *before* dispatch: partitioned field, saturated channel, single-hop degeneracy |
| `tools/bench/scenario_check.py results FILE` | plausibility checks on a result: PDR bounds, the drop book closing, delay99 ≥ mean |
| `tools/ns3_tools.py run bench_parse --ab A B --proto X` | paired per-seed deltas of one arm between two files, with t and bootstrap CIs and a noise verdict |
| `tools/ns3_tools.py run bench_parse --get FIELD --from run\|table\|csv\|paths --proto X -` | one field of one arm's result, read by name: a `##RUN##` metric, a summary-table metric, a `--csv` column by its header name, or a `# paths` key |
| `tools/ns3_tools.py run comparator_view` | arm-versus-arm readings: anchors, and arms paired against committed control cells |
| `ns3-campaign plan/dispatch/collect` (swarm-routing/ns3-tools; `tools/bench/campaign.py` for legacy arms) | a set of cells × arms run on the `benchmark-cell` workflow on one pinned `source_sha`, throttled, then collected |

The CI gates in `tools/checks/` read marker lines through the same reader,
with `bench_parse --get`, never by awk column position (#791). A marker
change the reader absorbs therefore cannot make a gate read the wrong number,
and `tools/bench/test_bench_parse_query.py` checks the pinned reader against
the old awk expressions on every committed cell.

## Adding a protocol

There are two ways in, depending on what you are bringing.

### Your own ns-3 routing module

If your protocol already is an ns-3 `Ipv4RoutingProtocol` with an
`Ipv4RoutingHelper`, it needs no rewrite. Vendor it like
`ns3/baselines/aomdv/` or `ns3/baselines/gpsr/`: its own directory, a
`CMakeLists.txt` built with `build_lib`, and a README carrying the fidelity
sheet. Then register it, as described [below](#registering-an-arm).

### A per-hop, table-driven arm on the `adaptive` base

For a protocol written from a paper that keeps a per-neighbour table (ant
colony, stigmergic, Q-learning-style), `ns3/baselines/adaptive/` gives you a
base class, `ns3::adaptive::RoutingBase`, that does all the ns-3 plumbing.
The arm writes only its policy. ARA, Termite and Q-routing are each one file
pair on top of it.

**What the base does for you:**

| plumbing | detail |
|---|---|
| control channel | UDP sockets per interface, unicast and broadcast, on the arm's port. The harness counts it as control traffic automatically |
| no-route hold | a packet without a next hop waits in a bounded queue (64 packets, 30 s) and is released when the arm learns a route, or dropped as a route drop |
| previous hop | `PrevHopTag` tells every hop which neighbour sent the packet, which ns-3's `RouteInput` does not |
| link failure | MAC retry-limit drops and unanswered ARP are turned into `OnLinkFailure(neighbor)`, and the lost data packet is recovered and re-routed. Traces `MacReinject` and `ArpReinject` keep the drop book honest: an arm that re-routes is booked per packet, by terminal fate ([metrics](benchmarks/metrics.md#per-packet-terminal-fate-on-the-adaptive-arms-761), #761) |
| randomness | one pinned random stream per node (`AssignStreams`) |

**What the arm implements:**

| hook | called when |
|---|---|
| `ControlPort()` | once: the arm's UDP port |
| `SelectNextHop(dst, src, prev, p)` | a data packet needs a next hop; return `Ipv4Address()` for "none" |
| `OnDataReceived(p, header, prev, backtracked)` | a data packet arrived from neighbour `prev`; learn from it, return false to drop it |
| `OnDataForwarded(p, header, next)` | optional: a data packet is being handed to `next` |
| `OnNoRouteInTransit(…)` | optional: no route at an intermediate node; return true to dispose of the packet yourself |
| `StartDiscovery(dst)` | data is held for `dst` and no route exists |
| `RecvControl(p, from, receiver)` | a control message arrived |
| `OnLinkFailure(neighbor)` | the link to `neighbor` is gone |
| `KnownNeighbors()` | the neighbours the failure resolver may match |

The base's services cover sending and flushing:

- `BroadcastControl`, `BroadcastControlJittered` and `UnicastControl` send
  control messages.
- `FlushQueue(dst)` releases held packets once you have a route.
- `DropQueued(dst)` gives up on them.
- `Rng()` is the pinned random stream.

`adaptive-tables.h` holds reusable pheromone tables (geometric or exponential
decay, bounded with floor, ceiling and pruning) and the proportional draw.
`ControlHeader` is a 24-byte control header with type, hops, ttl, seq, origin,
target and a value.

The module links only stock ns-3 (core, network, internet, wifi). Start from
[`qrouting-routing-protocol.cc`](https://github.com/swarm-routing/AntHocNet/blob/main/ns3/baselines/adaptive/model/qrouting-routing-protocol.cc),
the smallest complete arm (about 270 lines), and install it with `AdaptiveHelper("ns3::adaptive::<YourType>")`.

### Registering an arm

An arm is a class derived from `ns3::bench::Arm`
([`routing-bench-arm.h`](https://github.com/swarm-routing/ns3-tools/blob/v0.1.0/ns3/routing-bench/model/routing-bench-arm.h)),
created fresh for every (seed, arm) run. Three hooks are required:

- `Name()`, the `--protocols` token;
- `InstallRouting(internet, run)`, which sets your routing helper on the stack;
- `AssignStreams(run, stream)`, which pins your protocol's random streams and
  returns how many it took (#352).

The optional hooks cover everything else the harness used to special-case:
own devices (`hwmp`), post-stack wiring (`gpsr`), trace connections, refused
sends added back to the offered load (`oracle`, `olsr`), extra diagnostics,
self-checks and the replay. The stock arms in
[`stock-arms.cc`](https://github.com/swarm-routing/ns3-tools/blob/v0.1.0/ns3/routing-bench/helper/stock-arms.cc)
and the module arms are the worked examples; Q-routing's is a dozen lines
([`adaptive-bench-arms.cc`](https://github.com/swarm-routing/AntHocNet/blob/main/ns3/baselines/adaptive/helper/adaptive-bench-arms.cc)).

To join the harness, your module ships one file,
`helper/<module>-bench-arms.cc`, defining
`ns3::bench::RegisterArms_<module>()` (hyphens become underscores), which
calls `RegisterArm("<name>", "<summary>", &MakeYourArm)` for each arm. Compile
it into your module only when `routing-bench` is installed and enabled, the way
[`adaptive`'s CMakeLists](https://github.com/swarm-routing/AntHocNet/blob/main/ns3/baselines/adaptive/CMakeLists.txt)
does. `routing-compare` finds the file at configure time, links your module and
calls the function at start-up. No harness source, check script or workflow
changes.

What still needs a line: your module in the `--enable-modules` lists
(`tools/checks/check-allowlists.sh` derives the module set from the tree and
fails on a list that is missing it).

## The gates before a number counts

A protocol this project did not design must clear three gates before its
numbers are published (AGENTS.md, Conventions). These keep comparisons honest:
two vendored arms once compiled, passed CI and forwarded nothing
([#425](https://github.com/swarm-routing/AntHocNet/issues/425),
[#416](https://github.com/swarm-routing/AntHocNet/issues/416)).

1. **Fidelity sheet.** Every parameter from the paper with its section, every
   value the paper does not give marked as a repo choice, and every deviation
   with its reason. The model is the
   [`adaptive` README](https://github.com/swarm-routing/AntHocNet/blob/main/ns3/baselines/adaptive/README.md).
2. **Delivery smoke** (`tools/checks/check-arm-delivery.sh <ns3-dir>`, on
   every PR; [#439](https://github.com/swarm-routing/AntHocNet/issues/439)).
   Every arm in `routing-compare`'s `--protocols` help runs on a small static multi-hop field.
   It must deliver above a floor, use multi-hop paths, and close its drop book.
3. **Anchor.** The arm must reproduce its own paper's headline trend on a
   scenario matching the paper's. The rule is fixed in a pre-registration page
   *before* the run. A miss ships as a written infeasibility verdict, not as
   numbers.

   The [v2.5.0 comparators](benchmarks/v2.5.0-comparators.md) are the worked
   example: ARA passed; Termite and Q-routing missed and shipped as verdicts.

## Packaging and what is planned

**Today (v2.7.0):**

- The harness and the readers are
  [swarm-routing/ns3-tools](https://github.com/swarm-routing/ns3-tools)
  releases: `routing-bench-<ver>.tar.gz` (the harness and the stock arms,
  for `contrib/`) and the `ns3_tool` wheel (the readers, with the
  versioned marker contract). Use them without AntHocNet.
- This release ships `anthocnet-ns3-modules-<ver>.tar.gz`, six ns-3 contrib
  modules to unpack into `contrib/`: `routing-bench` at the
  [pin](#the-ns3-tools-pin), `anthocnet`, and the comparison modules `aomdv`,
  `gpsr`, `oracle` and `adaptive`. Keep `routing-bench` and drop the rest to
  run the harness on the stock arms alone, or add your own arm module beside
  it.
- `adaptive-<ver>.tar.gz` is the `adaptive` module on its own, for any ns-3
  ≥ 3.36 tree. Install it beside `routing-bench` to get its arms in
  `routing-compare`, or use `AdaptiveHelper` in your own program.

**Planned:** the bring-your-own-protocol tutorial with an arm template is
[ns3-tools#4](https://github.com/swarm-routing/ns3-tools/issues/4), and the
vendored arms move to ns3-tools later
([ns3-tools#34](https://github.com/swarm-routing/ns3-tools/issues/34)). See the
[roadmap](roadmap.md) for the exit criteria.

### The ns3-tools pin

[`ns3-tools.lock`](https://github.com/swarm-routing/AntHocNet/blob/main/ns3-tools.lock)
at the repository root names the one ns3-tools release this repository uses:
its tag, the commit the tag names, and the sha256 of the two assets it takes
(`routing-bench-<ver>.tar.gz` and `ns3_tool-<ver>-py3-none-any.whl`).
`tools/ns3_tools.py` reads it:

```bash
python3 tools/ns3_tools.py fetch                # download, check sha256, unpack into .ns3-tools/<tag>/
python3 tools/ns3_tools.py path routing-bench   # what `make install-ns3` copies to contrib/
python3 tools/ns3_tools.py run bench_parse A B  # any ns3_tool reader, at the pin
python3 tools/ns3_tools.py verify               # the tag still names the commit; the assets still match
```

`make install-ns3`, every CI gate, the release packages, the campaign tools
and the chart scripts go through it, so a published number always names one
harness and one reader version. The release bundle ships the lock, and the
release notes name the pin. `NS3_TOOLS_DIR=<ns3-tools checkout>` uses a local
checkout instead, for work on a bump only.

**Bumping it** is a PR that changes every line of the lock together:

1. Take the new tag's commit (`git ls-remote https://github.com/swarm-routing/ns3-tools refs/tags/vX.Y.Z`)
   and the two sha256 from the release's `SHA256SUMS`.
2. Run `python3 tools/ns3_tools.py verify`, then the readers' corpus check:
   `python3 tools/bench/test_bench_parse_query.py`.
3. If the harness changed, show the committed cells are unchanged: re-run the
   [v2.6.0 control](benchmarks/v2.6.0-harness-control.md) plan on the new pin
   and check it with
   `python3 tools/bench/harness_control.py tools/bench/campaigns/v2.6.0-harness-control.yaml`,
   or at least a 20-seed cell before and after the bump with identical
   `##RUN##` lines. A reader change keeps its output byte-identical on
   `results/cells/` unless the change is the point, which the PR then says.

## Citing

Cite the harness and the comparators through the release DOI of the version
you used. The version DOIs are in
[CONTRIBUTING.md](https://github.com/swarm-routing/AntHocNet/blob/main/CONTRIBUTING.md#doi-record);
v2.5.0 is `10.5281/zenodo.23748858`. The concept DOI in `CITATION.cff` always
resolves to the latest release. Name the arms and the version: a number
measured at one version is not guaranteed at another
([provenance](benchmarks/methodology.md#provenance-which-version-a-number-was-measured-at)).
