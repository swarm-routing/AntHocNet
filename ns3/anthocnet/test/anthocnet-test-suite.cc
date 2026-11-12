// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2026 Daniel Henrique Joppi

/*
 * NS-3 test suite for AntHocNet:
 *   1. AntHeader serialize/deserialize round-trip (the riskiest adapter seam).
 *   2. A deterministic multi-hop delivery test over SimpleNetDevices (no wifi
 *      randomness): a 3-node line 0-1-2 with the 0<->2 link black-listed, so a
 *      packet from 0 to 2 must be routed through 1. This guards the actual
 *      routing path (hello, neighbour learning, reactive discovery, forwarding
 *      and delivery), not just packet encoding.
 */
#include "ns3/test.h"
#include "ns3/packet.h"
#include "ns3/simulator.h"
#include "ns3/rng-seed-manager.h"
#include "ns3/node-container.h"
#include "ns3/simple-net-device.h"
#include "ns3/simple-channel.h"
#include "ns3/simple-net-device-helper.h"
#include "ns3/internet-stack-helper.h"
#include "ns3/ipv4-address-helper.h"
#include "ns3/socket.h"
#include "ns3/udp-socket-factory.h"
#include "ns3/inet-socket-address.h"

#include "ns3/wifi-helper.h"
#include "ns3/yans-wifi-helper.h"
#include "ns3/wifi-mac-helper.h"
#include "ns3/mobility-helper.h"
#include "ns3/mobility-model.h"
#include "ns3/constant-position-mobility-model.h"
#include "ns3/position-allocator.h"
#include "ns3/vector.h"
#include "ns3/double.h"
#include "ns3/data-rate.h"
#include "ns3/ipv4.h"
#include "ns3/ipv4-l3-protocol.h"
#include "ns3/uinteger.h"
#include "ns3/boolean.h"
#include "ns3/integer.h"
#include "ns3/nstime.h"

#include <sstream>
#include <string>
#include <vector>

#include "ns3/anthocnet-packet.h"
#include "anthocnet/core/ant_message_codec.h"
#include "anthocnet/core/config.h"
#include "ns3/anthocnet-helper.h"
#include "ns3/anthocnet-routing-protocol.h"
#include "ns3/anthocnet-rqueue.h"

using namespace ns3;
using ::anthocnet::core::AntDirection;
using ::anthocnet::core::AntMessage;
using ::anthocnet::core::AntType;

class AntHeaderRoundTripTestCase : public TestCase {
public:
    AntHeaderRoundTripTestCase() : TestCase("AntHeader serialize/deserialize round-trip") {}

    void DoRun() override {
        AntMessage m;
        m.type = AntType::Proactive;
        m.direction = AntDirection::Down;
        m.src = 3;
        m.dst = 17;
        m.seqNum = 70000;  // > 16 bits
        m.timeStart = 1.25;
        m.lifeAnt = 2.0;
        m.broadcastBudget = 2;
        m.visited = {{3, 0.0}, {4, 0.01}, {7, 0.02}};
        m.history = {{17, 0.05}};
        m.helloDests = {{8, 1.0}, {9, 0.5}};

        Ptr<Packet> packet = Create<Packet>();
        ns3::anthocnet::AntHeader out(m);
        packet->AddHeader(out);

        ns3::anthocnet::AntHeader in;
        packet->RemoveHeader(in);
        const AntMessage& r = in.Message();

        NS_TEST_ASSERT_MSG_EQ(static_cast<int>(r.type), static_cast<int>(AntType::Proactive),
                              "type");
        NS_TEST_ASSERT_MSG_EQ(static_cast<int>(r.direction), static_cast<int>(AntDirection::Down),
                              "direction");
        NS_TEST_ASSERT_MSG_EQ(r.src, 3, "src");
        NS_TEST_ASSERT_MSG_EQ(r.dst, 17, "dst");
        NS_TEST_ASSERT_MSG_EQ(r.seqNum, 70000u, "seqNum");
        NS_TEST_ASSERT_MSG_EQ_TOL(r.timeStart, 1.25, 1e-9, "timeStart");
        NS_TEST_ASSERT_MSG_EQ(r.broadcastBudget, 2, "broadcastBudget");
        NS_TEST_ASSERT_MSG_EQ(r.visited.size(), 3u, "visited size");
        NS_TEST_ASSERT_MSG_EQ(r.visited[2].node, 7, "visited node");
        NS_TEST_ASSERT_MSG_EQ_TOL(r.visited[1].time, 0.01, 1e-9, "visited time");
        NS_TEST_ASSERT_MSG_EQ(r.history.size(), 1u, "history size");
        NS_TEST_ASSERT_MSG_EQ(r.helloDests.size(), 2u, "hello size");
        NS_TEST_ASSERT_MSG_EQ(r.helloDests[1].node, 9, "hello node");
        NS_TEST_ASSERT_MSG_EQ_TOL(r.helloDests[1].pheromone, 0.5, 1e-9, "hello ph");
    }
};

// ADR-0025 (#573): the hello's optional kinematics section survives the ns-3
// header, a hello without it decodes as absent, and the header length moves
// by exactly the section's 25 bytes.
class AntHeaderKinematicsTestCase : public TestCase {
public:
    AntHeaderKinematicsTestCase()
        : TestCase("AntHeader carries the optional hello kinematics section") {}

    void DoRun() override {
        AntMessage m;
        m.type = AntType::Hello;
        m.direction = ::anthocnet::core::AntDirection::Up;
        m.src = 4;
        m.helloDests = {{8, 1.0}};
        const uint32_t plain = ns3::anthocnet::AntHeader(m).GetSerializedSize();

        m.hasKinematics = true;
        m.kinematics = ::anthocnet::core::Kinematics{1250.5, -300.25, 0.0, 20.0, -7.5, 0.0};
        ns3::anthocnet::AntHeader out(m);
        NS_TEST_ASSERT_MSG_EQ(out.GetSerializedSize(),
                              plain + ::anthocnet::core::codec::kKinematicsSize, "section size");
        Ptr<Packet> packet = Create<Packet>();
        packet->AddHeader(out);
        ns3::anthocnet::AntHeader in;
        packet->RemoveHeader(in);
        const AntMessage& r = in.Message();
        NS_TEST_ASSERT_MSG_EQ(r.hasKinematics, true, "section present");
        NS_TEST_ASSERT_MSG_EQ_TOL(r.kinematics.x, 1250.5, 1e-3, "x");
        NS_TEST_ASSERT_MSG_EQ_TOL(r.kinematics.vy, -7.5, 1e-6, "vy");
        NS_TEST_ASSERT_MSG_EQ(r.helloDests.size(), 1u, "adverts intact");

        m.hasKinematics = false;
        Ptr<Packet> bare = Create<Packet>();
        bare->AddHeader(ns3::anthocnet::AntHeader(m));
        ns3::anthocnet::AntHeader in2;
        bare->RemoveHeader(in2);
        NS_TEST_ASSERT_MSG_EQ(in2.Message().hasKinematics, false, "absent, not the origin");
    }
};

