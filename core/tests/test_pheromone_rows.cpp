// SPDX-License-Identifier: MIT OR GPL-2.0-only
// Copyright (C) 2026 Daniel Henrique Joppi

// #918: the pheromone table's per-destination rows answer exactly what the
// (neighbour, destination) maps did. The references below are the pre-#918
// algorithms written against the table's public get/set/remove API: in-place
// evaporation, best*(), the hello build's forEachBestRegular() and next-hop
// selection must match them bit for bit, including entries of nodes that are
// no longer neighbours, values at or below zero, and pruning.

#include <cstdint>
#include <cstring>
#include <map>
#include <utility>
#include <vector>

#include "anthocnet/core/pheromone_engine.h"
#include "anthocnet/core/pheromone_table.h"
#include "test_support.h"

using namespace anthocnet::core;

namespace {

class Lcg {
public:
    std::uint64_t next() {
        s_ = s_ * 6364136223846793005ULL + 1442695040888963407ULL;
        return s_ >> 33;
    }
    double unit() { return static_cast<double>(next() % 1000000) / 1000000.0; }

private:
    std::uint64_t s_ = 918;
};

// Bit-exact equality, stated as such: the rows must reproduce the maps' values
// to the last bit, so the doubles are compared as their bit patterns.
bool sameBits(double a, double b) {
    std::uint64_t ba, bb;
    std::memcpy(&ba, &a, sizeof ba);
    std::memcpy(&bb, &b, sizeof bb);
    return ba == bb;
}

const NodeAddress kDests[] = {1, 2, 3, 4, 5, 6, 7, 8, 100, 101, 102, 103};
const NodeAddress kNbrs[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
constexpr double kNearFloor = 4e-5;  // a few minPheromone: dies within a tick or two

// The pre-#918 evaporateAll, through the public API.
void referenceEvaporate(PheromoneTable& table, double dt, const Config& cfg,
                        const std::map<NodeAddress, double>* mult) {
    const double factor = std::pow(cfg.alpha, dt / cfg.evaporationInterval);
    auto factorFor = [&](NodeAddress dest) {
        if (!mult) return factor;
        auto it = mult->find(dest);
        if (it == mult->end()) return factor;
        return std::pow(cfg.alpha, dt / cfg.evaporationInterval * it->second);
    };
    const std::vector<NodeAddress> dests(table.regularDestinations().begin(),
                                         table.regularDestinations().end());
    for (NodeAddress dest : dests) {
        const double f = factorFor(dest);
        const std::vector<NodeAddress> nbs(table.neighbors().begin(), table.neighbors().end());
        for (NodeAddress n : nbs) {
            const double ph = table.getPheromoneRegular(dest, n);
            if (ph <= 0.0) continue;
            const double aged = ph * f;
            if (aged < cfg.minPheromone)
                table.removePheromoneRegular(dest, n);
            else
                table.setPheromoneRegular(dest, n, aged);
        }
    }
    const std::vector<NodeAddress> vdests(table.virtualDestinations().begin(),
                                          table.virtualDestinations().end());
    for (NodeAddress dest : vdests) {
        const double f = factorFor(dest);
        const std::vector<NodeAddress> nbs(table.neighbors().begin(), table.neighbors().end());
        for (NodeAddress n : nbs) {
            const double ph = table.getPheromoneVirtual(dest, n);
            if (ph <= 0.0) continue;
            const double aged = ph * f;
            if (aged < cfg.minPheromone)
                table.removePheromoneVirtual(dest, n);
            else
                table.setPheromoneVirtual(dest, n, aged);
        }
    }
}

// One random mutation, applied identically to both tables.
void mutate(Lcg& rng, PheromoneTable& a, PheromoneTable& b) {
    const NodeAddress d = kDests[rng.next() % 12];
    const NodeAddress n = kNbrs[rng.next() % 10];
    double v = rng.unit();
    if (rng.next() % 10 == 0) v = 0.0;         // a zero entry
    if (rng.next() % 20 == 0) v = -v;          // a negative one
    if (rng.next() % 5 == 0) v *= kNearFloor;  // near the floor
    switch (rng.next() % 9) {
        case 0:
            a.setPheromoneRegular(d, n, v);
            b.setPheromoneRegular(d, n, v);
            break;
        case 1:
            a.addPheromoneRegular(d, n, v);
            b.addPheromoneRegular(d, n, v);
            break;  // no neighbour
        case 2:
            a.setPheromoneVirtual(d, n, v);
            b.setPheromoneVirtual(d, n, v);
            break;
        case 3:
            a.removePheromoneRegular(d, n);
            b.removePheromoneRegular(d, n);
            break;
        case 4:
            a.removePheromoneVirtual(d, n);
            b.removePheromoneVirtual(d, n);
            break;
        case 5:
            a.addNeighbor(n);
            b.addNeighbor(n);
            break;
        case 6:
            a.removeNeighbor(n);
            b.removeNeighbor(n);
            break;  // entries stay, stale
        default:
            a.addPheromoneVirtual(d, n, v);
            b.addPheromoneVirtual(d, n, v);
            break;
    }
}

void checkSame(const PheromoneTable& a, const PheromoneTable& b) {
    CHECK_EQ(a.numEntriesRegular(), b.numEntriesRegular());
    CHECK_EQ(a.numEntriesVirtual(), b.numEntriesVirtual());
    CHECK(a.regularDestinations() == b.regularDestinations());
    CHECK(a.virtualDestinations() == b.virtualDestinations());
    for (NodeAddress d : kDests) {
        for (NodeAddress n : kNbrs) {
            CHECK(sameBits(a.getPheromoneRegular(d, n), b.getPheromoneRegular(d, n)));
            CHECK(sameBits(a.getPheromoneVirtual(d, n), b.getPheromoneVirtual(d, n)));
        }
    }
}

// The pre-#918 bestRegular / bestVirtual: max over current neighbours.
double referenceBest(const PheromoneTable& t, NodeAddress dest, bool regular) {
    double best = 0.0;
    for (NodeAddress n : t.neighbors()) {
        const double ph = regular ? t.getPheromoneRegular(dest, n) : t.getPheromoneVirtual(dest, n);
        if (ph > best) best = ph;
    }
    return best;
}

std::size_t countRows(const PheromoneTable& t) {
    std::size_t rows = 0;
    for (NodeAddress d : kDests) {
        bool any = false;
        for (NodeAddress n : kNbrs)
            any = any || t.getPheromoneRegular(d, n) != 0.0 || t.getPheromoneVirtual(d, n) != 0.0;
        rows += any ? 1 : 0;
    }
    return rows;
}

}  // namespace

int main() {
    Config cfg;
    const PheromoneEngine engine(cfg);

    // 1. In-place evaporation == the destinations x neighbours loop, over
    //    thousands of random mutations and ticks (with and without per-
    //    destination rate multipliers, #572).
    {
        Lcg rng;
        PheromoneTable a, b;
        std::map<NodeAddress, double> mult{{2, 0.25}, {101, 4.0}, {7, 1.5}};
        for (int round = 0; round < 3000; ++round) {
            for (int k = 0; k < 6; ++k) mutate(rng, a, b);
            const double dt = (rng.next() % 4 == 0) ? 0.0 : 0.05 + 2.0 * rng.unit();
            const auto* m = (rng.next() % 2) ? &mult : nullptr;
            engine.evaporateAll(a, dt, m);
            if (dt > 0.0) referenceEvaporate(b, dt, cfg, m);
            checkSame(a, b);
            for (NodeAddress d : kDests) {
                CHECK(sameBits(a.bestRegular(d), referenceBest(a, d, true)));
                CHECK(sameBits(a.bestVirtual(d), referenceBest(a, d, false)));
            }
        }
    }

    // 2. Pruning: a destination whose last entry evaporates loses its row,
    //    but stays in the destination set (callers observe that set).
    {
        PheromoneTable t;
        t.setPheromoneRegular(50, 1, 1.0);
        t.setPheromoneVirtual(50, 1, 1.0);
        t.setPheromoneRegular(60, 1, 1.0);
        // Rows for 50 and 60 only: adding neighbour 1 put 1 in the destination
        // sets but gave it no entry, so no row.
        CHECK_EQ(t.numRows(), static_cast<std::size_t>(2));
        for (int i = 0; i < 60; ++i) engine.evaporateAll(t, cfg.evaporationInterval);
        CHECK_EQ(t.numEntries(), static_cast<std::size_t>(0));
        CHECK_EQ(t.numRows(), static_cast<std::size_t>(0));
        CHECK(t.isEmptyRegular() && t.isEmptyVirtual());
        CHECK_EQ(t.regularDestinations().count(50), static_cast<std::size_t>(1));
        CHECK_EQ(t.regularDestinations().count(60), static_cast<std::size_t>(1));
        CHECK(sameBits(t.bestRegular(50), 0.0));
        // A removal empties a row too.
        t.setPheromoneRegular(70, 2, 0.5);
        CHECK_EQ(t.numRows(), static_cast<std::size_t>(1));
        t.removePheromoneRegular(70, 2);
        CHECK_EQ(t.numRows(), static_cast<std::size_t>(0));
    }

    // 3. A node that stops being a neighbour keeps its entries, unaged and
    //    unread, and they come back with it (as the maps behaved).
    {
        PheromoneTable t;
        t.setPheromoneRegular(9, 1, 0.5);
        t.setPheromoneRegular(9, 2, 0.8);
        t.removeNeighbor(2);
        CHECK(sameBits(t.bestRegular(9), 0.5));
        CHECK_EQ(t.numEntriesRegular(), static_cast<std::size_t>(2));
        engine.evaporateAll(t, cfg.evaporationInterval);
        CHECK_NEAR(t.getPheromoneRegular(9, 1), 0.5 * cfg.alpha, 1e-12);
        CHECK(sameBits(t.getPheromoneRegular(9, 2), 0.8));  // not a neighbour: not aged
        auto dist = t.nextHopDistribution(9, false, cfg.betaData);
        CHECK_EQ(dist.size(), static_cast<std::size_t>(1));
        CHECK_EQ(dist[0].first, 1);
        t.addNeighbor(2);
        CHECK(sameBits(t.bestRegular(9), 0.8));
    }

    // 4. forEachBestRegular == bestRegular per regular destination, in order;
    //    and the rows keep the random tables consistent with countRows().
    {
        Lcg rng;
        PheromoneTable a, b;
        for (int k = 0; k < 2000; ++k) mutate(rng, a, b);
        std::vector<std::pair<NodeAddress, double>> seen;
        a.forEachBestRegular([&seen](NodeAddress d, double best) { seen.emplace_back(d, best); });
        CHECK_EQ(seen.size(), a.regularDestinations().size());
        std::size_t i = 0;
        for (NodeAddress d : a.regularDestinations()) {
            if (i >= seen.size()) break;
            CHECK_EQ(seen[i].first, d);
            CHECK(sameBits(seen[i].second, a.bestRegular(d)));
            ++i;
        }
        CHECK(a.numRows() >= countRows(a));  // a row may hold only zero-valued entries
    }

    // 5. Selection reads rows exactly as it read the maps: the distribution
    //    equals pow(max(regular, virtual), beta) / sum over current neighbours.
    {
        Lcg rng;
        PheromoneTable a, b;
        for (int k = 0; k < 2000; ++k) mutate(rng, a, b);
        for (NodeAddress d : kDests) {
            for (bool proactive : {false, true}) {
                std::vector<std::pair<NodeAddress, double>> pw;
                double sum = 0.0;
                for (NodeAddress n : a.neighbors()) {
                    const double r = a.getPheromoneRegular(d, n);
                    const double v = proactive ? a.getPheromoneVirtual(d, n) : 0.0;
                    const double p = std::pow(r > v ? r : v, cfg.betaData);
                    sum += p;  // pow(0, beta) == 0: an absent entry adds nothing either way
                    pw.emplace_back(n, p);
                }
                const auto dist = a.nextHopDistribution(d, proactive, cfg.betaData);
                std::size_t j = 0;
                for (const auto& t : pw) {
                    if (sum == 0.0 || !(t.second / sum > 0.0)) continue;
                    if (j >= dist.size()) {
                        CHECK(false);
                        break;
                    }
                    CHECK_EQ(dist[j].first, t.first);
                    CHECK(sameBits(dist[j].second, t.second / sum));
                    ++j;
                }
                if (sum != 0.0) CHECK_EQ(j, dist.size());
            }
        }
    }

    return RUN_TESTS();
}
