// SPDX-License-Identifier: MIT OR GPL-2.0-only
// Copyright (C) 2026 Daniel Henrique Joppi

// Item 15: ant/route counters and the optional IRouterObserver fire at the
// expected events, and are zero-cost (unset observer) by construction.
#include <cstring>
#include <set>
#include <string>
#include <vector>

#include "anthocnet/core/ant_message_codec.h"
#include "anthocnet/core/ant_router_logic.h"
#include "test_support.h"
#include "testbench.h"

using namespace anthocnet::core;
using anthocnet::test::FakeClock;
using anthocnet::test::ScriptedRng;
using anthocnet::test::Testbench;

namespace {

// #917: swarm-core's events, recorded as they arrive.
struct SwarmRecorder : IRouterObserver {
    struct Control {
        std::uint8_t kind;
        bool broadcast;
        std::uint32_t bytes;
    };
    struct Decision {
        NodeAddress dest;
        RouteAction action;
    };
    std::vector<Control> sent;
    std::vector<Control> received;  // broadcast unused
    std::vector<Decision> decisions;
    std::vector<std::pair<std::uint32_t, std::uint32_t>> sizes;

    void onControlSent(std::uint8_t k, bool b, std::uint32_t bytes) override {
        sent.push_back({k, b, bytes});
    }
    void onControlReceived(std::uint8_t k, std::uint32_t bytes) override {
        received.push_back({k, false, bytes});
    }
    void onDataDecision(NodeAddress d, RouteAction a) override { decisions.push_back({d, a}); }
    void onStateSize(std::uint32_t e, std::uint32_t b) override { sizes.push_back({e, b}); }
};

// What a run leaves behind: every node's counters and pheromone table, in
// table order, so two runs compare exactly.
std::vector<double> fingerprint(Testbench& tb, int n) {
    std::vector<double> out{static_cast<double>(tb.dataDelivered),
                            static_cast<double>(tb.dataDiscarded),
                            static_cast<double>(tb.controlSent())};
    for (int i = 0; i < n; ++i) {
        const PheromoneTable& t = tb.node(i).table();
        out.push_back(static_cast<double>(t.numEntriesRegular()));
        out.push_back(static_cast<double>(t.numEntriesVirtual()));
        for (NodeAddress d : t.regularDestinations())
            for (NodeAddress nb : t.neighbors()) out.push_back(t.getPheromoneRegular(d, nb));
        for (NodeAddress d : t.virtualDestinations())
            for (NodeAddress nb : t.neighbors()) out.push_back(t.getPheromoneVirtual(d, nb));
    }
    return out;
}

// A 4-node line carrying data 0 -> 3, a link break and its repair: every ant
// type and every table change runs. `observers` (one per node) or none.
std::vector<double> runLine(std::vector<SwarmRecorder>* observers) {
    Config cfg;
    const int n = 4;
    Testbench tb(n, cfg);
    if (observers) {
        observers->resize(n);
        for (int i = 0; i < n; ++i)
            tb.node(i).setObserver(&(*observers)[static_cast<std::size_t>(i)]);
    }
    for (int i = 0; i + 1 < n; ++i) tb.linkUp(i, i + 1);
    for (int k = 0; k < 40; ++k) tb.at(1.0 + 0.25 * k, [&tb] { tb.sendData(0, 3); });
    tb.at(5.0, [&tb] { tb.linkDown(1, 2); });
    tb.at(7.0, [&tb] { tb.linkUp(1, 2); });
    tb.runUntil(12.0);
    std::vector<double> fp = fingerprint(tb, n);
    if (observers) {
        // The footprint last reported is the table's, at kStateEntryBytes each.
        for (int i = 0; i < n; ++i) {
            const SwarmRecorder& o = (*observers)[static_cast<std::size_t>(i)];
            CHECK(!o.sizes.empty());
            const auto entries = static_cast<std::uint32_t>(tb.node(i).table().numEntries());
            CHECK_EQ(o.sizes.back().first, entries);
            CHECK_EQ(o.sizes.back().second, entries * kStateEntryBytes);
            // One onControlSent per ant the node counts as sent.
            CHECK_EQ(static_cast<std::uint64_t>(o.sent.size()), tb.node(i).controlPacketsSent());
        }
    }
    return fp;
}

struct RecordingObserver : IRouterObserver {
    struct Sent {
        AntType type;
        AntDirection dir;
        bool broadcast;
    };
    std::vector<Sent> sent;
    std::vector<AntType> received;
    int routeAdds = 0;
    int routeRemoves = 0;

    void onAntSent(AntType t, AntDirection d, bool b) override { sent.push_back({t, d, b}); }
    void onAntReceived(AntType t, AntDirection /*d*/) override { received.push_back(t); }
    void onRouteChanged(NodeAddress /*dest*/, NodeAddress /*nb*/, bool added) override {
        if (added)
            ++routeAdds;
        else
            ++routeRemoves;
    }
};

}  // namespace

