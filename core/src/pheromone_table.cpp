// SPDX-License-Identifier: MIT OR GPL-2.0-only
// Copyright (C) 2026 Daniel Henrique Joppi

#include "anthocnet/core/pheromone_table.h"

#include <algorithm>
#include <cmath>
#include <vector>

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

// --- rows (#918; swarm-core's tables, S7) -----------------------------------

std::size_t PheromoneTable::numRows() const {
    // Destinations with a regular or a virtual entry: the union of the two
    // tables' rows (both ascending by destination).
    const std::vector<NodeAddress> r = regular_.destinations();
    const std::vector<NodeAddress> v = virtual_.destinations();
    std::size_t n = 0;
    auto i = r.begin();
    auto j = v.begin();
    while (i != r.end() || j != v.end()) {
        if (j == v.end() || (i != r.end() && *i < *j)) {
            ++i;
        } else if (i == r.end() || *j < *i) {
            ++j;
        } else {
            ++i;
            ++j;
        }
        ++n;
    }
    return n;
}

// The best value among the row's entries whose neighbour is a current
// neighbour (0 when none): entries of a former neighbour stay in the table
// and are skipped, as before S7.
double PheromoneTable::bestIn(Row row) const {
    double best = 0.0;
    auto nb = neighborTable_.begin();
    for (const auto& e : row) {
        while (nb != neighborTable_.end() && *nb < e.neighbor) ++nb;
        if (nb == neighborTable_.end()) break;
        if (*nb != e.neighbor) continue;
        if (e.value > best) best = e.value;
    }
    return best;
}

void PheromoneTable::addPheromoneRegular(NodeAddress dest, NodeAddress neighbor, double value) {
    destRegular_.insert(dest);
    regular_.set(dest, neighbor, value);
}

void PheromoneTable::addPheromoneVirtual(NodeAddress dest, NodeAddress neighbor, double value) {
    destVirtual_.insert(dest);
    virtual_.set(dest, neighbor, value);
}

void PheromoneTable::removePheromoneRegular(NodeAddress dest, NodeAddress neighbor) {
    regular_.erase(dest, neighbor);
}

void PheromoneTable::removePheromoneVirtual(NodeAddress dest, NodeAddress neighbor) {
    virtual_.erase(dest, neighbor);
}

double PheromoneTable::getPheromoneRegular(NodeAddress dest, NodeAddress neighbor) const {
    return regular_.get(dest, neighbor);
}

double PheromoneTable::getPheromoneVirtual(NodeAddress dest, NodeAddress neighbor) const {
    return virtual_.get(dest, neighbor);
}

double PheromoneTable::bestRegular(NodeAddress dest) const { return bestIn(regular_.row(dest)); }

double PheromoneTable::bestVirtual(NodeAddress dest) const { return bestIn(virtual_.row(dest)); }

void PheromoneTable::setPheromoneRegular(NodeAddress dest, NodeAddress neighbor, double value) {
    destRegular_.insert(dest);
    addNeighbor(neighbor);
    regular_.set(dest, neighbor, value);
}

void PheromoneTable::setPheromoneVirtual(NodeAddress dest, NodeAddress neighbor, double value) {
    addPheromoneVirtual(dest, neighbor, value);
}

void PheromoneTable::selectionTerms(NodeAddress dest, bool isProactiveAnt, double beta,
                                    NodeAddress exclude, std::vector<Term>& terms,
                                    double& phSum) const {
    terms.clear();
    phSum = 0.0;
    // #918: walk dest's rows beside the neighbour set (all in neighbour order)
    // instead of looking each (neighbour, dest) pair up. A neighbour without
    // an entry reads 0, as a missing key did.
    const Row reg = regular_.row(dest);
    const Row vir = virtual_.row(dest);
    auto r = reg.begin();
    auto v = vir.begin();
    for (NodeAddress neighbor : neighborTable_) {
        while (r != reg.end() && r->neighbor < neighbor) ++r;
        while (v != vir.end() && v->neighbor < neighbor) ++v;
        if (neighbor == exclude) continue;
        const bool useR = r != reg.end() && r->neighbor == neighbor;
        double phV = 0.0;
        if (isProactiveAnt && v != vir.end() && v->neighbor == neighbor) phV = v->value;
        const double phR = useR ? r->value : 0.0;
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