// Deterministic multi-hop delivery over SimpleNetDevices.
class AntHocNetDeliveryTestCase : public TestCase {
public:
    AntHocNetDeliveryTestCase() : TestCase("Multi-hop delivery over a 3-node line"), m_rxBytes(0) {}

    void DoRun() override {
        RngSeedManager::SetSeed(1);
        RngSeedManager::SetRun(1);

        NodeContainer nodes;
        nodes.Create(3);

        // Shared SimpleChannel; black-list the 0<->2 link so 0 and 2 are not
        // direct neighbours and traffic must traverse node 1.
        Ptr<SimpleChannel> channel = CreateObject<SimpleChannel>();
        SimpleNetDeviceHelper devHelper;
        NetDeviceContainer devices = devHelper.Install(nodes, channel);
        Ptr<SimpleNetDevice> d0 = DynamicCast<SimpleNetDevice>(devices.Get(0));
        Ptr<SimpleNetDevice> d2 = DynamicCast<SimpleNetDevice>(devices.Get(2));
        channel->BlackList(d0, d2);
        channel->BlackList(d2, d0);

        AntHocNetHelper anthocnet;
        InternetStackHelper internet;
        internet.SetRoutingHelper(anthocnet);
        internet.Install(nodes);

        Ipv4AddressHelper address;
        address.SetBase("10.1.0.0", "255.255.255.0");
        Ipv4InterfaceContainer ifs = address.Assign(devices);

        const uint16_t port = 9;

        // Receiver on node 2 counts bytes.
        Ptr<Socket> rx = Socket::CreateSocket(nodes.Get(2), UdpSocketFactory::GetTypeId());
        rx->Bind(InetSocketAddress(Ipv4Address::GetAny(), port));
        rx->SetRecvCallback(MakeCallback(&AntHocNetDeliveryTestCase::Receive, this));

        // Sender on node 0 -> node 2, after routes have a chance to form.
        Ptr<Socket> tx = Socket::CreateSocket(nodes.Get(0), UdpSocketFactory::GetTypeId());
        tx->Connect(InetSocketAddress(ifs.GetAddress(2), port));
        for (double t = 10.0; t < 18.0; t += 0.5) {
            Simulator::Schedule(Seconds(t), &AntHocNetDeliveryTestCase::Send, this, tx);
        }

        Simulator::Stop(Seconds(20.0));
        Simulator::Run();
        Simulator::Destroy();

        NS_TEST_ASSERT_MSG_GT(m_rxBytes, 0u, "node 2 received no data over the 2-hop path");
    }

private:
    void Send(Ptr<Socket> s) { s->Send(Create<Packet>(64)); }
    void Receive(Ptr<Socket> s) {
        Ptr<Packet> p;
        while ((p = s->Recv())) m_rxBytes += p->GetSize();
    }
    uint32_t m_rxBytes;
};

// Issue #203 — delivery across a node with MORE THAN ONE interface.
//
// Same 3-node line as above, but built from two separate channels on two
// separate subnets, so the middle node has two interfaces:
//
//     0 (10.1.1.1) --- (10.1.1.2) 1 (10.1.2.1) --- (10.1.2.2) 2
//
// This is the satellite/ISL shape (#192, #194) and it fails before #203: the
// data path derived its output device from m_socketAddresses.begin(), so node 1
// forwarded packets for 10.1.2.2 back out of its 10.1.1.0/24 device, and
// backward ants unicast to a canonical address that is not on the link they
// were sent over.
class MultiInterfaceDeliveryTestCase : public TestCase {
public:
    MultiInterfaceDeliveryTestCase()
        : TestCase("Multi-hop delivery through a multi-interface node (#203)"), m_rxBytes(0) {}

    void DoRun() override {
        RngSeedManager::SetSeed(1);
        RngSeedManager::SetRun(1);

        NodeContainer nodes;
        nodes.Create(3);

        // Two point-to-point-style links, each its own channel: 0<->1 and 1<->2.
        // Node 1 therefore holds one device (and one subnet) per link, and 0 and
        // 2 share no medium at all.
        NodeContainer leftPair(nodes.Get(0), nodes.Get(1));
        NodeContainer rightPair(nodes.Get(1), nodes.Get(2));
        SimpleNetDeviceHelper devHelper;
        NetDeviceContainer leftDevs = devHelper.Install(leftPair, CreateObject<SimpleChannel>());
        NetDeviceContainer rightDevs = devHelper.Install(rightPair, CreateObject<SimpleChannel>());

        AntHocNetHelper anthocnet;
        InternetStackHelper internet;
        internet.SetRoutingHelper(anthocnet);
        internet.Install(nodes);

        Ipv4AddressHelper address;
        address.SetBase("10.1.1.0", "255.255.255.0");
        Ipv4InterfaceContainer leftIfs = address.Assign(leftDevs);
        address.SetBase("10.1.2.0", "255.255.255.0");
        Ipv4InterfaceContainer rightIfs = address.Assign(rightDevs);

        const uint16_t port = 9;

        Ptr<Socket> rx = Socket::CreateSocket(nodes.Get(2), UdpSocketFactory::GetTypeId());
        rx->Bind(InetSocketAddress(Ipv4Address::GetAny(), port));
        rx->SetRecvCallback(MakeCallback(&MultiInterfaceDeliveryTestCase::Receive, this));

        // Node 0 -> node 2's address, which lives on the far subnet and is only
        // reachable through node 1's second interface.
        Ptr<Socket> tx = Socket::CreateSocket(nodes.Get(0), UdpSocketFactory::GetTypeId());
        tx->Connect(InetSocketAddress(rightIfs.GetAddress(1), port));
        for (double t = 10.0; t < 18.0; t += 0.5) {
            Simulator::Schedule(Seconds(t), &MultiInterfaceDeliveryTestCase::Send, this, tx);
        }

        Simulator::Stop(Seconds(20.0));
        Simulator::Run();
        Simulator::Destroy();

        NS_TEST_ASSERT_MSG_GT(m_rxBytes, 0u,
                              "node 2 received no data across the multi-interface relay");
    }

private:
    void Send(Ptr<Socket> s) { s->Send(Create<Packet>(64)); }
    void Receive(Ptr<Socket> s) {
        Ptr<Packet> p;
        while ((p = s->Recv())) m_rxBytes += p->GetSize();
    }
    uint32_t m_rxBytes;
};

