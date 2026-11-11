// SPDX-License-Identifier: MIT OR GPL-2.0-only
// Copyright (C) 2026 Daniel Henrique Joppi

#include "anthocnet/core/pheromone_table.h"

#include <algorithm>
#include <cmath>
#include <type_traits>

namespace anthocnet {
namespace core {

void PheromoneTable::addNeighbor(NodeAddress neighbor) {
    // Insert only if absent (std::set handles that), and seed the destination
    // sets so the neighbour is itself reachable.
    neighborTable_.insert(neighbor);
    destRegular_.insert(neighbor);
    destVirtual_.insert(neighbor);
}

void PheromoneTable::removeNeighbor(NodeAddress neighbor) { neighborTable_.erase(neighbor); }

// --- rows (#918) -------------------------------------------------------------

namespace {
template <typename Row>
auto lowerBound(Row& row, NodeAddress neighbor) -> decltype(row.begin()) {
    return std::lower_bound(
        row.begin(), row.end(), neighbor,
        [](const typename std::remove_reference<decltype(*row.begin())>::type& e, NodeAddress n) {
            return e.neighbor < n;
        });
}
}  // namespace

const PheromoneTable::Entry* PheromoneTable::find(NodeAddress dest, NodeAddress neighbor) const {
    auto row = rows_.find(dest);
    if (row == rows_.end()) return nullptr;
    auto it = lowerBound(row->second, neighbor);
    return it != row->second.end() && it->neighbor == neighbor ? &*it : nullptr;
}

PheromoneTable::Entry& PheromoneTable::upsert(NodeAddress dest, NodeAddress neighbor) {
    Row& row = rows_[dest];
    auto it = lowerBound(row, neighbor);
    if (it == row.end() || it->neighbor != neighbor)
        it = row.insert(it, Entry{neighbor, 0.0, 0.0, false, false});
    return *it;
}

void PheromoneTable::erase(NodeAddress dest, NodeAddress neighbor, bool regular) {
    auto row = rows_.find(dest);
    if (row == rows_.end()) return;
    auto it = lowerBound(row->second, neighbor);
    if (it == row->second.end() || it->neighbor != neighbor) return;
    if (regular && it->hasRegular) {
        it->hasRegular = false;
        it->regular = 0.0;
        --numRegular_;
    } else if (!regular && it->hasVirtual) {
        it->hasVirtual = false;
        it->virt = 0.0;
        --numVirtual_;
    }
    if (it->hasRegular || it->hasVirtual) return;
    row->second.erase(it);
    if (row->second.empty()) rows_.erase(row);
}

double PheromoneTable::bestRegularIn(const Row& row) const {
    double best = 0.0;
    auto nb = neighborTable_.begin();
    for (const Entry& e : row) {
        while (nb != neighborTable_.end() && *nb < e.neighbor) ++nb;
        if (nb == neighborTable_.end()) break;
        if (*nb != e.neighbor || !e.hasRegular) continue;
        if (e.regular > best) best = e.regular;
    }
    return best;
}

double PheromoneTable::bestVirtualIn(const Row& row) const {
    double best = 0.0;
    auto nb = neighborTable_.begin();
    for (const Entry& e : row) {
        while (nb != neighborTable_.end() && *nb < e.neighbor) ++nb;
        if (nb == neighborTable_.end()) break;
        if (*nb != e.neighbor || !e.hasVirtual) continue;
        if (e.virt > best) best = e.virt;
    }
    return best;
}

// --- entry management -------------------------------------------------------

void PheromoneTable::addPheromoneRegular(NodeAddress dest, NodeAddress neighbor, double value) {
    destRegular_.insert(dest);
    Entry& e = upsert(dest, neighbor);
    if (!e.hasRegular) ++numRegular_;
    e.hasRegular = true;
    e.regular = value;
}

void PheromoneTable::addPheromoneVirtual(NodeAddress dest, NodeAddress neighbor, double value) {
    destVirtual_.insert(dest);
    Entry& e = upsert(dest, neighbor);
    if (!e.hasVirtual) ++numVirtual_;
    e.hasVirtual = true;
    e.virt = value;
}

void PheromoneTable::removePheromoneRegular(NodeAddress dest, NodeAddress neighbor) {
    erase(dest, neighbor, true);
}

void PheromoneTable::removePheromoneVirtual(NodeAddress dest, NodeAddress neighbor) {
    erase(dest, neighbor, false);
}

double PheromoneTable::getPheromoneRegular(NodeAddress dest, NodeAddress neighbor) const {
    const Entry* e = find(dest, neighbor);
    return e && e->hasRegular ? e->regular : 0.0;
}

double PheromoneTable::getPheromoneVirtual(NodeAddress dest, NodeAddress neighbor) const {
    const Entry* e = find(dest, neighbor);
    return e && e->hasVirtual ? e->virt : 0.0;
}

double PheromoneTable::bestRegular(NodeAddress dest) const {
    auto row = rows_.find(dest);
    return row == rows_.end() ? 0.0 : bestRegularIn(row->second);
}

double PheromoneTable::bestVirtual(NodeAddress dest) const {
    auto row = rows_.find(dest);
    return row == rows_.end() ? 0.0 : bestVirtualIn(row->second);
}

void PheromoneTable::setPheromoneRegular(NodeAddress dest, NodeAddress neighbor, double value) {
    destRegular_.insert(dest);
    addNeighbor(neighbor);
    Entry& e = upsert(dest, neighbor);
    if (!e.hasRegular) ++numRegular_;
    e.hasRegular = true;
    e.regular = value;
}

void PheromoneTable::setPheromoneVirtual(NodeAddress dest, NodeAddress neighbor, double value) {
    addPheromoneVirtual(dest, neighbor, value);
}

// #655: both functions below used to look every neighbour's pheromone up and
// raise it to beta twice -- once in the normaliser (the former sumProbability /
// sumMaxProbability) and again in the sampling loop. They now do it once, into
// a small buffer, and sum and sample from it. The floating-point operations
// and their order are exactly the old ones: the normaliser adds the same
// pow() values in neighbour order (skipping, for data, the neighbours with no
// regular entry, as sumProbability did), and the sampler subtracts the same
// pow(w)/phSum. Selection is therefore bit-identical, which the WASM parity
// and determinism gates check.
void PheromoneTable::selectionTerms(NodeAddress dest, bool isProactiveAnt, double beta,
                                    NodeAddress exclude, std::vector<Term>& terms,
                                    double& phSum) const {
    terms.clear();
    phSum = 0.0;
    // #918: walk dest's row beside the neighbour set (both in neighbour order)
    // instead of looking each (neighbour, dest) pair up in a map. A neighbour
    // without an entry reads 0, as a missing key did.
    static const Row kNoRow;
    auto rowIt = rows_.find(dest);
    const Row& row = rowIt == rows_.end() ? kNoRow : rowIt->second;
    auto e = row.begin();
    for (NodeAddress neighbor : neighborTable_) {
        while (e != row.end() && e->neighbor < neighbor) ++e;
        if (neighbor == exclude) continue;
        const bool here = e != row.end() && e->neighbor == neighbor;
        const bool useR = here && e->hasRegular;
        double phV = 0.0;
        if (isProactiveAnt && here && e->hasVirtual) phV = e->virt;
        const double phR = useR ? e->regular : 0.0;
        const double p = std::pow(weighted(neighbor, phR > phV ? phR : phV), beta);
        // sumMaxProbability counted every neighbour; sumProbability only those
        // with a regular entry.
        if (isProactiveAnt || useR) phSum += p;
        terms.push_back({neighbor, p});
    }
}

NodeAddress PheromoneTable::nextNeighborNode(NodeAddress dest, bool isProactiveAnt, double beta,
                                             IRng& rng, NodeAddress exclude) const {
    // Unknown destination: no route. Data requires a regular entry; a proactive
    // ant may also route toward a purely-diffused (virtual-only) destination so
    // diffusion can extend reach (ADR-0007).
    const bool known = destRegular_.find(dest) != destRegular_.end() ||
                       (isProactiveAnt && destVirtual_.find(dest) != destVirtual_.end());
    if (!known) {
        return kInvalidAddress;
    }

    double phSum;
    selectionTerms(dest, isProactiveAnt, beta, exclude, terms_, phSum);
    if (phSum == 0.0) {
        // Excluding the previous hop left no route: rather than black-hole, fall
        // back to considering it (A1 "only option" rule).
        if (exclude != kInvalidAddress) {
            return nextNeighborNode(dest, isProactiveAnt, beta, rng, kInvalidAddress);
        }
        return kInvalidAddress;
    }

    double r = rng.uniform();
    NodeAddress chosen = kInvalidAddress;
    for (const Term& t : terms_) {
        chosen = t.neighbor;
        r -= t.pow / phSum;
        if (r <= 0.0) break;
    }
    return chosen;
}

std::vector<std::pair<NodeAddress, double>> PheromoneTable::nextHopDistribution(
    NodeAddress dest, bool isProactiveAnt, double beta, NodeAddress exclude) const {
    // Mirrors nextNeighborNode() term for term: the same known-destination
    // rule, the same normaliser, the same per-neighbour weight, the same A1
    // fallback -- so p_i is exactly the step the sampler subtracts from r.
    std::vector<std::pair<NodeAddress, double>> out;
    const bool known = destRegular_.find(dest) != destRegular_.end() ||
                       (isProactiveAnt && destVirtual_.find(dest) != destVirtual_.end());
    if (!known) return out;
    double phSum;
    std::vector<Term> terms;
    selectionTerms(dest, isProactiveAnt, beta, exclude, terms, phSum);
    if (phSum == 0.0) {
        if (exclude != kInvalidAddress) {
            return nextHopDistribution(dest, isProactiveAnt, beta, kInvalidAddress);
        }
        return out;
    }
    for (const Term& t : terms) {
        const double p = t.pow / phSum;
        if (p > 0.0) out.emplace_back(t.neighbor, p);
    }
    return out;
}

NodeAddress PheromoneTable::lookup(NodeAddress dest, double beta, IRng& rng,
                                   NodeAddress exclude) const {
    return nextNeighborNode(dest, false, beta, rng, exclude);
}

NodeAddress PheromoneTable::randomDestination(IRng& rng) const {
    const std::size_t size = destRegular_.size();
    if (size == 0) return kInvalidAddress;

    // Uniform draw: each entry has weight 1/size.
    double r = rng.uniform();
    const double step = 1.0 / static_cast<double>(size);
    NodeAddress dest = kInvalidAddress;
    for (NodeAddress d : destRegular_) {
        dest = d;
        r -= step;
        if (r <= 0.0) break;
    }
    return dest;
}

}  // namespace core
}  // namespace anthocnet
