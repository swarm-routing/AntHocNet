// SPDX-License-Identifier: MIT OR GPL-2.0-only
// Copyright (C) 2026 Daniel Henrique Joppi

/**
 * RouteDecision: the verb the algorithm hands back to an adapter.
 *
 * AntRouterLogic is pure: it never sends a packet or schedules a timer
 * itself. It returns RouteDecisions and the adapter carries them out against
 * its simulator (unicast to nextHop, link-layer broadcast, enqueue pending,
 * deliver to the local transport, or drop).
 */
#ifndef ANTHOCNET_CORE_ROUTE_DECISION_H
#define ANTHOCNET_CORE_ROUTE_DECISION_H

#include "anthocnet/core/ant_message.h"
#include "anthocnet/core/types.h"
#include "swarm-route-decision.h"

namespace anthocnet {
namespace core {

/// swarm-core's RouteAction (swarm-routing/ns3-swarm-core#14, S2): the same
/// enumerators in the same order (None, Unicast, Broadcast, Queue, Deliver,
/// Drop, DiscardPending). DiscardPending is local repair timing out ([1] §3.5,
/// D6): discard the data packets queued for destination `nextHop`.
using swarm::core::RouteAction;

/// The verb with its optional ant: swarm-core's RouteDecision over AntMessage
/// (swarm-routing/ns3-swarm-core#14, S3). The same fields and factories as
/// AntHocNet's own had, with `message` a std::optional<AntMessage>: it holds the
/// ant to emit for Unicast/Broadcast of an ant, and is empty for a data
/// verdict (Queue, Deliver, Drop, DiscardPending, a data Unicast).
using RouteDecision = swarm::core::RouteDecision<AntMessage>;

}  // namespace core
}  // namespace anthocnet

#endif  // ANTHOCNET_CORE_ROUTE_DECISION_H