// #204 — hellos on point-to-point links every N ticks, liveness from the up
// interface in between.
//
// The #203 relay with both links in point-to-point mode. Run once sending a
// hello every tick (P2pHelloEvery=1) and once every 5th tick: delivery must
// hold in both (the peer map #203 needs is learned from the hellos still sent,
// and no neighbour expires between them), and the relay must transmit fewer
// packets with the throttle.
class P2pHelloThrottleTestCase : public TestCase {
public:
    P2pHelloThrottleTestCase()
        : TestCase("Point-to-point hello throttle keeps delivery, sends less (#204)") {}

    void DoRun() override {
        uint32_t rx1 = 0, rx5 = 0, tx1 = 0, tx5 = 0;
        RunOnce(1, rx1, tx1);
        RunOnce(5, rx5, tx5);
        NS_TEST_ASSERT_MSG_GT(rx1, 0u, "no delivery with a hello every tick");
        NS_TEST_ASSERT_MSG_GT(rx5, 0u, "no delivery with a hello every 5th tick");
        NS_TEST_ASSERT_MSG_EQ(rx5, rx1, "the throttle lost data on a stable line");
        NS_TEST_ASSERT_MSG_LT(tx5, tx1, "the throttle did not reduce the relay's transmissions");
    }

private:
    void RunOnce(uint32_t every, uint32_t& rxBytes, uint32_t& relayTx) {
        RngSeedManager::SetSeed(1);
        RngSeedManager::SetRun(1);
        m_rx = 0;
        m_tx = 0;
        NodeContainer nodes;
        nodes.Create(3);
        SimpleNetDeviceHelper devHelper;
        devHelper.SetNetDevicePointToPointMode(true);
        NetDeviceContainer leftDevs = devHelper.Install(NodeContainer(nodes.Get(0), nodes.Get(1)),
                                                        CreateObject<SimpleChannel>());
        NetDeviceContainer rightDevs = devHelper.Install(NodeContainer(nodes.Get(1), nodes.Get(2)),
                                                         CreateObject<SimpleChannel>());
        AntHocNetHelper anthocnet;
        anthocnet.Set("P2pHelloEvery", UintegerValue(every));
        InternetStackHelper internet;
        internet.SetRoutingHelper(anthocnet);
        internet.Install(nodes);
        Ipv4AddressHelper address;
        address.SetBase("10.1.1.0", "255.255.255.0");
        address.Assign(leftDevs);
        address.SetBase("10.1.2.0", "255.255.255.0");
        Ipv4InterfaceContainer rightIfs = address.Assign(rightDevs);
        nodes.Get(1)->GetObject<Ipv4L3Protocol>()->TraceConnectWithoutContext(
            "Tx", MakeCallback(&P2pHelloThrottleTestCase::CountTx, this));

        const uint16_t port = 9;
        Ptr<Socket> rx = Socket::CreateSocket(nodes.Get(2), UdpSocketFactory::GetTypeId());
        rx->Bind(InetSocketAddress(Ipv4Address::GetAny(), port));
        rx->SetRecvCallback(MakeCallback(&P2pHelloThrottleTestCase::Receive, this));
        Ptr<Socket> tx = Socket::CreateSocket(nodes.Get(0), UdpSocketFactory::GetTypeId());
        tx->Connect(InetSocketAddress(rightIfs.GetAddress(1), port));
        for (double t = 10.0; t < 28.0; t += 0.5) {
            Simulator::Schedule(Seconds(t), &P2pHelloThrottleTestCase::Send, tx);
        }
        Simulator::Stop(Seconds(30.0));
        Simulator::Run();
        Simulator::Destroy();
        rxBytes = m_rx;
        relayTx = m_tx;
    }
    static void Send(Ptr<Socket> s) { s->Send(Create<Packet>(64)); }
    void Receive(Ptr<Socket> s) {
        Ptr<Packet> p;
        while ((p = s->Recv())) m_rx += p->GetSize();
    }
    void CountTx(Ptr<const Packet>, Ptr<Ipv4>, uint32_t) { ++m_tx; }
    uint32_t m_rx = 0;
    uint32_t m_tx = 0;
};

// #641 — StrongestNextHop, the read-only query --vizTrace draws routes with.
//
// On a three-node point-to-point line the only route from node 0 to node 2 is
// through node 1, so once data has flowed the strongest next hop node 0 holds
// toward node 2 must be node 1's address on their shared link. Before the
// protocol starts, the query answers 0.0.0.0 rather than guessing.
class StrongestNextHopTestCase : public TestCase {
public:
    StrongestNextHopTestCase()
        : TestCase("StrongestNextHop names the route the pheromone holds (#641)") {}

    void DoRun() override {
        using ns3::anthocnet::RoutingProtocol;
        RngSeedManager::SetSeed(1);
        RngSeedManager::SetRun(1);
        NodeContainer nodes;
        nodes.Create(3);
        SimpleNetDeviceHelper devHelper;
        devHelper.SetNetDevicePointToPointMode(true);
        NetDeviceContainer leftDevs = devHelper.Install(NodeContainer(nodes.Get(0), nodes.Get(1)),
                                                        CreateObject<SimpleChannel>());
        NetDeviceContainer rightDevs = devHelper.Install(NodeContainer(nodes.Get(1), nodes.Get(2)),
                                                         CreateObject<SimpleChannel>());
        AntHocNetHelper anthocnet;
        InternetStackHelper internet;
        internet.SetRoutingHelper(anthocnet);
        internet.Install(nodes);
        Ipv4AddressHelper address;
        address.SetBase("10.1.1.0", "255.255.255.0");
        Ipv4InterfaceContainer leftIfs = address.Assign(leftDevs);
        address.SetBase("10.1.2.0", "255.255.255.0");
        Ipv4InterfaceContainer rightIfs = address.Assign(rightDevs);
        Ptr<RoutingProtocol> rp0 =
            DynamicCast<RoutingProtocol>(nodes.Get(0)->GetObject<Ipv4>()->GetRoutingProtocol());
        NS_TEST_ASSERT_MSG_NE(rp0, nullptr, "node 0 runs AntHocNet");
        const Ipv4Address dest = rightIfs.GetAddress(1);
        const Ipv4Address relay = leftIfs.GetAddress(1);  // read before Destroy
        NS_TEST_ASSERT_MSG_EQ(rp0->StrongestNextHop(dest), Ipv4Address(),
                              "a query before the protocol starts answers 0.0.0.0");

        const uint16_t port = 9;
        Ptr<Socket> rx = Socket::CreateSocket(nodes.Get(2), UdpSocketFactory::GetTypeId());
        rx->Bind(InetSocketAddress(Ipv4Address::GetAny(), port));
        Ptr<Socket> tx = Socket::CreateSocket(nodes.Get(0), UdpSocketFactory::GetTypeId());
        tx->Connect(InetSocketAddress(dest, port));
        for (double t = 5.0; t < 15.0; t += 0.5) {
            Simulator::Schedule(Seconds(t), &StrongestNextHopTestCase::Send, tx);
        }
        Ipv4Address seen;
        Simulator::Schedule(Seconds(15.0), [&]() { seen = rp0->StrongestNextHop(dest); });
        Simulator::Stop(Seconds(16.0));
        Simulator::Run();
        Simulator::Destroy();
        NS_TEST_ASSERT_MSG_EQ(seen, relay, "node 0's strongest next hop toward node 2 is node 1");
    }

private:
    static void Send(Ptr<Socket> s) { s->Send(Create<Packet>(64)); }
};

