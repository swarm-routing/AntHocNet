# routing-bench — the protocol-agnostic comparison harness module

> **Moved from [`swarm-routing/AntHocNet`](https://github.com/swarm-routing/AntHocNet)
> `ns3/routing-bench/`** (swarm-routing/ns3-tools#2), unchanged: GPL-2.0-only,
> as there. The module name stays `routing-bench`, so every arm module that
> ships a `helper/<module>-bench-arms.cc` (AntHocNet's `anthocnet`, `aomdv`,
> `gpsr`, `oracle`, `adaptive`) keeps registering against it. Issue numbers
> below without a repo are AntHocNet's.

`routing-bench` is the ns-3 module that holds the comparison harness, so
the harness builds without AntHocNet
([ADR-0027](https://github.com/swarm-routing/AntHocNet/blob/main/docs/adr/0027-the-comparison-harness-is-protocol-agnostic.md),
epic [#764](https://github.com/swarm-routing/AntHocNet/issues/764)). It links
only stock ns-3 and names no protocol of its own. Arm modules depend on it;
it depends on none of them.

## What is here

| file | what |
|---|---|
| `model/routing-bench-arm.h/.cc` | the `Arm` contract, `RunContext`, `RunCounts`, `Measurement` (what an arm may touch of the run's measurement) and the `ArmRegistry` (#766) |
| `model/routing-bench-run.cc` | one (seed, arm) run: the world (scenario presets, mobility, channels, devices, traffic), stream pinning, the measurement (FlowMonitor and the trace sinks, the drop book, route stability, the identity-matched set, energy) and `RunOne` (#767) |
| `model/routing-bench-harness.cc` | the command line, the runs over seeds and arms, and the summary and marker output: `bench::RunHarness()` |
| `model/routing-bench-internal.h` | what those two share (`Params`, `Result`, `RunOne`, the defaults); not part of the API |
| `model/arm-isolation.h/.cc` | `--isolateArms` (default on, ns3-tools#198): every arm runs in its own worker process, forked from one zygote made before the harness allocates anything; not part of the API |
| `model/urban-obstacle-loss-model.h/.cc` | the urban building-shadowing channel (#488, #580), Sommer et al.'s obstacle model |
| `model/source-refusal-counter.h/.cc` | the #510 fallback that counts the sends OLSR refuses at the source |
| `model/livelock-watch.h/.cc` | the livelock watch (`--livelockEvents`, NS3-001): a `Scheduler` decorator that stops a run once too many events have run at one simulated instant |
| `model/icmp-error-guard-routing.h/.cc` | the ICMP no-route guard (NS3-001): a decorator around any `Ipv4RoutingProtocol` that applies nsnam/ns-3-dev!3101's suppression rules |
| `helper/icmp-error-guard-helper.h/.cc` | `IcmpErrorGuardHelper`: wraps whatever another `Ipv4RoutingHelper` creates in the guard, and sums its counters |
| `helper/stock-arms.h/.cc` | `RegisterStockArms()`: `aodv` (behind the ICMP no-route guard), `olsr`, `dsdv` and `hwmp` (with its 802.11s devices) |
| `examples/routing-compare.cc` | the program |
| `examples/anthocnet-compare.cc` | the same program under its original name (ADR-0027 §4), kept at least through v3.x |
| `examples/CMakeLists.txt` | finds the arm modules installed beside it and generates `arms-generated.cc` (#768) |
| `test/routing-bench-test-suite.cc` | registry rules, the stock arms, that every stock arm's rows are the same alone and after other arms and a reshaped heap, the ICMP no-route guard (nsnam/ns-3-dev!3101's test; a partitioned AODV network that livelocks on ns-3.49 unguarded and runs guarded), that the field a seed draws does not follow the devices' stream count (swarm-routing/AntHocNet#1222), and the livelock watch (a zero-time storm fails fast, isolated and not; the stock arms' output is byte-identical with it on and off) (`./test.py -s routing-bench`) |

The other arms live in their own modules, each in one
`helper/<module>-bench-arms.cc`: `anthocnet`, `aomdv`, `gpsr` (`gpsr`, `gpcr`,
`gpsr3d`), `oracle` and `adaptive` (`ara`, `termite`, `qrouting`, `beeadhoc`).

## Each arm in its own process (`--isolateArms`)

Stock ns-3 AODV and DSDV keep their per-interface sockets in a
`std::map<Ptr<Socket>, Ipv4InterfaceAddress>`, ordered by heap address, and
AODV draws one broadcast jitter per socket in that order (RREQ, its
forwarding, RERR, hello). On a node with one interface the order cannot vary;
on a node with several (isl-grid, leo-walker, any arm that adds devices) it
decides which interface gets which draw. In one process the heap is what the
earlier arms left, so the same seed gave another row when an earlier arm
allocated differently (ns3-tools#198, swarm-routing/AntHocNet#1039, #362;
[`docs/ns3-bugs.md`](../../docs/ns3-bugs.md) NS3-002). It
is stock code, so the harness isolates instead: by default each arm runs in a
fresh worker process. `RunHarness` forks a zygote before it allocates
anything; for each arm the zygote forks a worker that re-runs the command
line with `--protocols=<arm>`, prints that arm's per-seed lines and sends its
aggregates back for `##MATCH##`, `##COMMON##` and the summary. Every worker
starts from the same heap with the same command line, so an arm's rows do not
depend on the arms before it. The output is byte-identical to
`--isolateArms=0` (one process, as before) wherever nothing reads the heap
layout, apart from `isolateArms=` on `##CONFIG##` and `##PERF##`'s `maxrss_kb`,
which is now each arm's own. An arm's own code is still in its own process:
isolation removes the dependence on other arms, not on the arm's own
allocations, nor on the state `RunHarness` is entered with: a program calls it
first thing from `main()`, as `routing-compare` does, and the test suite forks
every harness run from one prepared state (ns3-tools#211).

## The `aodv` arm and ns-3.49 (NS3-001)

ns-3.49's new `Ipv4L3Protocol::SendIcmpNoRoute` answers every no-route
failure of a forwarding node with an ICMP Destination Unreachable, ICMP errors
and the node's own packets included. Stock AODV defers that error, which has
no route either, through loopback into its full request queue, whose drop
raises the next one: a zero-time livelock, and the run never ends
([`docs/ns3-rcapa/ns3-rcapa-aodv-icmp-livelock.md`](../../docs/ns3-rcapa/ns3-rcapa-aodv-icmp-livelock.md),
[nsnam/ns-3-dev#1408](https://gitlab.com/nsnam/ns-3-dev/-/work_items/1408)).
The images stay stock ns-3 (maintainer decision 2026-11-23), so the harness
carries the fix instead: the `aodv` arm installs AODV behind
`IcmpErrorGuardHelper`, which stops exactly the errors the proposed upstream
fix ([nsnam/ns-3-dev!3101](https://gitlab.com/nsnam/ns-3-dev/-/merge_requests/3101))
suppresses (destination not unicast, non-initial fragment, source not a host,
source one of the node's own addresses, ICMP error), so ns-3.49 with the guard
behaves as a release with the fix. The guard forwards everything else to
AODV and draws no random number; AODV's streams are assigned through the
object aggregated to each node, in `AodvHelper::AssignStreams`'s order, so on
ns-3.48 and earlier (no `SendIcmpNoRoute`, nothing to stop) the rows are
byte-identical to unguarded AODV. On ns-3.49 a run that did finish can still
move: the errors !3101 suppresses are no longer sent. The guard is generic
over any `Ipv4RoutingHelper`; an AODV-derived arm in another module can wrap
its own helper the same way.

## Failing fast on a livelock (`--livelockEvents`)

NS3-001 showed in CI only as job timeouts: an unguarded AODV-family run on
ns-3.49 spins at one simulated instant for as long as the job lets it (a SAGIN
`gpsr3d` run went past 17 minutes). The harness now stops such a run itself.
`model/livelock-watch.h` installs, just before `Simulator::Run()`, a
`Scheduler` that forwards every call to the scheduler the simulator would
have used (`SchedulerType`, `MapScheduler` by default) and counts, in
`RemoveNext`, the events taken at the current timestamp. When that count
reaches `--livelockEvents` (default 1000000; 0 turns it off) it calls
`Simulator::Stop()`, and `RunOne` prints one line on stderr and exits 70:

```
routing-bench: livelock: arm 'aodv-bare' seed 1: simulated time stuck at 4.894679984 s for 1000000 events (--livelockEvents=1000000), 1004694 events in the run; a zero-time event storm like NS3-001, see docs/ns3-bugs.md in swarm-routing/sim-tools
```

Under `--isolateArms` (the default) the arm's worker exits 70 and the parent
aborts with `arm '<arm>' failed in its own process (exit 70; its error is
above)`; with `--isolateArms=0` the process exits 70. The stopped run prints
no row.

Why a scheduler: a storm at one instant never lets a check scheduled for a
later time run, and ns-3 has no public per-event hook, but every event the
simulator runs passes through its scheduler's `RemoveNext`. Every ns-3
scheduler orders events by (timestamp, uid), and the simulator assigns both,
so the wrapped scheduler hands out the same events in the same order: a run
the watch does not stop is byte-identical to the run without it (the suite
compares the stock arms' whole output with the watch on and off). The limit
is an event count, not a wall clock, so whether a seed stops is
deterministic too, and the line goes to stderr, never into a marker. The
cost is one virtual call and a comparison per event: on ns-3.49 debug,
`--scenario=paper --time=20 --runs=2` over `aodv,olsr,dsdv` (6.1 M events)
took 181.4 s of CPU with the watch off and 185.6 s on (three interleaved
runs each, 176 to 193 s), within the run-to-run noise, and printed the same
bytes apart from `##PERF##` in all three pairs.

The watch covers every arm `routing-compare` runs, the vendored AOMDV/GPSR
family's included. A harness with its own `main` (AntHocNet's satellite
programs) gets the same by calling `bench::WatchForLivelock(limit)` before
`Simulator::Run()` and checking `bench::LivelockStatus()` after it.

The default, 1000000, is three orders of magnitude above the most events any
scenario ran at one instant (the watch's `maxAtTs`, ns-3.49, arms `aodv`,
`olsr`, `dsdv`; the full scenarios at their own lengths):

| scenario | seeds | most events at one instant |
|---|---|---|
| default (no `--scenario`) | 2 | 117 |
| `paper` | 2 | 313 |
| `thesis` (100 nodes, 900 s) | 1 | 843 |
| `fanet` | 2 | 212 |
| `vanet` | 1 | 562 |

An unguarded stock AODV run on ns-3.49's sparse field
(`--nNodes=16 --area=500 --range=100`, the ns3-containers `sparse-aodv`
canary's shape) stops at 4.89 s of simulated time, seed 1, after 46 s of wall time on the
debug build (1000000 ICMP-storm events; the storm costs about 45 µs per event
there), where it used to run until the job's timeout; the guarded
`aodv` arm runs the same three seeds to the end without tripping it.

## The same field on every ns-3 release (stream layout)

Every random variable of a run is pinned to a stream inside the seed's own
block, `[seed * 10^6, (seed + 1) * 10^6)` (AntHocNet #352), so a run is a
function of its seed and flags, not of the runs before it. Inside the block:

| streams (from the block's start) | what |
|---|---|
| running counter, from 0 | the channel; then the devices' **reserved** range, `bench::kAdhocDeviceStreams` (2) per node; the position allocator; mobility (and the Manhattan schedule); the internet stack (ARP jitter); the arm's routing streams |
| 500 000 (`kDeviceStreamOffset`) | the device sub-block: hwmp's 802.11s devices (#708), or the ad-hoc MAC's own streams |
| 990 000 (`kAppStreamOffset`) | the applications: flow start times and on/off variables (#431) |

The reserved range is what ns-3.48's `WifiHelper::AssignStreams` takes for an
ad-hoc device (the PHY's stream and the Txop's backoff), and the running
counter advances by exactly that per node on every release and in every arm.
ns-3.49 added IBSS beacons to `AdhocWifiMac`, which take two more streams
per device; taken from the running counter, as before, they moved the
position allocator and everything after it, so the same seed drew another
field on 3.49 (swarm-routing/AntHocNet#1222). `bench::AssignWifiDeviceStreams`
gives the PHY, station manager and Txop their ns-3.48 indices on the reserved
range and the MAC's own streams (3.49's beacon jitter and beacon Txop) the
device sub-block; a release whose PHY, managers or Txops need another count
gets all of its device streams from the device sub-block instead. So:

- on ns-3.48 every stream keeps its index and the output is byte-identical to
  earlier releases of the harness;
- on ns-3.49 a seed places and moves the nodes exactly as on 3.48, and the
  arms' routing jitter, ARP and the applications draw from the same streams;
  rows still move wherever ns-3 itself changed behaviour between the releases;
- a future release whose devices take another count keeps the field too
  (the suite's placement test runs the harness with the QoS ad-hoc MAC, four
  Txops instead of one, and requires the same `--dumpTrace` movement).

No two consumers share a stream: the running counter aborts before it
reaches the device sub-block, the device sub-block before the applications.

## Registering an arm

Registration is an explicit call, never a static initialiser (ADR-0027 §3).
A module adds its arms in one function, in a file named
`helper/<module>-bench-arms.cc`:

```cpp
#include "ns3/routing-bench-arm.h"

class MyArm : public ns3::bench::Arm {
  public:
    std::string Name() const override { return "myproto"; }
    void InstallRouting(ns3::InternetStackHelper& internet, ns3::bench::RunContext&) override {
        internet.SetRoutingHelper(m_helper);
    }
    int64_t AssignStreams(ns3::bench::RunContext& run, int64_t stream) override {
        return m_helper.AssignStreams(run.nodes, stream);
    }
  private:
    MyHelper m_helper;
};

ns3::Ptr<ns3::bench::Arm> MakeMyArm() { return ns3::Create<MyArm>(); }

namespace ns3 { namespace bench {
void RegisterArms_mymodule() { RegisterArm("myproto", "My protocol (Author 2026)", &MakeMyArm); }
}}
```

Compile the file only when `routing-bench` is installed and enabled, and name
its library through a variable: ns-3 scans module CMakeLists for library
tokens to resolve dependencies, and your module should still build alone. The
pattern, from `ns3/baselines/adaptive/CMakeLists.txt`:

```cmake
set(mymodule_bench_sources)
set(mymodule_bench_libs)
set(mymodule_bench_module routing-bench)
if(EXISTS ${PROJECT_SOURCE_DIR}/contrib/${mymodule_bench_module}/CMakeLists.txt
   AND mymodule_bench_module IN_LIST ns3-all-enabled-modules)
  set(mymodule_bench_sources helper/mymodule-bench-arms.cc)
  set(mymodule_bench_lib lib${mymodule_bench_module})
  set(mymodule_bench_libs ${${mymodule_bench_lib}})
endif()
# then ${mymodule_bench_sources} in SOURCE_FILES, ${mymodule_bench_libs} in LIBRARIES_TO_LINK
```

`routing-compare`'s CMake globs `contrib/*/helper/*-bench-arms.cc`, keeps the
enabled modules, and calls each `RegisterArms_<module>()` at start-up. A
duplicate or empty name aborts.

The optional hooks, in the order the harness calls them, are documented in
[`routing-bench-arm.h`](model/routing-bench-arm.h) and ADR-0027 §2.

## Compatibility

No published number may move: the byte-identical control on the committed
cells was AntHocNet's
[#769](https://github.com/swarm-routing/AntHocNet/issues/769). The user guide
is still AntHocNet's
[comparison-harness.md](https://github.com/swarm-routing/AntHocNet/blob/main/docs/comparison-harness.md);
this repository's own guide is swarm-routing/ns3-tools#4.
