// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2026 Daniel Henrique Joppi

/*
 * AntHocNet's pending queue, as swarm-core's DeferredPacketQueue (the one
 * SwarmRoutingBase holds; S10b of #1019, swarm-routing/ns3-swarm-core#126):
 * data packets with no known route are held there until a backward ant
 * installs one, with a per-entry lifetime and a global size cap. This header
 * keeps what is AntHocNet's: why a packet is held (its hold class), the
 * queue's configuration and the per-reason hold statistics the harness reads.
 */
#ifndef ANTHOCNET_RQUEUE_H
#define ANTHOCNET_RQUEUE_H

#include "ns3/boolean.h"
#include "ns3/nstime.h"
#include "ns3/swarm-deferred-packet-queue.h"
#include "ns3/uinteger.h"

#include <cstdint>

namespace ns3 {
namespace anthocnet {

/// Why a data packet is sitting in the pending queue — used to attribute the
/// delay tail (issue #21) to the dominant hold path. SETUP = first reactive
/// discovery for a destination this node has never yet routed to; RECONV =
/// re-discovery after a previously-known route was lost; REPAIR = re-injected
/// after a MAC transmit failure while a local repair runs (#46).
enum HoldReason : uint8_t { HOLD_SETUP = 0, HOLD_RECONV = 1, HOLD_REPAIR = 2 };
constexpr uint8_t kHoldReasons = 3;

/// Per-reason hold-time accounting over a run (issue #21 tail attribution).
/// The #88 sweep showed the median offered packet delivers in ~3 ms (dOff50):
/// the mean/jitter gap vs AODV is carried entirely by queue-held packets, so
/// which reason accumulates the most *delivered* hold time is the dominant
/// cause of the tail. `dropped*` counts packets that left the queue without
/// being forwarded — aged out at QueueTimeout, or evicted as the oldest entry
/// when the queue was full (#215) — a PDR loss rather than a delay, but the
/// same hold mechanic. It does NOT count packets released by a failed local
/// repair (`DiscardPending`); those are a distinct drop cause and are counted
/// by RoutingProtocol::RepairDiscardedPackets().
struct HoldStats {
    uint64_t deliveredCount[kHoldReasons] = {0, 0, 0};
    double deliveredSumS[kHoldReasons] = {0.0, 0.0, 0.0};
    double deliveredMaxS[kHoldReasons] = {0.0, 0.0, 0.0};
    uint64_t droppedCount[kHoldReasons] = {0, 0, 0};
    double droppedSumS[kHoldReasons] = {0.0, 0.0, 0.0};
};

/// The queue as AntHocNet's RequestQueue was (#21): 64 packets, the oldest
/// dropped first when full; each packet expires `timeout` after its latest
/// enqueue (re-armed when it is put back), and a reconvergence or repair hold
/// no later than its cap after its first enqueue (0: no cap). With
/// SwarmRoutingBase's PeriodicPurge off, an expired packet is dropped only when
/// the queue is next used, as before.
inline void ConfigurePendingQueue(DeferredPacketQueue& q, Time timeout, Time reconvCap,
                                  Time repairCap) {
    q.SetAttribute("MaxPackets", UintegerValue(64));
    q.SetAttribute("MaxAge", TimeValue(timeout));
    q.SetAttribute("RearmOnEnqueue", BooleanValue(true));
    q.SetHoldClassCap(HOLD_RECONV, reconvCap);
    q.SetHoldClassCap(HOLD_REPAIR, repairCap);
}

/// The per-reason view of the queue's per-class accounts: `forwarded` is
/// AntHocNet's `delivered` (the packet left toward a next hop), `dropped` the
/// packets that expired or overflowed.
inline HoldStats HoldStatsOf(const DeferredQueueStats& s) {
    HoldStats h;
    for (uint8_t r = 0; r < kHoldReasons; ++r) {
        const DeferredClassStats& c = s.byClass[r];
        h.deliveredCount[r] = c.forwarded;
        h.deliveredSumS[r] = c.forwardedHoldSumS;
        h.deliveredMaxS[r] = c.forwardedHoldMaxS;
        h.droppedCount[r] = c.dropped;
        h.droppedSumS[r] = c.droppedHoldSumS;
    }
    return h;
}

}  // namespace anthocnet
}  // namespace ns3

#endif  // ANTHOCNET_RQUEUE_H