// #206 — the per-next-hop congestion signal on non-wifi devices.
//
// The same multi-interface relay shape as the #203 test (one SimpleNetDevice
// per link, so node 1 has one transmit queue per neighbour), with the relay's
// egress toward node 2 slowed to 32 kb/s so a data burst backlogs that queue
// while the queue toward node 0 stays idle. The ILinkState surface must then
// DISCRIMINATE: a positive backlog and a sampled service time toward the
// loaded next hop, (near-)nothing toward the idle one, and an aggregate at
// least as large as any single queue. A node-wide signal cannot pass this —
// it would read the same value for both hops, which is exactly the silent
// failure #206 exists to prevent.
class PerHopCongestionSignalTestCase : public TestCase {
public:
    PerHopCongestionSignalTestCase()
        : TestCase("Per-next-hop congestion signal on non-wifi devices (#206)"),
          m_qLoaded(0),
          m_qIdle(0),
          m_qAggregate(0),
          m_tLoaded(0.0),
          m_tIdle(0.0) {}

    void DoRun() override {
        using ns3::anthocnet::RoutingProtocol;
        RngSeedManager::SetSeed(1);
        RngSeedManager::SetRun(1);

        NodeContainer nodes;
        nodes.Create(3);

        NodeContainer leftPair(nodes.Get(0), nodes.Get(1));
        NodeContainer rightPair(nodes.Get(1), nodes.Get(2));
        SimpleNetDeviceHelper devHelper;
        NetDeviceContainer leftDevs = devHelper.Install(leftPair, CreateObject<SimpleChannel>());
        NetDeviceContainer rightDevs = devHelper.Install(rightPair, CreateObject<SimpleChannel>());

        // Slow the relay's egress toward node 2. The left link keeps the
        // default infinite rate, so its queue drains instantly and stays idle.
        Ptr<SimpleNetDevice> relayRight = DynamicCast<SimpleNetDevice>(rightDevs.Get(0));
        relayRight->SetAttribute("DataRate", DataRateValue(DataRate("32kbps")));

        AntHocNetHelper anthocnet;
        InternetStackHelper internet;
        internet.SetRoutingHelper(anthocnet);
        internet.Install(nodes);

        Ipv4AddressHelper address;
        address.SetBase("10.1.1.0", "255.255.255.0");
        Ipv4InterfaceContainer leftIfs = address.Assign(leftDevs);
        address.SetBase("10.1.2.0", "255.255.255.0");
        Ipv4InterfaceContainer rightIfs = address.Assign(rightDevs);

        const uint16_t port = 9;
        Ptr<Socket> rx = Socket::CreateSocket(nodes.Get(2), UdpSocketFactory::GetTypeId());
        rx->Bind(InetSocketAddress(Ipv4Address::GetAny(), port));
        rx->SetRecvCallback(MakeCallback(&PerHopCongestionSignalTestCase::Drain, this));

        // A sustained 1000-byte burst from node 0: at 32 kb/s the relay drains
        // ~4 pkt/s, so sending 40/s from t=10 guarantees a deep backlog toward
        // node 2 at the probe time.
        Ptr<Socket> tx = Socket::CreateSocket(nodes.Get(0), UdpSocketFactory::GetTypeId());
        tx->Connect(InetSocketAddress(rightIfs.GetAddress(1), port));
        for (double t = 10.0; t < 14.0; t += 0.025) {
            Simulator::Schedule(Seconds(t), &PerHopCongestionSignalTestCase::Send, this, tx);
        }

        Simulator::Schedule(Seconds(13.0), &PerHopCongestionSignalTestCase::Probe, this,
                            nodes.Get(1), RoutingProtocol::ToCore(rightIfs.GetAddress(1)),
                            RoutingProtocol::ToCore(leftIfs.GetAddress(0)));

        Simulator::Stop(Seconds(15.0));
        Simulator::Run();
        Simulator::Destroy();

        NS_TEST_ASSERT_MSG_GT(m_qLoaded, 0, "no backlog read toward the loaded next hop");
        // The instant-drain queue can momentarily hold the packet being
        // serialised within a timestep; anything beyond that is a real backlog
        // and breaks the discrimination this test pins.
        NS_TEST_ASSERT_MSG_LT(m_qIdle, 2, "backlog read toward the idle next hop");
        NS_TEST_ASSERT_MSG_GT(m_qLoaded, m_qIdle, "signal does not discriminate between next hops");
        NS_TEST_ASSERT_MSG_GT_OR_EQ(m_qAggregate, m_qLoaded,
                                    "aggregate smaller than a single queue");
        // Service time toward the loaded hop: sampled (positive) and at the
        // serialisation scale — 1000 B at 32 kb/s is 0.25 s; ants are smaller.
        // A value near an ISL's propagation delay would mean the decomposition
        // folded propagation in, which it must not.
        NS_TEST_ASSERT_MSG_GT(m_tLoaded, 0.0, "no service-time sample toward the loaded next hop");
        NS_TEST_ASSERT_MSG_LT(m_tLoaded, 1.0, "service time off the serialisation scale");
        NS_TEST_ASSERT_MSG_EQ(m_tIdle, 0.0, "service-time sample on the never-backlogged link");
    }

private:
    void Send(Ptr<Socket> s) { s->Send(Create<Packet>(1000)); }
    void Drain(Ptr<Socket> s) {
        Ptr<Packet> p;
        while ((p = s->Recv())) {
        }
    }
    void Probe(Ptr<Node> relay, ::anthocnet::core::NodeAddress towardLoaded,
               ::anthocnet::core::NodeAddress towardIdle) {
        Ptr<ns3::anthocnet::RoutingProtocol> rp =
            relay->GetObject<ns3::anthocnet::RoutingProtocol>();
        NS_TEST_ASSERT_MSG_EQ((rp != nullptr), true, "relay has no AntHocNet protocol");
        if (!rp) return;
        m_qLoaded = rp->macQueueLength(towardLoaded);
        m_qIdle = rp->macQueueLength(towardIdle);
        m_qAggregate = rp->macQueueLength(::anthocnet::core::kInvalidAddress);
        m_tLoaded = rp->macServiceTime(towardLoaded);
        m_tIdle = rp->macServiceTime(towardIdle);
    }

    int m_qLoaded;
    int m_qIdle;
    int m_qAggregate;
    double m_tLoaded;
    double m_tIdle;
};

