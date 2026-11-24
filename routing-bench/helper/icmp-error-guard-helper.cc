// Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
// SPDX-License-Identifier: GPL-2.0-only

#include "icmp-error-guard-helper.h"

#include "ns3/node.h"

namespace ns3 {
namespace bench {

IcmpErrorGuardHelper::IcmpErrorGuardHelper(const Ipv4RoutingHelper& inner)
    : m_inner(inner.Copy()), m_counters(std::make_shared<IcmpErrorGuardCounters>()) {}

IcmpErrorGuardHelper::IcmpErrorGuardHelper(const IcmpErrorGuardHelper& o)
    : Ipv4RoutingHelper(o), m_inner(o.m_inner->Copy()), m_counters(o.m_counters) {}

IcmpErrorGuardHelper* IcmpErrorGuardHelper::Copy() const {
    return new IcmpErrorGuardHelper(*this);
}

Ptr<Ipv4RoutingProtocol> IcmpErrorGuardHelper::Create(Ptr<Node> node) const {
    return CreateIcmpErrorGuard(m_inner->Create(node), m_counters);
}

}  // namespace bench
}  // namespace ns3
