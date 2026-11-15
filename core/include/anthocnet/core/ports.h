// SPDX-License-Identifier: MIT OR GPL-2.0-only
// Copyright (C) 2026 Daniel Henrique Joppi

/**
 * Ports the simulator adapters implement.
 *
 * The core depends only on these interfaces, never on a concrete simulator.
 * NS-2 implements them over Scheduler/Random/Node; NS-3 over
 * Simulator::Now/UniformRandomVariable/Ipv4 interfaces.
 */
#ifndef ANTHOCNET_CORE_PORTS_H
#define ANTHOCNET_CORE_PORTS_H

#include <cstdint>
#include <functional>
#include <vector>

#include "anthocnet/core/ant_message.h"
#include "anthocnet/core/route_decision.h"
#include "anthocnet/core/types.h"
#include "swarm-observer.h"
#include "swarm-ports.h"

namespace anthocnet {
namespace core {

// The ports swarm-core defines (swarm-routing/ns3-swarm-core#14, S2): the same
// signatures AntHocNet's own had, now defined once in swarm-core's
// swarm-ports.h, which documents each. In short:
//  - IClock: simulation time (seconds).
//  - IRng: all algorithm randomness (uniform() in [0, 1), uniformInt(n) in
//    [0, n)), so runs are reproducible from the adapter's RNG stream.
//  - INeighborProvider: the current one-hop neighbours.
//  - ITimerScheduler: deferred work (ant generation, table maintenance);
//    swarm-core's adds cancellable timers with defaults, unused here.
//  - ILinkState: MAC queue length and smoothed service time toward a next hop
//    (item 10/A2, [1] §3.2; #206: kInvalidAddress aggregates across
//    interfaces); optional, the core falls back to the ant's transit time.
//  - ILinkQuality: per-neighbour SINR in dB, NaN before a sample (#181,
//    ADR-0024).
//  - IEnergy: residual battery fraction 0..1, NaN without an energy model
//    (#145).
using swarm::core::IClock;
using swarm::core::IEnergy;
using swarm::core::ILinkQuality;
using swarm::core::ILinkState;
using swarm::core::INeighborProvider;
using swarm::core::IRng;
using swarm::core::ITimerScheduler;

/// This node's own position and velocity (#573, ADR-0025). Absent, not zero:
/// an adapter that does not know where the node is returns false, because a
/// node reported at the origin would sit on top of every other such node and
/// read as an immortal link. Neighbours' kinematics never come through this
/// port; they come from their hellos, as a deployed protocol's would.
class IKinematics {
public:
    virtual ~IKinematics() = default;
    /// Fill `out` and return true, or return false (leaving `out` untouched)
    /// when the position is unknown.
    virtual bool self(Kinematics& out) const = 0;
};

/// Optional observer the core notifies of routing events (item 15). It only
/// *reports*: it never makes routing decisions or does I/O, so the core stays
/// pure. Adapters implement it to fan events out to their native trace
/// machinery (ns-3 TracedCallback). All methods default to no-ops; when no
/// observer is set the core pays only a null-pointer check.
///
/// swarm-core's router observer (swarm-observer.h; #917) supplies five
/// events, same names, arguments and meaning as AntHocNet's own had:
/// onControlSent(kind, broadcast, bytes) and onControlReceived(kind, bytes)
/// (kind: controlKind(), the wire type byte, 0x80 set for a backward ant;
/// bytes: codec::serializedSize), onRouteChanged(dest, nextHop, added),
/// onDataDecision(dest, action) (onDataPacket's verdict, or a decision the
/// adapter takes without the core) and onStateSize(entries, bytes) (regular
/// plus virtual pheromone entries, kStateEntryBytes each, when it changes).
/// This class adds the two ant events, which fire beside them, never instead.
class IRouterObserver : public swarm::core::IRouterObserver {
public:
    /// This node put an ant on the medium (origination or forwarding).
    virtual void onAntSent(AntType /*type*/, AntDirection /*dir*/, bool /*broadcast*/) {}
    /// This node processed a (non-duplicate) received ant.
    virtual void onAntReceived(AntType /*type*/, AntDirection /*dir*/) {}
};

/// One entry of the kind-name table: a control kind and the name readers print
/// ("REACTIVE_BANT", not "130"); swarm-core's KindName (S2).
using swarm::core::KindName;

/// Set in a control kind for a backward ant. The wire carries the direction in
/// its own byte (docs/wire-format.md), so the kind folds it into the type
/// byte's free high bit; the AntType values use bits 0x01..0x10.
constexpr std::uint8_t kBackwardKindBit = 0x80;

/// The observer's kind for an ant of `type` travelling in `dir`.
inline std::uint8_t controlKind(AntType type, AntDirection dir) {
    return static_cast<std::uint8_t>(static_cast<std::uint8_t>(type) |
                                     (dir == AntDirection::Down ? kBackwardKindBit : 0));
}

/// Every control kind AntHocNet sends, by kind. A kind added to AntType is
/// added here too (test_observability checks the table covers them).
constexpr KindName kControlKindNames[] = {
    {0x01, "HELLO"},    {0x02, "REACTIVE_FANT"}, {0x04, "PROACTIVE_FANT"}, {0x08, "REPAIR_FANT"},
    {0x10, "LINKFAIL"}, {0x82, "REACTIVE_BANT"}, {0x84, "PROACTIVE_BANT"}, {0x88, "REPAIR_BANT"},
};

/// The name of `kind`, or nullptr when the table does not name it.
inline const char* findKindName(std::uint8_t kind) {
    for (const KindName& k : kControlKindNames) {
        if (k.kind == kind) return k.name;
    }
    return nullptr;
}

/// Payload bytes of one pheromone entry, as onStateSize counts them: the
/// (neighbour, destination) key and its value.
constexpr std::uint32_t kStateEntryBytes = 2 * sizeof(NodeAddress) + sizeof(double);

}  // namespace core
}  // namespace anthocnet

#endif  // ANTHOCNET_CORE_PORTS_H
