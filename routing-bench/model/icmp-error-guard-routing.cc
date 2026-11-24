// Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
// SPDX-License-Identifier: GPL-2.0-only

#include "icmp-error-guard-routing.h"

#include "ns3/abort.h"
#include "ns3/boolean.h"
#include "ns3/icmpv4.h"
#include "ns3/ip-l4-protocol.h"
#include "ns3/ipv4-header.h"
#include "ns3/ipv4.h"
#include "ns3/log.h"
#include "ns3/packet.h"

// RouteInput callbacks went from by-value to const-reference in ns-3.37.
#ifdef ROUTING_BENCH_NS3_ROUTEINPUT_BYVALUE
#define BENCH_RI_CB(T) T
#else
#define BENCH_RI_CB(T) const T&
#endif

namespace ns3 {
namespace bench {

NS_LOG_COMPONENT_DEFINE("IcmpErrorGuardRouting");

namespace {

constexpr uint8_t kIcmpProtocol = 1;

/// ICMP types that are error messages (RFC 792; RFC 1122 sec. 3.2.2).
bool IsIcmpErrorType(uint8_t type) {
    switch (type) {
    case 3:   // Destination Unreachable
    case 4:   // Source Quench
    case 5:   // Redirect
    case 11:  // Time Exceeded
    case 12:  // Parameter Problem
        return true;
    default:
        return false;
    }
}

}  // namespace

const char* IcmpErrorGuardCounters::ReasonName(uint32_t reason) {
    static const char* const names[] = {"dest-not-unicast", "fragment", "source-not-host",
                                        "source-own", "icmp-error"};
    return reason < N_REASONS ? names[reason] : "allowed";
}

uint64_t IcmpErrorGuardCounters::Vetoed() const {
    uint64_t n = 0;
    for (uint64_t v : vetoed) n += v;
    return n;
}

class IcmpErrorGuardRouting : public Ipv4RoutingProtocol {
public:
    using Reason = IcmpErrorGuardCounters::Reason;

    static TypeId GetTypeId();

    void Wrap(Ptr<Ipv4RoutingProtocol> inner, std::shared_ptr<IcmpErrorGuardCounters> counters) {
        NS_ABORT_MSG_IF(!inner, "IcmpErrorGuardRouting needs an inner routing protocol");
        m_inner = inner;
        m_counters = counters ? counters : std::make_shared<IcmpErrorGuardCounters>();
    }

    Ptr<Ipv4Route> RouteOutput(Ptr<Packet> p, const Ipv4Header& header, Ptr<NetDevice> oif,
                               Socket::SocketErrno& sockerr) override;
    bool RouteInput(Ptr<const Packet> p, const Ipv4Header& header, Ptr<const NetDevice> idev,
                    BENCH_RI_CB(UnicastForwardCallback) ucb,
                    BENCH_RI_CB(MulticastForwardCallback) mcb,
                    BENCH_RI_CB(LocalDeliverCallback) lcb,
                    BENCH_RI_CB(ErrorCallback) ecb) override;
    void NotifyInterfaceUp(uint32_t i) override { m_inner->NotifyInterfaceUp(i); }
    void NotifyInterfaceDown(uint32_t i) override { m_inner->NotifyInterfaceDown(i); }
    void NotifyAddAddress(uint32_t i, Ipv4InterfaceAddress a) override {
        m_inner->NotifyAddAddress(i, a);
    }
    void NotifyRemoveAddress(uint32_t i, Ipv4InterfaceAddress a) override {
        m_inner->NotifyRemoveAddress(i, a);
    }
    void SetIpv4(Ptr<Ipv4> ipv4) override {
        m_ipv4 = ipv4;
        m_inner->SetIpv4(ipv4);
    }
    void PrintRoutingTable(Ptr<OutputStreamWrapper> stream,
                           Time::Unit unit = Time::S) const override {
        m_inner->PrintRoutingTable(stream, unit);
    }

protected:
    void DoInitialize() override {
        m_inner->Initialize();
        Ipv4RoutingProtocol::DoInitialize();
    }
    void DoDispose() override {
        // Only drop the reference, as Ipv4L3Protocol does with its own routing
        // protocol: AodvHelper aggregates AODV to the node, and Dispose() on an
        // aggregated object disposes the whole aggregate, Ipv4L3Protocol (the
        // caller) included. A protocol that is not aggregated goes with its
        // last reference.
        m_inner = nullptr;
        m_ipv4 = nullptr;
        Ipv4RoutingProtocol::DoDispose();
    }

private:
    bool StockWouldSend(const Ipv4Header& header) const;
    bool NodeUnicast(Ipv4Address a) const;
    Reason Classify(const Ipv4Header& header, Ptr<const Packet> p) const;
    void OnNoRoute(const Ipv4Header& header, Ptr<const Packet> p);
    bool MatchesArmed(Ptr<const Packet> p, const Ipv4Header& header) const;
    void Disarm();

