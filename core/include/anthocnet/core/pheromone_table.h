// SPDX-License-Identifier: MIT OR GPL-2.0-only
// Copyright (C) 2026 Daniel Henrique Joppi

/**
 * PheromoneTable: per-node routing state.
 *
 * Holds the regular and virtual pheromone, the neighbour set, and the
 * per-table destination sets. This began as a near-verbatim port of the
 * original NS-2 class with two changes:
 *   - randomness is taken from an injected IRng (not libc rand());
 *   - the probabilistic selection helpers are const-correct and take the
 *     config so BETA is not a compile-time constant.
 *
 * Storage (#918): one row per destination, holding a {neighbour, regular,
 * virtual} entry per neighbour that has pheromone toward it, in neighbour
 * order. Next-hop selection, best*() and evaporation walk one contiguous row
 * beside the (ordered) neighbour set instead of looking every (neighbour,
 * destination) pair up in a map. A row is dropped when its last entry is.
 * What the table answers is unchanged: entries of a node that is no longer a
 * neighbour stay (as they did in the maps) and are skipped by every walk, and
 * the destination sets keep their never-shrinking meaning, because callers
 * observe them (routing-table dumps, randomDestination, adaptive-evaporation
 * churn state); an empty destination simply has no row.
 */
#ifndef ANTHOCNET_CORE_PHEROMONE_TABLE_H
#define ANTHOCNET_CORE_PHEROMONE_TABLE_H

#include <cstddef>
#include <iterator>
#include <map>
#include <set>
#include <utility>
#include <vector>

#include "anthocnet/core/config.h"
#include "anthocnet/core/ports.h"
#include "anthocnet/core/types.h"

namespace anthocnet {
namespace core {

class PheromoneTable {
public:
    using NeighborSet = std::set<NodeAddress>;
    using DestinationSet = std::set<NodeAddress>;

    // --- entry management -------------------------------------------------
    void addPheromoneRegular(NodeAddress dest, NodeAddress neighbor, double value);
    void addPheromoneVirtual(NodeAddress dest, NodeAddress neighbor, double value);
    void removePheromoneRegular(NodeAddress dest, NodeAddress neighbor);
    void removePheromoneVirtual(NodeAddress dest, NodeAddress neighbor);
    double getPheromoneRegular(NodeAddress dest, NodeAddress neighbor) const;
    double getPheromoneVirtual(NodeAddress dest, NodeAddress neighbor) const;

    /// Best (largest) regular pheromone any neighbour holds for `dest`, or 0 if
    /// none. Used to advertise this node's path goodness in hello adverts.
    double bestRegular(NodeAddress dest) const;
    /// Best (largest) virtual pheromone any neighbour holds for `dest`, or 0 if
    /// none. Same units as bestRegular (the diffusion bootstrap re-inverts into
    /// regular units), so the two are directly comparable — that comparison is
    /// the proactive emission gate (#180).
    double bestVirtual(NodeAddress dest) const;
    void setPheromoneRegular(NodeAddress dest, NodeAddress neighbor, double value);
    void setPheromoneVirtual(NodeAddress dest, NodeAddress neighbor, double value);

    bool isEmptyRegular() const { return numRegular_ == 0; }
    bool isEmptyVirtual() const { return numVirtual_ == 0; }

    /// Size gauge (issue #133): current (neighbor, dest) entry counts. The
    /// table grows with destinations x neighbours (regular + virtual) and only
    /// evaporation / link-failure removal shrink it, so adapters export these
    /// to observe growth over long runs. Read-only; no routing behaviour.
    std::size_t numEntriesRegular() const { return numRegular_; }
    std::size_t numEntriesVirtual() const { return numVirtual_; }
    std::size_t numEntries() const { return numRegular_ + numVirtual_; }
    /// Destinations that currently hold a row, i.e. at least one regular or
    /// virtual entry (#918). Unlike the destination sets, this shrinks.
    std::size_t numRows() const { return rows_.size(); }

    /// Calls f(dest, bestRegular(dest)) for every regular destination, in
    /// destination order: the same values as calling bestRegular() per
    /// destination, read straight from the rows (#918, the hello build).
    template <typename F>
    void forEachBestRegular(F f) const;

    /// Ages every entry in place (#918): for each row, each entry whose
    /// neighbour is a current neighbour and whose value is > 0 is multiplied
    /// by factorFor(dest), and removed when the result is below
    /// `minPheromone`. Regular and virtual alike. This is exactly what
    /// PheromoneEngine::evaporateAll used to do through get/set/remove over
    /// destinations x neighbours, without the lookups or the copies.
    template <typename FactorFor>
    void ageEntries(FactorFor factorFor, double minPheromone);

    // --- neighbour management --------------------------------------------
    void addNeighbor(NodeAddress neighbor);
    void removeNeighbor(NodeAddress neighbor);
    const NeighborSet& neighbors() const { return neighborTable_; }
    std::size_t numNeighbors() const { return neighborTable_.size(); }
    bool hasNeighbor() const { return !neighborTable_.empty(); }

    const DestinationSet& regularDestinations() const { return destRegular_; }
    const DestinationSet& virtualDestinations() const { return destVirtual_; }

    // --- routing ----------------------------------------------------------
    /// Stochastic next-hop choice. With isProactiveAnt the virtual table is
    /// blended in (max of regular/virtual); otherwise only regular is used.
    /// `beta` is the Eq.1 exponent (caller passes betaAnts or betaData).
    /// `exclude` (e.g. the previous hop for data) is skipped unless it is the
    /// only option, so a packet isn't sent back the way it came (A1).
    /// Returns kInvalidAddress when no route exists.
    NodeAddress nextNeighborNode(NodeAddress dest, bool isProactiveAnt, double beta, IRng& rng,
                                 NodeAddress exclude = kInvalidAddress) const;

