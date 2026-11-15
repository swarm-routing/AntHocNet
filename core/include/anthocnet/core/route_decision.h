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

struct RouteDecision {
    RouteAction action = RouteAction::None;
    NodeAddress nextHop = kInvalidAddress;
    bool hasMessage = false;  ///< Whether `message` is populated.
    AntMessage message;       ///< Ant to emit (Unicast/Broadcast).

    static RouteDecision drop() { return {RouteAction::Drop, kInvalidAddress, false, {}}; }
    static RouteDecision none() { return {RouteAction::None, kInvalidAddress, false, {}}; }
    static RouteDecision deliver() { return {RouteAction::Deliver, kInvalidAddress, false, {}}; }
};

}  // namespace core
}  // namespace anthocnet

#endif  // ANTHOCNET_CORE_ROUTE_DECISION_H
