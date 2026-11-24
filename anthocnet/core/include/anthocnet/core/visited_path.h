// SPDX-License-Identifier: MIT OR GPL-2.0-only
// Copyright (C) 2026 Daniel Henrique Joppi

/**
 * The visited-node stack an ant carries.
 *
 * In the original NS-2 code this was a header-resident `AntTimeEntry**`
 * malloc'd array of heap pointers, which leaked, double-freed on broadcast
 * (the same header memory was reused by NS-2's packet pool), and serialized
 * as `sizeof(pointer)` bytes regardless of the real path length. Modelling it
 * as a value-type vector of PODs removes that entire class of bug.
 */
#ifndef ANTHOCNET_CORE_VISITED_PATH_H
#define ANTHOCNET_CORE_VISITED_PATH_H

#include <cstddef>

#include "anthocnet/core/types.h"
#include "swarm-visited-path.h"

namespace anthocnet {
namespace core {

/// One entry on an ant's visited-node stack: a node and the trip time the ant
/// took to reach it from the previous hop. swarm-core's Hop
/// (swarm-routing/ns3-swarm-core#14, S5), the same two fields.
using AntHop = swarm::core::Hop;

/// The stack itself: swarm-core's bounded VisitedPath (push/pop/back,
/// setBackTime to re-time the last hop, range-for over hops()).
using VisitedPath = swarm::core::VisitedPath;

/// The capacity of an ant's stacks. Config::maxPathLength bounds the forward
/// stack (the router drops an ant that reaches it) and the codec refuses more
/// than kMaxVisitedOnWire on the wire; the stack's own cap is swarm-core's
/// largest, so it never refuses a hop the router lets through, whatever
/// maxPathLength is configured: no behaviour changes with S5.
constexpr std::size_t kHopStackCapacity = VisitedPath::kMaxCapacity;

}  // namespace core
}  // namespace anthocnet

#endif  // ANTHOCNET_CORE_VISITED_PATH_H
