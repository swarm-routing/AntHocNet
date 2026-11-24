// Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
// SPDX-License-Identifier: GPL-2.0-only

#ifndef ROUTING_BENCH_ICMP_ERROR_GUARD_ROUTING_H
#define ROUTING_BENCH_ICMP_ERROR_GUARD_ROUTING_H

/**
 * NS3-001 (docs/ns3-rcapa/ns3-rcapa-aodv-icmp-livelock.md, #238): ns-3.49's
 * new Ipv4L3Protocol::SendIcmpNoRoute answers every no-route failure of a
 * forwarding node with an ICMP Destination Unreachable, ICMP errors and the
 * node's own packets included. Under stock AODV that error has no route
 * either, is deferred through loopback into AODV's full request queue, whose
 * drop raises the next error, all at one simulated instant: the run never
 * ends. Reported upstream as nsnam/ns-3-dev#1408; the fix proposed there is
 * nsnam/ns-3-dev!3101.
 *
 * The guard is an Ipv4RoutingProtocol that wraps any other one (a decorator,
 * protocol-agnostic: IcmpErrorGuardHelper takes any Ipv4RoutingHelper) and
 * makes the node behave as !3101 does, without touching ns-3. Everything is
 * forwarded to the inner protocol. Where Ipv4L3Protocol decides to send the
 * no-route error (the ErrorCallback passed down through RouteInput, and
 * RouteInput returning false), the guard classifies the offending datagram
 * with !3101's rules, in its order:
 *   - destination not unicast (broadcast, subnet-directed broadcast, multicast);
 *   - a non-initial fragment;
 *   - source not a host (0/8, 127/8, >= 224/4, or not unicast);
 *   - source one of this node's own addresses;
 *   - an ICMP error (type 3, 4, 5, 11, 12) or an empty ICMP datagram.
 * When !3101 would send nothing, the guard arms a veto keyed to that
 * datagram's header. Ipv4L3Protocol builds the error at once, and
 * Icmpv4L4Protocol asks this node's routing protocol for its route: the guard
 * answers "no route" for exactly the Destination Unreachable that embeds the
 * armed header, so Icmpv4L4Protocol drops it unsent. Any other RouteOutput
 * disarms the veto and is forwarded. Ipv4L3Protocol's drop traces
 * (DROP_ROUTE_ERROR, DROP_NO_ROUTE) run as in stock ns-3.
 *
 * On a release without SendIcmpNoRoute (3.48 and earlier) there is no error
 * to stop: a veto is armed and disarmed unused, and the run is byte-identical
 * to the unguarded one. The guard draws no random number.
 *
 * The protocol class itself is defined in the .cc (as the #510 refusal
 * counter is): its RouteInput signature changed across ns-3 releases, and the
 * compile definition that selects it is scoped to this module.
 */

#include "ns3/ipv4-routing-protocol.h"
#include "ns3/ptr.h"

#include <array>
#include <cstdint>
#include <memory>

namespace ns3 {
namespace bench {

/// What the guards made from one helper counted, summed over their nodes.
struct IcmpErrorGuardCounters {
    /// Why !3101 would not send the no-route error (the first rule that matches).
    enum Reason : uint8_t {
        DEST_NOT_UNICAST = 0,
        FRAGMENT,
        SOURCE_NOT_HOST,
        SOURCE_OWN,
        ICMP_ERROR,
        N_REASONS
    };
    static const char* ReasonName(uint32_t reason);

    uint64_t noRoute = 0;                    ///< failures where stock 3.49 sends an error
    uint64_t allowed = 0;                    ///< of these, errors !3101 sends too
    std::array<uint64_t, N_REASONS> armed{};   ///< vetoes armed, by reason
    std::array<uint64_t, N_REASONS> vetoed{};  ///< errors stopped, by reason
    uint64_t unmatched = 0;  ///< vetoes no error met (every one before 3.49)

    uint64_t Vetoed() const;
};

/// Wraps `inner` in a guard that adds to `counters` (may be null).
Ptr<Ipv4RoutingProtocol> CreateIcmpErrorGuard(Ptr<Ipv4RoutingProtocol> inner,
                                              std::shared_ptr<IcmpErrorGuardCounters> counters);

}  // namespace bench
}  // namespace ns3

#endif  // ROUTING_BENCH_ICMP_ERROR_GUARD_ROUTING_H