// B3 — address mapping never aliases the core's kInvalidAddress sentinel.
class AddressMappingTestCase : public TestCase {
public:
    AddressMappingTestCase() : TestCase("ToCore/ToIpv4 address mapping (B3)") {}

    void DoRun() override {
        using ns3::anthocnet::RoutingProtocol;
        const ::anthocnet::core::NodeAddress invalid = ::anthocnet::core::kInvalidAddress;

        // The limited broadcast must never collide with "no route" (-1).
        NS_TEST_ASSERT_MSG_NE(RoutingProtocol::ToCore(Ipv4Address("255.255.255.255")), invalid,
                              "broadcast aliases kInvalidAddress");

        // MSB-set / arbitrary unicast addresses round-trip without aliasing it.
        const char* addrs[] = {"128.0.0.1", "200.1.2.3", "10.1.0.5", "192.168.1.1"};
        for (const char* s : addrs) {
            Ipv4Address a(s);
            ::anthocnet::core::NodeAddress core = RoutingProtocol::ToCore(a);
            NS_TEST_ASSERT_MSG_NE(core, invalid, "unicast aliases kInvalidAddress");
            NS_TEST_ASSERT_MSG_EQ(RoutingProtocol::ToIpv4(core), a, "round-trip");
        }
    }
};

// #918: the pending queue's in-place DequeueAll/Purge. Every backward ant at
// every node flushes its origin, so a flush for a destination with nothing
// queued must leave the queue exactly as it was; a real flush hands out that
// destination's packets in queue order and keeps the others' order; an aged
// entry is still purged first, through its error callback.
class RequestQueueInPlaceTestCase : public TestCase {
public:
    RequestQueueInPlaceTestCase()
        : TestCase("Pending queue: in-place, order-preserving flush (#918)") {}

    void DoRun() override {
        using ns3::anthocnet::QueueEntry;
        using ns3::anthocnet::RequestQueue;
        RequestQueue q(64, Seconds(10));
        const char* dsts[] = {"10.0.0.1", "10.0.0.2", "10.0.0.1", "10.0.0.3", "10.0.0.2"};
        std::vector<uint64_t> uids;
        for (const char* d : dsts) {
            QueueEntry e;
            e.packet = Create<Packet>(8);
            e.header.SetDestination(Ipv4Address(d));
            uids.push_back(e.packet->GetUid());
            q.Enqueue(e);
        }

        // No match: nothing out, nothing moved.
        std::vector<QueueEntry> out;
        q.DequeueAll(Ipv4Address("10.0.0.9"), out);
        NS_TEST_ASSERT_MSG_EQ(out.size(), 0u, "a flush with no match dequeues nothing");
        NS_TEST_ASSERT_MSG_EQ(q.Size(), 5u, "a flush with no match keeps every entry");

        // A match: that destination's packets, in queue order.
        q.DequeueAll(Ipv4Address("10.0.0.1"), out);
        NS_TEST_ASSERT_MSG_EQ(out.size(), 2u, "both packets for 10.0.0.1");
        NS_TEST_ASSERT_MSG_EQ(out[0].packet->GetUid(), uids[0], "queue order kept (1st)");
        NS_TEST_ASSERT_MSG_EQ(out[1].packet->GetUid(), uids[2], "queue order kept (2nd)");
        NS_TEST_ASSERT_MSG_EQ(q.Size(), 3u, "the other three stay");

        // The rest kept their order: drain them one destination at a time.
        std::vector<QueueEntry> rest;
        q.DequeueAll(Ipv4Address("10.0.0.2"), rest);
        q.DequeueAll(Ipv4Address("10.0.0.3"), rest);
        NS_TEST_ASSERT_MSG_EQ(rest.size(), 3u, "three left");
        NS_TEST_ASSERT_MSG_EQ(rest[0].packet->GetUid(), uids[1], "kept order (10.0.0.2 first)");
        NS_TEST_ASSERT_MSG_EQ(rest[1].packet->GetUid(), uids[4], "kept order (10.0.0.2 second)");
        NS_TEST_ASSERT_MSG_EQ(rest[2].packet->GetUid(), uids[3], "kept order (10.0.0.3)");
        NS_TEST_ASSERT_MSG_EQ(q.Size(), 0u, "empty");

        // Aging: an expired entry leaves through its error callback on the
        // next flush, even one that matches nothing; a live one stays.
        m_errors = 0;
        RequestQueue aging(64, Seconds(1));
        QueueEntry old;
        old.packet = Create<Packet>(8);
        old.header.SetDestination(Ipv4Address("10.0.0.1"));
        old.ecb = MakeCallback(&RequestQueueInPlaceTestCase::OnError, this);
        aging.Enqueue(old);
        Simulator::Schedule(Seconds(0.5), [&aging]() {
            QueueEntry young;
            young.packet = Create<Packet>(8);
            young.header.SetDestination(Ipv4Address("10.0.0.2"));
            aging.Enqueue(young);
        });
        std::vector<QueueEntry> none;
        Simulator::Schedule(Seconds(1.2),
                            [&aging, &none]() { aging.DequeueAll(Ipv4Address("10.0.0.9"), none); });
        Simulator::Run();
        Simulator::Destroy();
        NS_TEST_ASSERT_MSG_EQ(none.size(), 0u, "nothing matched");
        NS_TEST_ASSERT_MSG_EQ(m_errors, 1u, "the aged entry was purged through its ecb");
        NS_TEST_ASSERT_MSG_EQ(aging.Stats().droppedCount[ns3::anthocnet::HOLD_SETUP], 1u,
                              "and counted as dropped");
    }

private:
    void OnError(Ptr<const Packet>, const Ipv4Header&, Socket::SocketErrno) { ++m_errors; }
    uint32_t m_errors = 0;
};

// Repair ant fires on a MAC transmit-failure (ADR-0008 detector D, issue #19).
// Two adhoc-wifi nodes within range establish a data session; the receiver is
// then moved out of range so the sender's unicasts fail after the retry limit.
// The WifiMac "DroppedMpdu" trace must drive a bounded repair ant, observed via
// the protocol's "Tx" trace (type == Repair). SimpleNetDevice has no such
// failure model, so this case requires real wifi.
class RepairAntOnLinkBreakTestCase : public TestCase {
public:
    RepairAntOnLinkBreakTestCase()
        : TestCase("Repair ant fires on MAC tx-failure (detector D)"), m_repairAnts(0) {}

