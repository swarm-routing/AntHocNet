// Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
// SPDX-License-Identifier: GPL-2.0-only

#ifndef ROUTING_BENCH_ICMP_ERROR_GUARD_HELPER_H
#define ROUTING_BENCH_ICMP_ERROR_GUARD_HELPER_H

/**
 * NS3-001: installs the ICMP no-route guard (model/icmp-error-guard-routing.h,
 * the rules of nsnam/ns-3-dev!3101) around whatever routing protocol another
 * Ipv4RoutingHelper creates. Any helper works; the inner helper keeps its own
 * conventions (AodvHelper still aggregates AODV to the node).
 *
 *   InternetStackHelper internet;
 *   internet.SetRoutingHelper(IcmpErrorGuardHelper(AodvHelper()));
 *
 * Ipv4::GetRoutingProtocol() is then the guard, so a helper's AssignStreams
 * that casts it (AodvHelper's) finds nothing: assign the protocol's streams
 * through the object aggregated to the node instead (the aodv arm does,
 * helper/stock-arms.cc).
 */

#include "ns3/icmp-error-guard-routing.h"
#include "ns3/ipv4-routing-helper.h"

#include <memory>

namespace ns3 {
namespace bench {

class IcmpErrorGuardHelper : public Ipv4RoutingHelper {
public:
    explicit IcmpErrorGuardHelper(const Ipv4RoutingHelper& inner);
    IcmpErrorGuardHelper(const IcmpErrorGuardHelper& o);
    IcmpErrorGuardHelper& operator=(const IcmpErrorGuardHelper&) = delete;

    IcmpErrorGuardHelper* Copy() const override;
    Ptr<Ipv4RoutingProtocol> Create(Ptr<Node> node) const override;

    /// The guards' counters, summed over every node this helper (or a copy) installed.
    const IcmpErrorGuardCounters& Counters() const { return *m_counters; }

private:
    std::unique_ptr<Ipv4RoutingHelper> m_inner;
    std::shared_ptr<IcmpErrorGuardCounters> m_counters;  // shared by every Copy()
};

}  // namespace bench
}  // namespace ns3

#endif  // ROUTING_BENCH_ICMP_ERROR_GUARD_HELPER_H