int main() {
    Config cfg;

    // --- reactive broadcast on data with no route increments antsSent ------
    {
        FakeClock clock;
        ScriptedRng rng({0.5});
        AntRouterLogic router(/*addr*/ 1, cfg, clock, rng);
        RecordingObserver obs;
        router.setObserver(&obs);

        router.onDataPacket(/*dest*/ 9);
        CHECK_EQ(router.antsSent(AntType::Reactive), static_cast<std::uint64_t>(1));
        CHECK_EQ(router.controlPacketsSent(), static_cast<std::uint64_t>(1));
        CHECK_EQ(obs.sent.size(), static_cast<std::size_t>(1));
        CHECK(obs.sent[0].type == AntType::Reactive);
        CHECK(obs.sent[0].dir == AntDirection::Up);
        CHECK(obs.sent[0].broadcast == true);

        // A second packet to the same dest within reactiveRetryInterval must
        // NOT launch another ant (rate-limited), so the counter is unchanged.
        router.onDataPacket(/*dest*/ 9);
        CHECK_EQ(router.antsSent(AntType::Reactive), static_cast<std::uint64_t>(1));
    }

    // --- received counter + route-added on neighbour learn -----------------
    {
        FakeClock clock;
        ScriptedRng rng({0.5});
        AntRouterLogic router(/*addr*/ 1, cfg, clock, rng);
        RecordingObserver obs;
        router.setObserver(&obs);

        AntMessage hello;
        hello.type = AntType::Hello;
        hello.direction = AntDirection::Up;
        hello.src = 2;
        hello.seqNum = 1;
        hello.helloDests = {{9, 1.0}};

        router.onReceiveAnt(hello, /*prevHop*/ 2);
        CHECK_EQ(router.antsReceived(AntType::Hello), static_cast<std::uint64_t>(1));
        CHECK_EQ(obs.received.size(), static_cast<std::size_t>(1));
        CHECK(obs.received[0] == AntType::Hello);
        CHECK(obs.routeAdds >= 1);  // neighbour 2 was learned

        // Duplicate (same src,seq) is deduped: not counted as received again.
        router.onReceiveAnt(hello, /*prevHop*/ 2);
        CHECK_EQ(router.antsReceived(AntType::Hello), static_cast<std::uint64_t>(1));
    }

    // --- forwarding the spawned backward ant counts as a send --------------
    {
        FakeClock clock;
        clock.set(1.0);
        ScriptedRng rng({0.5});
        AntRouterLogic router(/*addr*/ 9, cfg, clock, rng);
        RecordingObserver obs;
        router.setObserver(&obs);

        AntMessage fwd;
        fwd.type = AntType::Reactive;
        fwd.direction = AntDirection::Up;
        fwd.src = 3;
        fwd.dst = 9;  // this node is the destination
        fwd.seqNum = 1;
        fwd.timeStart = 0.0;
        fwd.visited = {{3, 0.0}, {4, 0.01}};

        router.onReceiveAnt(fwd, /*prevHop*/ 4);
        // The destination unicasts a backward ant home -> one send, direction Down.
        CHECK_EQ(router.antsSent(AntType::Reactive), static_cast<std::uint64_t>(1));
        CHECK_EQ(obs.sent.size(), static_cast<std::size_t>(1));
        CHECK(obs.sent[0].dir == AntDirection::Down);
        CHECK(obs.sent[0].broadcast == false);
    }

    // --- no observer set: counters still work, no crash --------------------
    {
        FakeClock clock;
        ScriptedRng rng({0.5});
        AntRouterLogic router(/*addr*/ 1, cfg, clock, rng);
        router.onDataPacket(/*dest*/ 9);
        CHECK_EQ(router.antsSent(AntType::Reactive), static_cast<std::uint64_t>(1));
    }

    // --- #917: swarm-core's events --------------------------------------------
    // The kind folds the direction into the type byte; every kind is named once.
    {
        CHECK_EQ(controlKind(AntType::Reactive, AntDirection::Up), static_cast<std::uint8_t>(0x02));
        CHECK_EQ(controlKind(AntType::Reactive, AntDirection::Down),
                 static_cast<std::uint8_t>(0x82));
        std::set<std::uint8_t> kinds;
        for (const KindName& k : kControlKindNames) {
            CHECK(k.name != nullptr);
            CHECK(kinds.insert(k.kind).second);  // no duplicate kind
            CHECK(std::strcmp(findKindName(k.kind), k.name) == 0);
        }
        for (AntType t : {AntType::Hello, AntType::Reactive, AntType::Proactive, AntType::Repair,
                          AntType::LinkFail})
            CHECK(findKindName(controlKind(t, AntDirection::Up)) != nullptr);
        for (AntType t : {AntType::Reactive, AntType::Proactive, AntType::Repair})
            CHECK(findKindName(controlKind(t, AntDirection::Down)) != nullptr);
        CHECK(findKindName(0x7f) == nullptr);
        CHECK_EQ(std::string(findKindName(0x82)), std::string("REACTIVE_BANT"));
    }

    // Sent and received control carry the kind and the codec's encoded size;
    // a data packet with no route is a Queue decision.
    {
        FakeClock clock;
        ScriptedRng rng({0.5});
        AntRouterLogic router(/*addr*/ 1, cfg, clock, rng);
        SwarmRecorder obs;
        router.setObserver(&obs);

        const std::vector<RouteDecision> out = router.onDataPacket(/*dest*/ 9);
        CHECK_EQ(obs.decisions.size(), static_cast<std::size_t>(1));
        CHECK(obs.decisions[0].dest == 9);
        CHECK(obs.decisions[0].action == RouteAction::Queue);
        CHECK_EQ(obs.sent.size(), static_cast<std::size_t>(1));
        CHECK_EQ(obs.sent[0].kind, static_cast<std::uint8_t>(0x02));
        CHECK(obs.sent[0].broadcast);
        CHECK_EQ(out.size(), static_cast<std::size_t>(2));
        CHECK_EQ(obs.sent[0].bytes,
                 static_cast<std::uint32_t>(codec::serializedSize(out[1].message)));

        AntMessage hello = router.createHelloAnt();
        CHECK_EQ(obs.sent.size(), static_cast<std::size_t>(2));
        CHECK_EQ(obs.sent[1].kind, static_cast<std::uint8_t>(0x01));
        CHECK_EQ(obs.sent[1].bytes, static_cast<std::uint32_t>(codec::serializedSize(hello)));

        AntMessage peer;
        peer.type = AntType::Hello;
        peer.direction = AntDirection::Up;
        peer.src = 2;
        peer.seqNum = 1;
        peer.helloDests = {{9, 1.0}};
        router.onReceiveAnt(peer, /*prevHop*/ 2);
        CHECK_EQ(obs.received.size(), static_cast<std::size_t>(1));
        CHECK_EQ(obs.received[0].kind, static_cast<std::uint8_t>(0x01));
        CHECK_EQ(obs.received[0].bytes, static_cast<std::uint32_t>(codec::serializedSize(peer)));
        // Learning neighbour 2 grew the table, so the footprint was reported.
        CHECK(!obs.sizes.empty());
        CHECK_EQ(obs.sizes.back().first, static_cast<std::uint32_t>(router.table().numEntries()));

        // With a route to 2, data to 2 is forwarded: a Unicast decision.
        router.onDataPacket(/*dest*/ 2);
        CHECK(obs.decisions.back().dest == 2);
        CHECK(obs.decisions.back().action == RouteAction::Unicast);

        // A duplicate is not processed, so not reported.
        router.onReceiveAnt(peer, /*prevHop*/ 2);
        CHECK_EQ(obs.received.size(), static_cast<std::size_t>(1));
    }

    // A backward ant's kind has the backward bit.
    {
        FakeClock clock;
        clock.set(1.0);
        ScriptedRng rng({0.5});
        AntRouterLogic router(/*addr*/ 9, cfg, clock, rng);
        SwarmRecorder obs;
        router.setObserver(&obs);
        AntMessage fwd;
        fwd.type = AntType::Reactive;
        fwd.direction = AntDirection::Up;
        fwd.src = 3;
        fwd.dst = 9;
        fwd.seqNum = 1;
        fwd.visited = {{3, 0.0}, {4, 0.01}};
        router.onReceiveAnt(fwd, /*prevHop*/ 4);
        CHECK_EQ(obs.received[0].kind, static_cast<std::uint8_t>(0x02));
        CHECK_EQ(obs.sent.size(), static_cast<std::size_t>(1));
        CHECK_EQ(obs.sent[0].kind, static_cast<std::uint8_t>(0x82));
        CHECK(!obs.sent[0].broadcast);
    }

    // Observers only read (org rule 5): the same run, with an observer on every
    // node and with none, ends in the same tables and counters.
    {
        std::vector<SwarmRecorder> observers;
        const std::vector<double> without = runLine(nullptr);
        const std::vector<double> with = runLine(&observers);
        CHECK(without == with);
        CHECK(without[0] > 0);  // data was delivered: the run exercised routing
        std::size_t decisions = 0;
        for (const SwarmRecorder& o : observers) decisions += o.decisions.size();
        CHECK(decisions > 0);
    }

    return RUN_TESTS();
}
