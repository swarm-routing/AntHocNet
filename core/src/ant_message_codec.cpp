// SPDX-License-Identifier: MIT OR GPL-2.0-only
// Copyright (C) 2026 Daniel Henrique Joppi

#include "anthocnet/core/ant_message_codec.h"

#include <cmath>

#include "swarm-wire.h"

namespace anthocnet {
namespace core {
namespace codec {

namespace {

// The little-endian field writers and readers are swarm-core's WireWriter and
// WireReader (swarm-routing/ns3-swarm-core#14, S6): the same bytes the private
// Writer/Reader produced (every field little-endian, a double as its IEEE-754
// bits), so kWireVersion is unchanged. The decoder keeps its per-section checks
// below; WireReader also refuses any read past the end, so a frame the
// sections accept is read whole and a short one is refused either way.

constexpr std::size_t kVersionSize = 1;    // offset-0 wire-version byte
constexpr std::size_t kHopSize = 4 + 8;    // int32 node + double time
constexpr std::size_t kHelloSize = 4 + 8;  // int32 node + double pheromone
// type,dir,src,dst,seq,timeStart,lifeAnt,broadcastBudget (prevHop/hops/pathTime/
// pheromone are reconstructed locally — ADR-0009 — and not serialized).
constexpr std::size_t kFixedSize = 1 + 1 + 4 + 4 + 4 + 8 + 8 + 4;
constexpr std::size_t kCountFields = 2 + 2 + 2;  // visited, history, hello counts

bool validType(std::uint8_t v) {
    return v == static_cast<std::uint8_t>(AntType::Hello) ||
           v == static_cast<std::uint8_t>(AntType::Reactive) ||
           v == static_cast<std::uint8_t>(AntType::Proactive) ||
           v == static_cast<std::uint8_t>(AntType::Repair) ||
           v == static_cast<std::uint8_t>(AntType::LinkFail);
}

bool validDirection(std::uint8_t v) {
    return v == static_cast<std::uint8_t>(AntDirection::Up) ||
           v == static_cast<std::uint8_t>(AntDirection::Down);
}

}  // namespace

std::size_t serializedSize(const AntMessage& msg) {
    return kVersionSize + kFixedSize + kCountFields + msg.visited.size() * kHopSize +
           msg.history.size() * kHopSize + msg.helloDests.size() * kHelloSize +
           (msg.hasKinematics ? kKinematicsSize : 0);
}

void serialize(const AntMessage& msg, std::vector<std::uint8_t>& out) {
    out.clear();
    swarm::core::WireWriter w(out);
    w.reserve(serializedSize(msg));  // one allocation per frame (#655)

    w.version(kWireVersion);
    w.u8(static_cast<std::uint8_t>(msg.type));
    w.u8(static_cast<std::uint8_t>(msg.direction));
    w.i32(msg.src);
    w.i32(msg.dst);
    w.u32(msg.seqNum);
    w.f64(msg.timeStart);
    w.f64(msg.lifeAnt);
    w.i32(msg.broadcastBudget);

    w.u16(static_cast<std::uint16_t>(msg.visited.size()));
    for (const AntHop& h : msg.visited) {
        w.i32(h.node);
        w.f64(h.time);
    }
    w.u16(static_cast<std::uint16_t>(msg.history.size()));
    for (const AntHop& h : msg.history) {
        w.i32(h.node);
        w.f64(h.time);
    }
    w.u16(static_cast<std::uint16_t>(msg.helloDests.size()));
    for (const HelloDest& d : msg.helloDests) {
        w.i32(d.node);
        w.f64(d.pheromone);
    }
    if (msg.hasKinematics) {
        const Kinematics& k = msg.kinematics;
        w.u8(kKinematicsTag);
        for (double v : {k.x, k.y, k.z, k.vx, k.vy, k.vz}) w.f32(static_cast<float>(v));
    }
}

std::vector<std::uint8_t> serialize(const AntMessage& msg) {
    std::vector<std::uint8_t> out;
    serialize(msg, out);
    return out;
}

bool deserialize(const std::uint8_t* bytes, std::size_t len, AntMessage& msg) {
    swarm::core::WireReader r({bytes, len});

    // Trust boundary: reject foreign/stale frames and invalid enums in O(1)
    // before touching any length-prefixed section.
    if (r.remaining() < kVersionSize || !r.expectVersion(kWireVersion)) return false;
    if (r.remaining() < kFixedSize + 2) return false;  // fixed fields + visited count
    const std::uint8_t typeByte = r.u8();
    const std::uint8_t dirByte = r.u8();
    if (!validType(typeByte) || !validDirection(dirByte)) return false;
    msg.type = static_cast<AntType>(typeByte);
    msg.direction = static_cast<AntDirection>(dirByte);
    msg.src = r.i32();
    msg.dst = r.i32();
    msg.seqNum = r.u32();
    msg.timeStart = r.f64();
    msg.lifeAnt = r.f64();
    msg.broadcastBudget = static_cast<int>(r.i32());

    // Each section: its count, then one check that the entries and the next
    // section's count are all there.
    const std::uint16_t nVisited = r.u16();
    if (nVisited > kMaxVisitedOnWire ||
        r.remaining() < static_cast<std::size_t>(nVisited) * kHopSize + 2)
        return false;
    msg.visited.clear();
    for (std::uint16_t k = 0; k < nVisited; ++k) {
        const NodeAddress node = r.i32();  // named: argument order is unspecified
        const Time time = r.f64();
        msg.visited.push(node, time);
    }

    const std::uint16_t nHistory = r.u16();
    if (nHistory > kMaxHistoryOnWire ||
        r.remaining() < static_cast<std::size_t>(nHistory) * kHopSize + 2)
        return false;
    msg.history.clear();
    for (std::uint16_t k = 0; k < nHistory; ++k) {
        const NodeAddress node = r.i32();
        const Time time = r.f64();
        msg.history.push(node, time);
    }

    const std::uint16_t nHello = r.u16();
    if (nHello > kMaxHelloOnWire || r.remaining() < static_cast<std::size_t>(nHello) * kHelloSize)
        return false;
    msg.helloDests.resize(nHello);
    for (auto& d : msg.helloDests) {
        d.node = r.i32();
        d.pheromone = r.f64();
    }

    // ADR-0025: anything after the adverts must be one well-formed kinematics
    // section on a hello. A wrong tag, a short section, a non-finite value,
    // trailing bytes, or a section on any other ant type rejects the frame.
    msg.hasKinematics = false;
    msg.kinematics = Kinematics{};
    if (r.remaining() > 0) {
        if (msg.type != AntType::Hello || r.remaining() != kKinematicsSize ||
            r.u8() != kKinematicsTag)
            return false;
        double v[6];
        for (double& x : v) {
            x = static_cast<double>(r.f32());
            if (!std::isfinite(x)) return false;
        }
        msg.kinematics = Kinematics{v[0], v[1], v[2], v[3], v[4], v[5]};
        msg.hasKinematics = true;
    }

    return r.ok();
}

}  // namespace codec
}  // namespace core
}  // namespace anthocnet
