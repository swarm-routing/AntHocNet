// SPDX-License-Identifier: MIT OR GPL-2.0-only
// Copyright (C) 2026 Daniel Henrique Joppi

/**
 * Simulator-agnostic fundamental types for the AntHocNet algorithm core.
 *
 * Nothing in core/ may include an NS-2 or NS-3 header. Addresses are modelled
 * with a plain integer so both simulators (whose own address types are
 * integral) can pass values straight through their adapters.
 */
#ifndef ANTHOCNET_CORE_TYPES_H
#define ANTHOCNET_CORE_TYPES_H

#include <cstdint>

#include "swarm-types.h"

namespace anthocnet {
namespace core {

// swarm-core's fundamental types (swarm-routing/ns3-swarm-core#14, S2): the
// same types and value as AntHocNet's own until S2, now defined once there.
/// Network-layer node address (std::int32_t: ns-3's 32-bit IPv4 payload).
using swarm::core::NodeAddress;
/// Simulation time, in seconds (double).
using swarm::core::Time;
/// "No such node / no route" sentinel (-1).
using swarm::core::kInvalidAddress;

/// A node's position (m) and velocity (m/s) in the adapter's Cartesian frame
/// (ADR-0025). Only ever compared between nodes of one simulation, so the
/// frame needs no geodesy.
struct Kinematics {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    double vx = 0.0;
    double vy = 0.0;
    double vz = 0.0;
};

}  // namespace core
}  // namespace anthocnet

#endif  // ANTHOCNET_CORE_TYPES_H
