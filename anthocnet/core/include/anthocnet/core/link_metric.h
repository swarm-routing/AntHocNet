// SPDX-License-Identifier: MIT OR GPL-2.0-only
// Copyright (C) 2026 Daniel Henrique Joppi

/**
 * ILinkMetric: the strategy that turns a backward ant's path observation into a
 * pheromone value (Eq.2). Extracting it from advanceBackAnt makes the pure core
 * the natural home for both the canonical metric and pluggable research metrics
 * (fuzzy / energy / QoS), selected by config, without touching the state machine
 * (item 16, ADR-0003).
 *
 * The metric must be pure: it reads only the observation, never the simulator,
 * clock, or RNG. Adapter-supplied signals (energy, SNR, queue occupancy) enter
 * through LinkObservation, keeping the core free of simulator dependencies.
 */
#ifndef ANTHOCNET_CORE_LINK_METRIC_H
#define ANTHOCNET_CORE_LINK_METRIC_H

#include <cmath>
#include <limits>

#include "swarm-energy-metric.h"
#include "swarm-link-metric.h"

namespace anthocnet {
namespace core {

// The metric seam is swarm-core's (swarm-routing/ns3-swarm-core#14, S8):
// LinkObservation (hops, pathTime, hopTime; swarm-core adds a `stability` this
// core leaves NaN), HopSignals (the node-local sinrDb and residualEnergy, NaN
// when unknown, which every metric reads as "no evidence against this link")
// and ILinkMetric (pheromone(obs); usesLinkQuality(): the per-hop cost is a
// link-quality cost (#181), each node writing its own estimate of the link a
// backward ant came over into that hop, thesis §4.2.2; hopCost(signals): that
// cost). Observations carry path aggregates and node-local signals only, never
// per-hop downstream vectors (ADR-0009). The thresholds AntHocNet's HopSignals
// used to carry (SINR_c, c_const, the energy floor) are members of the metric
// instances below; AntRouterLogic builds them from its Config.
using swarm::core::HopSignals;
using swarm::core::ILinkMetric;
using swarm::core::LinkObservation;

/// The canonical AntHocNet metric (Eq.2): blend the path-time estimate with the
/// hop-count estimate and invert. Identical to the post-item-02 inline formula.
struct ClassicMetric : ILinkMetric {
    double pheromone(const LinkObservation& o) const override {
        return std::pow((o.pathTime + o.hops * o.hopTime) / 2.0, -1.0);
    }
};

/// Eq. 4.11: 1 for a link whose SINR is above `criticalDb`, `penalty` for one
/// at or below it. No sample (NaN) counts as a good link (ILinkQuality).
inline double sinrLinkCost(double sinrDb, double criticalDb, double penalty) {
    return (std::isnan(sinrDb) || sinrDb > criticalDb) ? 1.0 : penalty;
}

/// The SINR metric (#181), the one the 2007 thesis used for every headline
/// result: "we normally use the signal-to-interference-and-noise ratio metric,
/// as this gave the best results" (§4.2.6). Each link costs 1 when its SINR is
/// above SINR_c and c_const otherwise (eq. 4.11, sinrLinkCost); a route costs
/// the sum over its links, carried as the hops' per-hop values; and the
/// pheromone is the inverse of that cost (eq. 4.3). SINR_c and c_const are
/// Config::sinrCriticalDb and Config::sinrPenaltyCost (defaults 17 dB, 3).
struct SinrMetric : ILinkMetric {
    explicit SinrMetric(double criticalDb = 17.0, double penaltyCost = 3.0)
        : criticalDb_(criticalDb), penaltyCost_(penaltyCost) {}
    double pheromone(const LinkObservation& o) const override {
        // Every hop costs at least 1, so the cost is never below the hop count.
        const double cost = o.pathTime > o.hops ? o.pathTime : o.hops;
        return cost > 0.0 ? 1.0 / cost : 0.0;
    }
    bool usesLinkQuality() const override { return true; }
    double hopCost(const HopSignals& s) const override {
        return sinrLinkCost(s.sinrDb, criticalDb_, penaltyCost_);
    }
    double criticalDb() const { return criticalDb_; }
    double penaltyCost() const { return penaltyCost_; }

private:
    double criticalDb_;
    double penaltyCost_;
};

/// The energy-aware metric (#145, the BeeAdHoc / EEABR line) is swarm-core's
/// EnergyMetric (swarm-routing/ns3-swarm-core#120, AntHocNet's bit for bit): a
/// hop costs 1 / e, e the forwarding node's residual energy fraction, floored
/// (Config::energyCostFloor, default 0.01) so an almost-empty node costs a
/// large but finite amount; a route costs the sum over its relays and the
/// pheromone is the inverse. Each node prices only its own hop, so nothing new
/// travels on the wire (ADR-0009, as for SINR, ADR-0024). Unknown energy (NaN)
/// costs 1: the metric degrades to hop count, never to a wrong ranking.
using swarm::core::EnergyMetric;

}  // namespace core
}  // namespace anthocnet

#endif  // ANTHOCNET_CORE_LINK_METRIC_H
