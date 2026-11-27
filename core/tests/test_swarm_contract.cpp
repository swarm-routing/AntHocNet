// Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
// SPDX-License-Identifier: MIT OR GPL-2.0-only

/*
 * swarm-core's protocol contract suite (#1117, swarm-routing/swarm-core#8):
 * AntRouterLogic opts in through a testbench adapter on swarm-core's
 * swarm::core::testbench::Network, from the release swarm-core.lock names
 * (core/testbench/swarm-contract-tests.h).
 *
 * The adapter does what both real adapters do and nothing more (golden rule
 * 2): it hands the bench's clock and RNG to the core, owns the hello and
 * proactive timers and the pending-packet queue, and carries out the
 * RouteDecisions the core returns. Its traffic is one CBR stream from node 0
 * to the far corner of a 3x3 grid, so several paths exist and the core's
 * stochastic next-hop choice reaches the air.
 *
 * ZeroTtlAndSelfLoopPacketsAreDropped is skipped: no AntHocNet message
 * carries a per-hop limit. Ants are bounded by (src,seq) dedup, the capped
 * visited path (Config::maxPathLength) and, for repair and link-failure ants,
 * a broadcast budget; data packets carry no TTL in the core.
 *
 * The core runs on a default Config{}: the test reads the default path and
 * changes nothing in it.
 */
#include <cstdint>
#include <map>
#include <memory>
#include <vector>

#include "anthocnet/core/ant_message.h"
#include "anthocnet/core/ant_message_codec.h"
#include "anthocnet/core/ant_router_logic.h"
#include "anthocnet/core/config.h"
#include "anthocnet/core/ports.h"
#include "testbench/swarm-contract-tests.h"

namespace tb = swarm::core::testbench;
using namespace anthocnet::core;

namespace {

/// What the bench carries: an ant, or a data packet (only its destination
/// matters to the core).
struct BenchMsg {
    bool isData = false;
    NodeAddress dataDest = kInvalidAddress;
    AntMessage ant;
};

/// Kind byte for the bench's counters and trace: 0 for data, else one per
/// (ant type, direction).
std::uint8_t kindOf(const BenchMsg& m) {
    if (m.isData) return 0;
    return static_cast<std::uint8_t>(1 + 2 * static_cast<int>(m.ant.type) +
                                     (m.ant.isBackward() ? 1 : 0));
}

/// FNV-1a over a data packet's destination, or an ant's encoded bytes.
std::uint64_t payloadHash(const BenchMsg& m) {
    std::uint64_t h = 0xCBF29CE484222325ull;
    auto mix = [&h](std::uint8_t b) {
        h ^= b;
        h *= 0x100000001B3ull;
    };
    mix(m.isData ? 1 : 0);
    if (m.isData) {
        const auto d = static_cast<std::uint32_t>(m.dataDest);
        for (int i = 0; i < 4; ++i) mix(static_cast<std::uint8_t>(d >> (8 * i)));
        return h;
    }
    for (std::uint8_t b : codec::serialize(m.ant)) mix(b);
    return h;
}

/// Relays the core's swarm-core events to the suite's observer (the core
/// takes AntHocNet's observer, which extends swarm-core's).
class ObserverRelay : public IRouterObserver {
public:
    explicit ObserverRelay(swarm::core::IRouterObserver& to) : to_(to) {}
    void onControlSent(std::uint8_t kind, bool broadcast, std::uint32_t bytes) override {
        to_.onControlSent(kind, broadcast, bytes);
    }
    void onControlReceived(std::uint8_t kind, std::uint32_t bytes) override {
        to_.onControlReceived(kind, bytes);
    }
    void onRouteChanged(NodeAddress dest, NodeAddress nextHop, bool added) override {
        to_.onRouteChanged(dest, nextHop, added);
    }
    void onDataDecision(NodeAddress dest, RouteAction action) override {
        to_.onDataDecision(dest, action);
    }
    void onStateSize(std::uint32_t entries, std::uint32_t bytes) override {
        to_.onStateSize(entries, bytes);
    }

private:
    swarm::core::IRouterObserver& to_;
};

constexpr NodeAddress kSource = 0;
constexpr NodeAddress kSink = 8;  // the far corner of the 3x3 grid
constexpr int kPackets = 20;
constexpr double kFirstPacket = 2.0;
constexpr double kPacketInterval = 1.0;

class AntHocNetNode : public tb::INodeAdapter<BenchMsg> {
public:
    AntHocNetNode(tb::NodePorts<BenchMsg>& p, const tb::ContractProviders& prov)
        : p_(p),
          core_(p.address(), cfg_, p.clock(), p.rng(), /*metric=*/nullptr, prov.linkState,
                prov.linkQuality) {
        core_.setEnergy(prov.energy);
        if (prov.observer != nullptr) {
            relay_ = std::make_unique<ObserverRelay>(*prov.observer);
            core_.setObserver(relay_.get());
        }
    }