    void DoRun() override {
        RngSeedManager::SetSeed(1);
        RngSeedManager::SetRun(1);

        NodeContainer nodes;
        nodes.Create(2);

        // Disk connectivity model so the link is purely a function of distance.
        WifiHelper wifi;
        wifi.SetStandard(WIFI_STANDARD_80211b);
        YansWifiChannelHelper channel;
        channel.SetPropagationDelay("ns3::ConstantSpeedPropagationDelayModel");
        channel.AddPropagationLoss("ns3::RangePropagationLossModel", "MaxRange",
                                   DoubleValue(120.0));
        YansWifiPhyHelper phy;
        phy.SetChannel(channel.Create());
        WifiMacHelper mac;
        mac.SetType("ns3::AdhocWifiMac");
        NetDeviceContainer devices = wifi.Install(phy, mac, nodes);

        // Node 0 at the origin, node 1 in range (60 m < 120 m).
        MobilityHelper mobility;
        Ptr<ListPositionAllocator> pos = CreateObject<ListPositionAllocator>();
        pos->Add(Vector(0.0, 0.0, 0.0));
        pos->Add(Vector(60.0, 0.0, 0.0));
        mobility.SetPositionAllocator(pos);
        mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
        mobility.Install(nodes);

        AntHocNetHelper anthocnet;
        InternetStackHelper internet;
        internet.SetRoutingHelper(anthocnet);
        internet.Install(nodes);

        Ipv4AddressHelper address;
        address.SetBase("10.1.0.0", "255.255.255.0");
        Ipv4InterfaceContainer ifs = address.Assign(devices);

        // Count repair ants put on the medium across both nodes (item-15 Tx trace).
        for (uint32_t i = 0; i < nodes.GetN(); ++i) {
            Ptr<Ipv4> ipv4 = nodes.Get(i)->GetObject<Ipv4>();
            Ptr<ns3::anthocnet::RoutingProtocol> rp =
                DynamicCast<ns3::anthocnet::RoutingProtocol>(ipv4->GetRoutingProtocol());
            if (rp) {
                rp->TraceConnectWithoutContext(
                    "Tx", MakeCallback(&RepairAntOnLinkBreakTestCase::CountTx, this));
            }
        }

        const uint16_t port = 9;
        Ptr<Socket> rx = Socket::CreateSocket(nodes.Get(1), UdpSocketFactory::GetTypeId());
        rx->Bind(InetSocketAddress(Ipv4Address::GetAny(), port));

        Ptr<Socket> tx = Socket::CreateSocket(nodes.Get(0), UdpSocketFactory::GetTypeId());
        tx->Connect(InetSocketAddress(ifs.GetAddress(1), port));
        // Send densely so a live session exists before the break and, after it,
        // detector D reaches Config::txFailureThreshold consecutive MAC drops
        // (the #19 debounce) well before detector A's hello-timeout (~2 s) would
        // evict the moved node — otherwise A removes it first and D never fires
        // the repair. The simulation is deterministic (fixed seed/run), so this
        // ordering is stable across ns-3 versions.
        for (double t = 5.0; t < 28.0; t += 0.1) {
            Simulator::Schedule(Seconds(t), &RepairAntOnLinkBreakTestCase::Send, this, tx);
        }

        // Break the link at t=15 s: move node 1 far out of range.
        Simulator::Schedule(Seconds(15.0), &RepairAntOnLinkBreakTestCase::MoveAway, this,
                            nodes.Get(1));

        Simulator::Stop(Seconds(30.0));
        Simulator::Run();
        Simulator::Destroy();

        NS_TEST_ASSERT_MSG_GT(m_repairAnts, 0u,
                              "no repair ant was sent after the induced link break");
    }

private:
    void Send(Ptr<Socket> s) { s->Send(Create<Packet>(64)); }
    void MoveAway(Ptr<Node> n) {
        n->GetObject<MobilityModel>()->SetPosition(Vector(5000.0, 0.0, 0.0));
    }
    void CountTx(uint8_t type, uint8_t /*dir*/, bool /*broadcast*/) {
        if (type == static_cast<uint8_t>(AntType::Repair)) ++m_repairAnts;
    }
    uint32_t m_repairAnts;
};

// #917: swarm-core's router-observer trace sources (ControlTx, ControlRx,
// DataDecision, StateSize) report every event, and connecting them changes
// nothing (org rule 5): the same seed gives the same Tx/RouteChanged event
// stream and the same delivered bytes with them connected and without.
class ObserverTracesTestCase : public TestCase {
public:
    ObserverTracesTestCase()
        : TestCase("Router-observer traces report and leave the run unchanged") {}

    void DoRun() override {
        const std::string without = Run(false);
        const std::string with = Run(true);
        NS_TEST_ASSERT_MSG_EQ(with, without, "connecting the observer traces changed the run");
        NS_TEST_ASSERT_MSG_GT(m_controlTx, 0u, "no ControlTx");
        NS_TEST_ASSERT_MSG_GT(m_controlRx, 0u, "no ControlRx");
        NS_TEST_ASSERT_MSG_EQ(m_unnamedKinds, 0u, "a control kind without a name");
        NS_TEST_ASSERT_MSG_EQ(m_zeroBytes, 0u, "a control message of 0 bytes");
        NS_TEST_ASSERT_MSG_GT(m_forwarded, 0u, "no Unicast data decision");
        NS_TEST_ASSERT_MSG_GT(m_delivered, 0u, "no Deliver data decision");
        // One Deliver per 64-byte datagram the sink got: ants are not data.
        NS_TEST_ASSERT_MSG_EQ(m_delivered, m_rxBytes / 64, "Deliver count != data delivered");
        NS_TEST_ASSERT_MSG_GT(m_queued, 0u, "no Queue data decision");
        NS_TEST_ASSERT_MSG_GT(m_stateReports, 0u, "no StateSize report");
        NS_TEST_ASSERT_MSG_EQ(m_badStateBytes, 0u, "StateSize bytes are not entries x 16");
        NS_TEST_ASSERT_MSG_EQ(ns3::anthocnet::RoutingProtocol::GetKindName(0x82),
                              std::string("REACTIVE_BANT"), "kind name");
        NS_TEST_ASSERT_MSG_EQ(ns3::anthocnet::RoutingProtocol::GetKindName(0x7f),
                              std::string("127"), "unnamed kind");
    }

private:
    // The 3-node line of AntHocNetDeliveryTestCase; returns its event stream.
    std::string Run(bool observe) {
        RngSeedManager::SetSeed(1);
        RngSeedManager::SetRun(1);
        m_events.str("");
        m_rxBytes = 0;

        NodeContainer nodes;
        nodes.Create(3);
        Ptr<SimpleChannel> channel = CreateObject<SimpleChannel>();
        SimpleNetDeviceHelper devHelper;
        NetDeviceContainer devices = devHelper.Install(nodes, channel);
        Ptr<SimpleNetDevice> d0 = DynamicCast<SimpleNetDevice>(devices.Get(0));
        Ptr<SimpleNetDevice> d2 = DynamicCast<SimpleNetDevice>(devices.Get(2));
        channel->BlackList(d0, d2);
        channel->BlackList(d2, d0);

        AntHocNetHelper anthocnet;
        InternetStackHelper internet;
        internet.SetRoutingHelper(anthocnet);
        internet.Install(nodes);
        Ipv4AddressHelper address;
        address.SetBase("10.1.0.0", "255.255.255.0");
        // Both runs share this process, where streams are otherwise numbered
        // on from the previous run: fix them, before the interfaces come up and
        // the first hello is jittered, so the runs draw the same values.
        anthocnet.AssignStreams(nodes, 0);
        internet.AssignStreams(nodes, 1000);
        Ipv4InterfaceContainer ifs = address.Assign(devices);

        for (uint32_t i = 0; i < nodes.GetN(); ++i) {
            Ptr<ns3::anthocnet::RoutingProtocol> rp = DynamicCast<ns3::anthocnet::RoutingProtocol>(
                nodes.Get(i)->GetObject<Ipv4>()->GetRoutingProtocol());
            rp->TraceConnectWithoutContext("Tx", MakeCallback(&ObserverTracesTestCase::Tx, this));
            rp->TraceConnectWithoutContext("RouteChanged",
                                           MakeCallback(&ObserverTracesTestCase::Route, this));
            if (!observe) continue;
            rp->TraceConnectWithoutContext("ControlTx",
                                           MakeCallback(&ObserverTracesTestCase::ControlTx, this));
            rp->TraceConnectWithoutContext("ControlRx",
                                           MakeCallback(&ObserverTracesTestCase::ControlRx, this));
            rp->TraceConnectWithoutContext("DataDecision",
                                           MakeCallback(&ObserverTracesTestCase::Decision, this));
            rp->TraceConnectWithoutContext("StateSize",
                                           MakeCallback(&ObserverTracesTestCase::State, this));
        }

        const uint16_t port = 9;
        Ptr<Socket> rx = Socket::CreateSocket(nodes.Get(2), UdpSocketFactory::GetTypeId());
        rx->Bind(InetSocketAddress(Ipv4Address::GetAny(), port));
        rx->SetRecvCallback(MakeCallback(&ObserverTracesTestCase::Receive, this));
        Ptr<Socket> tx = Socket::CreateSocket(nodes.Get(0), UdpSocketFactory::GetTypeId());
        tx->Connect(InetSocketAddress(ifs.GetAddress(2), port));
        for (double t = 10.0; t < 18.0; t += 0.5) {
            Simulator::Schedule(Seconds(t), &ObserverTracesTestCase::Send, this, tx);
        }
        Simulator::Stop(Seconds(20.0));
        Simulator::Run();
        Simulator::Destroy();
        m_events << "rx " << m_rxBytes << "\n";
        return m_events.str();
    }