    Ptr<Ipv4RoutingProtocol> m_inner;
    Ptr<Ipv4> m_ipv4;
    std::shared_ptr<IcmpErrorGuardCounters> m_counters;
    bool m_armed = false;
    Reason m_armedReason = IcmpErrorGuardCounters::N_REASONS;
    Ipv4Header m_armedHeader;
};

NS_OBJECT_ENSURE_REGISTERED(IcmpErrorGuardRouting);

TypeId IcmpErrorGuardRouting::GetTypeId() {
    static TypeId tid = TypeId("ns3::bench::IcmpErrorGuardRouting")
                            .SetParent<Ipv4RoutingProtocol>()
                            .SetGroupName("RoutingBench")
                            .AddConstructor<IcmpErrorGuardRouting>();
    return tid;
}

// ns-3.49's SendIcmpNoRoute sends when the node forwards, the destination is
// neither multicast nor the limited broadcast, and an ICMP layer exists.
bool IcmpErrorGuardRouting::StockWouldSend(const Ipv4Header& header) const {
    BooleanValue fwd;
    m_ipv4->GetAttribute("IpForward", fwd);
    const Ipv4Address dst = header.GetDestination();
    return fwd.Get() && !dst.IsMulticast() && dst != Ipv4Address::GetBroadcast() &&
           m_ipv4->GetProtocol(kIcmpProtocol);
}

// Ipv4L3Protocol::IsUnicast: not broadcast, multicast, or the subnet-directed
// broadcast of one of this node's addresses.
bool IcmpErrorGuardRouting::NodeUnicast(Ipv4Address a) const {
    if (a.IsBroadcast() || a.IsMulticast()) return false;
    for (uint32_t i = 0; i < m_ipv4->GetNInterfaces(); ++i) {
        for (uint32_t j = 0; j < m_ipv4->GetNAddresses(i); ++j) {
            if (a == m_ipv4->GetAddress(i, j).GetBroadcast()) return false;
        }
    }
    return true;
}

// nsnam/ns-3-dev!3101's SendIcmpNoRoute, rule for rule and in its order.
IcmpErrorGuardRouting::Reason IcmpErrorGuardRouting::Classify(const Ipv4Header& header,
                                                              Ptr<const Packet> p) const {
    if (!NodeUnicast(header.GetDestination())) return IcmpErrorGuardCounters::DEST_NOT_UNICAST;
    if (header.GetFragmentOffset() != 0) return IcmpErrorGuardCounters::FRAGMENT;
    const Ipv4Address src = header.GetSource();
    const uint32_t first = src.Get() >> 24;
    if (first == 0 || first == 127 || first >= 224 || !NodeUnicast(src)) {
        return IcmpErrorGuardCounters::SOURCE_NOT_HOST;
    }
    for (uint32_t i = 0; i < m_ipv4->GetNInterfaces(); ++i) {
        for (uint32_t j = 0; j < m_ipv4->GetNAddresses(i); ++j) {
            if (m_ipv4->GetAddress(i, j).GetLocal() == src) {
                return IcmpErrorGuardCounters::SOURCE_OWN;
            }
        }
    }
    if (header.GetProtocol() == kIcmpProtocol) {
        if (!p || p->GetSize() == 0) return IcmpErrorGuardCounters::ICMP_ERROR;
        uint8_t type = 0;
        p->CopyData(&type, 1);
        if (IsIcmpErrorType(type)) return IcmpErrorGuardCounters::ICMP_ERROR;
    }
    return IcmpErrorGuardCounters::N_REASONS;
}

void IcmpErrorGuardRouting::OnNoRoute(const Ipv4Header& header, Ptr<const Packet> p) {
    if (!StockWouldSend(header)) return;  // stock sends nothing: nothing to guard
    ++m_counters->noRoute;
    const Reason r = Classify(header, p);
    if (r == IcmpErrorGuardCounters::N_REASONS) {
        ++m_counters->allowed;
        return;
    }
    Disarm();  // a veto still armed here never met its error
    ++m_counters->armed[r];
    m_armed = true;
    m_armedReason = r;
    m_armedHeader = header;
    NS_LOG_LOGIC("suppress the no-route ICMP for " << header << ": "
                                                    << IcmpErrorGuardCounters::ReasonName(r));
}

// Icmpv4L4Protocol::SendDestUnreach asks for a route with the Destination
// Unreachable body (unused/MTU, the offending IP header, 8 data bytes),
// destined to the offending datagram's source.
bool IcmpErrorGuardRouting::MatchesArmed(Ptr<const Packet> p, const Ipv4Header& header) const {
    if (!p || header.GetProtocol() != kIcmpProtocol ||
        header.GetDestination() != m_armedHeader.GetSource()) {
        return false;
    }
    if (p->GetSize() < 4 + m_armedHeader.GetSerializedSize() + 8) return false;
    Icmpv4DestinationUnreachable body;
    Ptr<Packet> copy = p->Copy();
    copy->RemoveHeader(body);
    const Ipv4Header& h = body.GetHeader();
    return h.GetSource() == m_armedHeader.GetSource() &&
           h.GetDestination() == m_armedHeader.GetDestination() &&
           h.GetIdentification() == m_armedHeader.GetIdentification() &&
           h.GetProtocol() == m_armedHeader.GetProtocol() &&
           h.GetFragmentOffset() == m_armedHeader.GetFragmentOffset() &&
           h.GetPayloadSize() == m_armedHeader.GetPayloadSize();
}

void IcmpErrorGuardRouting::Disarm() {
    if (m_armed) ++m_counters->unmatched;
    m_armed = false;
}

Ptr<Ipv4Route> IcmpErrorGuardRouting::RouteOutput(Ptr<Packet> p, const Ipv4Header& header,
                                                  Ptr<NetDevice> oif,
                                                  Socket::SocketErrno& sockerr) {
    if (m_armed) {
        // The error is built synchronously right after the decision, so the
        // next RouteOutput is it; anything else disarms the veto.
        if (MatchesArmed(p, header)) {
            m_armed = false;
            ++m_counters->vetoed[m_armedReason];
            sockerr = Socket::ERROR_NOROUTETOHOST;
            return nullptr;  // Icmpv4L4Protocol::SendMessage drops the message
        }
        Disarm();
    }
    return m_inner->RouteOutput(p, header, oif, sockerr);
}

bool IcmpErrorGuardRouting::RouteInput(Ptr<const Packet> p, const Ipv4Header& header,
                                       Ptr<const NetDevice> idev,
                                       BENCH_RI_CB(UnicastForwardCallback) ucb,
                                       BENCH_RI_CB(MulticastForwardCallback) mcb,
                                       BENCH_RI_CB(LocalDeliverCallback) lcb,
                                       BENCH_RI_CB(ErrorCallback) ecb) {
    ErrorCallback guarded = ecb;
    if (!ecb.IsNull()) {
        // Ipv4L3Protocol::RouteInputError: the drop trace, then (3.49) the error.
        guarded = ErrorCallback([this, ecb](Ptr<const Packet> pkt, const Ipv4Header& h,
                                            Socket::SocketErrno e) {
            OnNoRoute(h, pkt);
            ecb(pkt, h, e);
            Disarm();  // still armed: no error was built
        });
    }
    const bool routed = m_inner->RouteInput(p, header, idev, ucb, mcb, lcb, guarded);
    if (!routed) {
        // Ipv4L3Protocol::Receive records DROP_NO_ROUTE and calls
        // SendIcmpNoRoute, whose error reaches RouteOutput next.
        OnNoRoute(header, p);
    }
    return routed;
}

Ptr<Ipv4RoutingProtocol> CreateIcmpErrorGuard(Ptr<Ipv4RoutingProtocol> inner,
                                              std::shared_ptr<IcmpErrorGuardCounters> counters) {
    Ptr<IcmpErrorGuardRouting> guard = CreateObject<IcmpErrorGuardRouting>();
    guard->Wrap(inner, counters);
    return guard;
}

}  // namespace bench
}  // namespace ns3