    void start() override {
        // Stagger the timers by address, as test_testbench's bench does, so
        // their order is fixed but they do not all fire at one instant.
        const double skew = 0.001 * p_.address();
        p_.timers().schedule(cfg_.helloInterval + skew, [this] { helloTick(); });
        p_.timers().schedule(cfg_.proactiveInterval + skew, [this] { proactiveTick(); });
        if (p_.address() == kSource) {
            for (int i = 0; i < kPackets; ++i) {
                p_.timers().schedule(kFirstPacket + kPacketInterval * i, [this] {
                    core_.noteDataSession(kSink);
                    execute(core_.onDataPacket(kSink), kSink);
                });
            }
        }
    }

    void receive(NodeAddress from, const BenchMsg& m) override {
        if (m.isData) {
            if (m.dataDest == p_.address()) return;  // delivered
            execute(core_.onDataPacket(m.dataDest, from), m.dataDest);
            return;
        }
        execute(core_.onReceiveAnt(m.ant, from));
        // A backward ant installs a route toward its origin: release what
        // waited for it, as the adapters do.
        if (m.ant.isBackward()) flush(m.ant.src);
    }

    void undelivered(NodeAddress to, const BenchMsg& m) override {
        const NodeAddress dest = m.isData ? m.dataDest : kInvalidAddress;
        execute(core_.reportTxFailure(to, dest), dest);
    }

private:
    static BenchMsg ant(const AntMessage& a) { return BenchMsg{false, kInvalidAddress, a}; }
    static BenchMsg data(NodeAddress dest) { return BenchMsg{true, dest, {}}; }

    /// Carries out the core's decisions; `dataDest` is the destination of the
    /// data packet being handled, if any.
    void execute(const std::vector<RouteDecision>& decisions,
                 NodeAddress dataDest = kInvalidAddress) {
        for (const RouteDecision& d : decisions) {
            switch (d.action) {
                case RouteAction::Unicast:
                    if (d.message.has_value())
                        p_.unicast(d.nextHop, ant(*d.message));
                    else
                        p_.unicast(d.nextHop, data(dataDest));
                    break;
                case RouteAction::Broadcast:
                    p_.broadcast(ant(*d.message));
                    break;
                case RouteAction::Queue:
                    ++pending_[d.nextHop];  // nextHop carries the destination
                    break;
                case RouteAction::DiscardPending:
                    pending_.erase(d.nextHop);
                    break;
                case RouteAction::Deliver:
                case RouteAction::Drop:
                case RouteAction::None:
                    break;
            }
        }
    }

    void flush(NodeAddress dest) {
        auto it = pending_.find(dest);
        if (it == pending_.end()) return;
        while (it->second > 0) {
            const NodeAddress next = core_.nextHopForData(dest);
            if (next == kInvalidAddress) break;
            --it->second;
            p_.unicast(next, data(dest));
        }
        if (it->second == 0) pending_.erase(it);
    }

    void helloTick() {
        execute(core_.onMaintenanceTick());
        p_.broadcast(ant(core_.createHelloAnt()));
        p_.timers().schedule(cfg_.helloInterval, [this] { helloTick(); });
    }

    void proactiveTick() {
        for (AntMessage& a : core_.createProactiveAnts()) {
            const NodeAddress next = core_.selectNextHop(a.dst, /*proactive=*/true);
            if (next == kInvalidAddress)
                p_.broadcast(ant(a));
            else
                p_.unicast(next, ant(a));
        }
        p_.timers().schedule(cfg_.proactiveInterval, [this] { proactiveTick(); });
    }

    tb::NodePorts<BenchMsg>& p_;
    Config cfg_{};
    AntRouterLogic core_;
    std::unique_ptr<ObserverRelay> relay_;
    std::map<NodeAddress, int> pending_;
};

tb::Topology grid3x3() {
    tb::Topology t(9);
    for (NodeAddress r = 0; r < 3; ++r) {
        for (NodeAddress c = 0; c < 3; ++c) {
            const NodeAddress n = 3 * r + c;
            if (c < 2) t.connect(n, n + 1);
            if (r < 2) t.connect(n, n + 3);
        }
    }
    return t;
}

}  // namespace

int main() {
    tb::ContractSpec<BenchMsg> spec;
    spec.factory = [](tb::NodePorts<BenchMsg>& p, const tb::ContractProviders& prov) {
        return std::make_unique<AntHocNetNode>(p, prov);
    };
    spec.topology = grid3x3();
    spec.kindOf = kindOf;
    spec.payloadHash = payloadHash;
    spec.duration = 30.0;  // three proactive rounds, the whole CBR stream
    // hopLimited unset: no AntHocNet message carries a hop limit (see above).
    return tb::runProtocolContract(spec) == 0 ? 0 : 1;
}