    void Send(Ptr<Socket> s) { s->Send(Create<Packet>(64)); }
    void Receive(Ptr<Socket> s) {
        Ptr<Packet> p;
        while ((p = s->Recv())) m_rxBytes += p->GetSize();
    }
    void Tx(uint8_t type, uint8_t dir, bool broadcast) {
        m_events << Simulator::Now().GetNanoSeconds() << " tx " << unsigned(type) << ' '
                 << unsigned(dir) << ' ' << broadcast << "\n";
    }
    void Route(uint32_t dest, uint32_t nb, bool added) {
        m_events << Simulator::Now().GetNanoSeconds() << " route " << dest << ' ' << nb << ' '
                 << added << "\n";
    }
    void ControlTx(uint8_t kind, bool /*broadcast*/, uint32_t bytes) {
        ++m_controlTx;
        if (ns3::anthocnet::RoutingProtocol::GetKindName(kind) == std::to_string(kind))
            ++m_unnamedKinds;
        if (bytes == 0) ++m_zeroBytes;
    }
    void ControlRx(uint8_t kind, uint32_t bytes) {
        ++m_controlRx;
        if (ns3::anthocnet::RoutingProtocol::GetKindName(kind) == std::to_string(kind))
            ++m_unnamedKinds;
        if (bytes == 0) ++m_zeroBytes;
    }
    void Decision(Ipv4Address /*dest*/, uint8_t action) {
        using ::anthocnet::core::RouteAction;
        if (action == static_cast<uint8_t>(RouteAction::Unicast)) ++m_forwarded;
        if (action == static_cast<uint8_t>(RouteAction::Deliver)) ++m_delivered;
        if (action == static_cast<uint8_t>(RouteAction::Queue)) ++m_queued;
    }
    void State(uint32_t entries, uint32_t bytes) {
        ++m_stateReports;
        if (bytes != entries * 16u) ++m_badStateBytes;
    }

    std::ostringstream m_events;
    uint32_t m_rxBytes = 0;
    uint32_t m_controlTx = 0;
    uint32_t m_controlRx = 0;
    uint32_t m_unnamedKinds = 0;
    uint32_t m_zeroBytes = 0;
    uint32_t m_forwarded = 0;
    uint32_t m_delivered = 0;
    uint32_t m_queued = 0;
    uint32_t m_stateReports = 0;
    uint32_t m_badStateBytes = 0;
};

// Every ns-3 attribute that writes a core::Config field starts at core::Config{}'s
// value (#793; it replaces the regex check tools/checks/check-default-parity.py,
// #258). The user-visible default is the attribute's initial value, not the
// constructor initialiser: ns-3 sets attributes after the constructor runs. So
// this reads both the TypeId's initial value and the attribute of a freshly
// created object, converted to the core's units (Time -> seconds), and compares
// them with the compiled Config{}. The map lives here, so renaming a core
// field is a compile error and renaming an attribute fails the lookup; neither
// is silently skipped. Deliberately not mapped: ReactiveRetryInterval, the
// adapter's own retry timer, a different quantity from the core's
// reactiveRetryInterval (docs/configuration.md §3.3 item 2).
class ConfigDefaultsMatchCoreTestCase : public TestCase {
public:
    ConfigDefaultsMatchCoreTestCase()
        : TestCase("ns-3 attribute defaults equal core::Config{} (#793)") {}