    /// The probability distribution nextNeighborNode() samples from, for the
    /// same arguments: one (neighbour, probability) pair per candidate, in
    /// neighbour order, summing to 1 (empty when there is no route). Applies
    /// the same A1 fallback (the excluded hop is reconsidered when it is the
    /// only option). Read-only: for inspection and teaching (#546), never a
    /// second routing path -- it consumes no randomness.
    std::vector<std::pair<NodeAddress, double>> nextHopDistribution(
        NodeAddress dest, bool isProactiveAnt, double beta,
        NodeAddress exclude = kInvalidAddress) const;
    /// Reactive lookup == nextNeighborNode(dest, false, beta).
    NodeAddress lookup(NodeAddress dest, double beta, IRng& rng,
                       NodeAddress exclude = kInvalidAddress) const;

    /// Uniformly pick a known regular destination, or kInvalidAddress if none.
    NodeAddress randomDestination(IRng& rng) const;

    /// Per-neighbour multipliers on the pheromone used for next-hop choice
    /// (link-lifetime discount, #574). nullptr (the default) means none, and
    /// selection is then arithmetically identical to a table without this
    /// hook. A neighbour missing from the map is not discounted. The map is
    /// owned by the caller and must outlive its use.
    void setLinkWeights(const std::map<NodeAddress, double>* weights) { linkWeights_ = weights; }

private:
    /// One neighbour's pheromone toward a row's destination. An absent value
    /// is has* == false (and reads as 0), as a missing map key used to.
    struct Entry {
        NodeAddress neighbor;
        double regular;
        double virt;
        bool hasRegular;
        bool hasVirtual;
    };
    using Row = std::vector<Entry>;  ///< sorted by neighbour
    using Rows = std::map<NodeAddress, Row>;

    const Entry* find(NodeAddress dest, NodeAddress neighbor) const;
    Entry& upsert(NodeAddress dest, NodeAddress neighbor);
    /// Clears one value; erases the entry, and the row, once nothing is left.
    void erase(NodeAddress dest, NodeAddress neighbor, bool regular);
    /// The largest regular value a current neighbour holds in `row`, or 0.
    double bestRegularIn(const Row& row) const;
    double bestVirtualIn(const Row& row) const;

    /// One candidate's selection weight, pow(pheromone, beta) (#655).
    struct Term {
        NodeAddress neighbor;
        double pow;
    };
    /// Every candidate's weight, once, in neighbour order, and the normaliser
    /// the old sumProbability / sumMaxProbability computed (#655).
    void selectionTerms(NodeAddress dest, bool isProactiveAnt, double beta, NodeAddress exclude,
                        std::vector<Term>& terms, double& phSum) const;
    /// `ph` times `neighbor`'s link weight, or `ph` untouched without one.
    double weighted(NodeAddress neighbor, double ph) const {
        if (!linkWeights_) return ph;
        auto it = linkWeights_->find(neighbor);
        return it == linkWeights_->end() ? ph : ph * it->second;
    }
    const std::map<NodeAddress, double>* linkWeights_ = nullptr;  ///< #574, optional
    /// Scratch buffer for nextNeighborNode, kept to avoid an allocation per
    /// decision. Not part of the table's state.
    mutable std::vector<Term> terms_;

    Rows rows_;
    std::size_t numRegular_ = 0;
    std::size_t numVirtual_ = 0;
    NeighborSet neighborTable_;
    DestinationSet destRegular_;
    DestinationSet destVirtual_;
};

template <typename F>
void PheromoneTable::forEachBestRegular(F f) const {
    auto row = rows_.begin();
    for (NodeAddress dest : destRegular_) {
        while (row != rows_.end() && row->first < dest) ++row;
        const bool has = row != rows_.end() && row->first == dest;
        f(dest, has ? bestRegularIn(row->second) : 0.0);
    }
}

template <typename FactorFor>
void PheromoneTable::ageEntries(FactorFor factorFor, double minPheromone) {
    for (auto row = rows_.begin(); row != rows_.end();) {
        const double f = factorFor(row->first);
        Row& entries = row->second;
        auto nb = neighborTable_.begin();
        std::size_t out = 0;
        for (std::size_t i = 0; i < entries.size(); ++i) {
            Entry e = entries[i];
            while (nb != neighborTable_.end() && *nb < e.neighbor) ++nb;
            if (nb != neighborTable_.end() && *nb == e.neighbor) {
                if (e.hasRegular && !(e.regular <= 0.0)) {
                    const double aged = e.regular * f;
                    if (aged < minPheromone) {
                        e.hasRegular = false;
                        e.regular = 0.0;
                        --numRegular_;
                    } else {
                        e.regular = aged;
                    }
                }
                if (e.hasVirtual && !(e.virt <= 0.0)) {
                    const double aged = e.virt * f;
                    if (aged < minPheromone) {
                        e.hasVirtual = false;
                        e.virt = 0.0;
                        --numVirtual_;
                    } else {
                        e.virt = aged;
                    }
                }
            }
            if (e.hasRegular || e.hasVirtual) entries[out++] = e;
        }
        entries.resize(out);
        row = entries.empty() ? rows_.erase(row) : std::next(row);
    }
}

}  // namespace core
}  // namespace anthocnet

#endif  // ANTHOCNET_CORE_PHEROMONE_TABLE_H