    void DoRun() override {
        using ns3::anthocnet::RoutingProtocol;
        const ::anthocnet::core::Config c;
        const struct {
            const char* attribute;
            double core;
        } fields[] = {
            {"Alpha", static_cast<double>(c.alpha)},
            {"BetaAnts", static_cast<double>(c.betaAnts)},
            {"BetaData", static_cast<double>(c.betaData)},
            {"Gamma", static_cast<double>(c.gamma)},
            {"HopCountAlpha", static_cast<double>(c.hopCountAlpha)},
            {"EnableProactive", static_cast<double>(c.enableProactive)},
            {"EnableDiffusion", static_cast<double>(c.enableDiffusion)},
            {"EnableReactive", static_cast<double>(c.enableReactive)},
            {"EnableRepair", static_cast<double>(c.enableRepair)},
            {"EnableLinkFail", static_cast<double>(c.enableLinkFail)},
            {"EnableDirectedReactive", static_cast<double>(c.enableDirectedReactive)},
            {"ShareKinematics", static_cast<double>(c.shareKinematics)},
            {"EnableLinkLifetime", static_cast<double>(c.enableLinkLifetime)},
            {"LinkLifetimeRange", static_cast<double>(c.linkLifetimeRange)},
            {"LinkLifetimeHorizon", static_cast<double>(c.linkLifetimeHorizon)},
            {"LinkLifetimeFloor", static_cast<double>(c.linkLifetimeFloor)},
            {"EnableQuietMode", static_cast<double>(c.enableQuietMode)},
            {"QuietTolerance", static_cast<double>(c.quietTolerance)},
            {"QuietBackoffFactor", static_cast<double>(c.quietBackoffFactor)},
            {"QuietMaxBackoff", static_cast<double>(c.quietMaxBackoff)},
            {"EnableAdaptiveEvaporation", static_cast<double>(c.enableAdaptiveEvaporation)},
            {"AdaptiveEvapRefChurn", static_cast<double>(c.adaptiveEvapRefChurn)},
            {"AdaptiveEvapMin", static_cast<double>(c.adaptiveEvapMin)},
            {"AdaptiveEvapMax", static_cast<double>(c.adaptiveEvapMax)},
            {"AdaptiveEvapTau", static_cast<double>(c.adaptiveEvapTau)},
            {"ProactiveBroadcastProb", static_cast<double>(c.proactiveBroadcastProb)},
            {"ProactiveVirtualMargin", static_cast<double>(c.proactiveVirtualMargin)},
            {"MaxHelloAdverts", static_cast<double>(c.maxHelloAdverts)},
            {"SessionTtl", static_cast<double>(c.sessionTtl)},
            {"HelloInterval", static_cast<double>(c.helloInterval)},
            {"ProactiveInterval", static_cast<double>(c.proactiveInterval)},
            {"TxFailureThreshold", static_cast<double>(c.txFailureThreshold)},
            {"RepairWaitFactor", static_cast<double>(c.repairWaitFactor)},
            {"RepairTimeout", static_cast<double>(c.repairTimeout)},
            {"HopTime", static_cast<double>(c.hopTimeSec)},
            {"EnableMultipath", static_cast<double>(c.enableMultipath)},
            {"AntAcceptanceFactor", static_cast<double>(c.antAcceptanceFactor)},
            {"AntAcceptanceFactorNewHop", static_cast<double>(c.antAcceptanceFactorNewHop)},
            {"LinkfailNotifyInterval", static_cast<double>(c.linkfailNotifyInterval)},
            {"EnableMacMetric", static_cast<double>(c.enableMacMetric)},
            {"SinrCriticalDb", static_cast<double>(c.sinrCriticalDb)},
            {"SinrPenaltyCost", static_cast<double>(c.sinrPenaltyCost)},
        };
        const TypeId tid = RoutingProtocol::GetTypeId();
        Ptr<RoutingProtocol> fresh = CreateObject<RoutingProtocol>();
        for (const auto& f : fields) {
            struct TypeId::AttributeInformation info;
            NS_TEST_ASSERT_MSG_EQ(tid.LookupAttributeByName(f.attribute, &info), true,
                                  "no attribute " << f.attribute << " on RoutingProtocol");
            NS_TEST_EXPECT_MSG_EQ(AsCoreValue(*info.initialValue), f.core,
                                  f.attribute << ": attribute initial value differs from "
                                                 "core::Config{}");
            Ptr<AttributeValue> now = info.checker->Create();
            fresh->GetAttribute(f.attribute, *now);
            NS_TEST_EXPECT_MSG_EQ(AsCoreValue(*now), f.core,
                                  f.attribute << ": a new RoutingProtocol differs from "
                                                 "core::Config{}");
        }
    }

private:
    // The attribute's value in the core's units, as a double (bools 0/1).
    static double AsCoreValue(const AttributeValue& v) {
        if (const auto* d = dynamic_cast<const DoubleValue*>(&v)) return d->Get();
        if (const auto* b = dynamic_cast<const BooleanValue*>(&v)) return b->Get() ? 1.0 : 0.0;
        if (const auto* u = dynamic_cast<const UintegerValue*>(&v))
            return static_cast<double>(u->Get());
        if (const auto* i = dynamic_cast<const IntegerValue*>(&v))
            return static_cast<double>(i->Get());
        if (const auto* t = dynamic_cast<const TimeValue*>(&v)) return t->Get().GetSeconds();
        NS_FATAL_ERROR("attribute value of an unmapped type");
        return 0.0;
    }
};

// ns-3 made the TestSuite/TestCase enums scoped in ns-3.42 (enum class Type /
// Duration) and removed the deprecated unscoped aliases in ns-3.47. So the
// scoped form is required from 3.47, the unscoped form is the only one before
// 3.42, and 3.42–3.46 accept both. ANTHOCNET_NS3_SCOPED_TEST_ENUMS is defined
// by the module's CMakeLists for ns-3 >= 3.42.
#ifdef ANTHOCNET_NS3_SCOPED_TEST_ENUMS
#define AHN_TEST_TYPE_UNIT TestSuite::Type::UNIT
#define AHN_TEST_QUICK TestCase::Duration::QUICK
#else
#define AHN_TEST_TYPE_UNIT UNIT
#define AHN_TEST_QUICK TestCase::QUICK
#endif

class AntHocNetTestSuite : public TestSuite {
public:
    AntHocNetTestSuite() : TestSuite("anthocnet", AHN_TEST_TYPE_UNIT) {
        AddTestCase(new AntHeaderRoundTripTestCase(), AHN_TEST_QUICK);
        AddTestCase(new AntHeaderKinematicsTestCase(), AHN_TEST_QUICK);
        AddTestCase(new AddressMappingTestCase(), AHN_TEST_QUICK);
        AddTestCase(new AntHocNetDeliveryTestCase(), AHN_TEST_QUICK);
        AddTestCase(new MultiInterfaceDeliveryTestCase(), AHN_TEST_QUICK);
        AddTestCase(new P2pHelloThrottleTestCase(), AHN_TEST_QUICK);
        AddTestCase(new StrongestNextHopTestCase(), AHN_TEST_QUICK);
        AddTestCase(new PerHopCongestionSignalTestCase(), AHN_TEST_QUICK);
        AddTestCase(new RepairAntOnLinkBreakTestCase(), AHN_TEST_QUICK);
        AddTestCase(new ObserverTracesTestCase(), AHN_TEST_QUICK);
        AddTestCase(new RequestQueueInPlaceTestCase(), AHN_TEST_QUICK);
        AddTestCase(new ConfigDefaultsMatchCoreTestCase(), AHN_TEST_QUICK);
    }
};

static AntHocNetTestSuite g_antHocNetTestSuite;
