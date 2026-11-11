// SPDX-License-Identifier: MIT OR GPL-2.0-only
// Copyright (C) 2026 Daniel Henrique Joppi

#include "anthocnet/core/ant_message_codec.h"

#include <cmath>
#include <cstring>

namespace anthocnet {
namespace core {
namespace codec {

namespace {

// Little-endian primitive writers/readers. Kept explicit (rather than a raw
// struct memcpy) so the format is portable across compilers/endianness and
// stable between the two adapters.

// The encoder sizes the buffer once and writes through a cursor (#655): one
// capacity check per frame instead of one push_back per byte, which made
// encoding ~34x slower than decoding. Same little-endian layout, so the bytes
// on the wire are unchanged.
struct Writer {
    std::uint8_t* p;

#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    // On a little-endian host the wire layout is the in-memory layout, so a
    // field is one memcpy. Elsewhere the shifts below produce the same bytes.
    template <typename T>
    void raw(T v) {
        std::memcpy(p, &v, sizeof(T));
        p += sizeof(T);
    }
    void u8(std::uint8_t v) { *p++ = v; }
    void u16(std::uint16_t v) { raw(v); }
    void u32(std::uint32_t v) { raw(v); }
    void i32(std::int32_t v) { raw(v); }
    void dbl(double v) { raw(v); }
#else

    void u8(std::uint8_t v) { *p++ = v; }
    void u16(std::uint16_t v) {
        p[0] = static_cast<std::uint8_t>(v & 0xFF);
        p[1] = static_cast<std::uint8_t>((v >> 8) & 0xFF);
        p += 2;
    }
    void u32(std::uint32_t v) {
        for (int i = 0; i < 4; ++i) p[i] = static_cast<std::uint8_t>((v >> (8 * i)) & 0xFF);
        p += 4;
    }
    void i32(std::int32_t v) { u32(static_cast<std::uint32_t>(v)); }
    void dbl(double v) {
        std::uint64_t bits;
        std::memcpy(&bits, &v, sizeof(bits));
        for (int i = 0; i < 8; ++i) p[i] = static_cast<std::uint8_t>((bits >> (8 * i)) & 0xFF);
        p += 8;
    }
#endif
};

// The decoder checks bounds once per section (#918), against the fixed header
// and then each length-prefixed section, and reads the fields through an
// unchecked cursor: one memcpy per field on a little-endian host, instead of a
// bounds check and a byte loop per field. Same layout, same accepted frames.
struct Reader {
    const std::uint8_t* p;
    std::size_t remaining;

    void skip(std::size_t n) {
        p += n;
        remaining -= n;
    }
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    template <typename T>
    T raw() {
        T v;
        std::memcpy(&v, p, sizeof(T));
        skip(sizeof(T));
        return v;
    }
    std::uint16_t u16() { return raw<std::uint16_t>(); }
    std::uint32_t u32() { return raw<std::uint32_t>(); }
    double dbl() { return raw<double>(); }
#else
    std::uint16_t u16() {
        std::uint16_t v =
            static_cast<std::uint16_t>(p[0]) | (static_cast<std::uint16_t>(p[1]) << 8);
        skip(2);
        return v;
    }
    std::uint32_t u32() {
        std::uint32_t v = 0;
        for (int i = 0; i < 4; ++i) v |= static_cast<std::uint32_t>(p[i]) << (8 * i);
        skip(4);
        return v;
    }
    double dbl() {
        std::uint64_t bits = 0;
        for (int i = 0; i < 8; ++i) bits |= static_cast<std::uint64_t>(p[i]) << (8 * i);
        skip(8);
        double v;
        std::memcpy(&v, &bits, sizeof(v));
        return v;
    }
#endif
    std::uint8_t u8() {
        const std::uint8_t v = *p;
        skip(1);
        return v;
    }
    std::int32_t i32() { return static_cast<std::int32_t>(u32()); }
    float f32() {
        const std::uint32_t bits = u32();
        float v;
        std::memcpy(&v, &bits, sizeof(v));
        return v;
    }
};

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
    out.resize(serializedSize(msg));
    Writer w{out.data()};

    w.u8(kWireVersion);
    w.u8(static_cast<std::uint8_t>(msg.type));
    w.u8(static_cast<std::uint8_t>(msg.direction));
    w.i32(msg.src);
    w.i32(msg.dst);
    w.u32(msg.seqNum);
    w.dbl(msg.timeStart);
    w.dbl(msg.lifeAnt);
    w.i32(msg.broadcastBudget);

    w.u16(static_cast<std::uint16_t>(msg.visited.size()));
    for (const AntHop& h : msg.visited) {
        w.i32(h.node);
        w.dbl(h.time);
    }
    w.u16(static_cast<std::uint16_t>(msg.history.size()));
    for (const AntHop& h : msg.history) {
        w.i32(h.node);
        w.dbl(h.time);
    }
    w.u16(static_cast<std::uint16_t>(msg.helloDests.size()));
    for (const HelloDest& d : msg.helloDests) {
        w.i32(d.node);
        w.dbl(d.pheromone);
    }
    if (msg.hasKinematics) {
        const Kinematics& k = msg.kinematics;
        w.u8(kKinematicsTag);
        for (double v : {k.x, k.y, k.z, k.vx, k.vy, k.vz}) {
            const float f = static_cast<float>(v);
            std::uint32_t bits;
            std::memcpy(&bits, &f, sizeof(bits));
            w.u32(bits);
        }
    }
}

std::vector<std::uint8_t> serialize(const AntMessage& msg) {
    std::vector<std::uint8_t> out;
    serialize(msg, out);
    return out;
}

bool deserialize(const std::uint8_t* bytes, std::size_t len, AntMessage& msg) {
    Reader r{bytes, len};

    // Trust boundary: reject foreign/stale frames and invalid enums in O(1)
    // before touching any length-prefixed section.
    if (r.remaining < kVersionSize || r.u8() != kWireVersion) return false;
    if (r.remaining < kFixedSize + 2) return false;  // fixed fields + visited count
    const std::uint8_t typeByte = r.u8();
    const std::uint8_t dirByte = r.u8();
    if (!validType(typeByte) || !validDirection(dirByte)) return false;
    msg.type = static_cast<AntType>(typeByte);
    msg.direction = static_cast<AntDirection>(dirByte);
    msg.src = r.i32();
    msg.dst = r.i32();
    msg.seqNum = r.u32();
    msg.timeStart = r.dbl();
    msg.lifeAnt = r.dbl();
    msg.broadcastBudget = static_cast<int>(r.i32());

    // Each section: its count, then one check that the entries and the next
    // section's count are all there.
    const std::uint16_t nVisited = r.u16();
    if (nVisited > kMaxVisitedOnWire ||
        r.remaining < static_cast<std::size_t>(nVisited) * kHopSize + 2)
        return false;
    msg.visited.resize(nVisited);
    for (auto& h : msg.visited) {
        h.node = r.i32();
        h.time = r.dbl();
    }

    const std::uint16_t nHistory = r.u16();
    if (nHistory > kMaxHistoryOnWire ||
        r.remaining < static_cast<std::size_t>(nHistory) * kHopSize + 2)
        return false;
    msg.history.resize(nHistory);
    for (auto& h : msg.history) {
        h.node = r.i32();
        h.time = r.dbl();
    }

    const std::uint16_t nHello = r.u16();
    if (nHello > kMaxHelloOnWire || r.remaining < static_cast<std::size_t>(nHello) * kHelloSize)
        return false;
    msg.helloDests.resize(nHello);
    for (auto& d : msg.helloDests) {
        d.node = r.i32();
        d.pheromone = r.dbl();
    }

    // ADR-0025: anything after the adverts must be one well-formed kinematics
    // section on a hello. A wrong tag, a short section, a non-finite value,
    // trailing bytes, or a section on any other ant type rejects the frame.
    msg.hasKinematics = false;
    msg.kinematics = Kinematics{};
    if (r.remaining > 0) {
        if (msg.type != AntType::Hello || r.remaining != kKinematicsSize ||
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

    return true;
}

}  // namespace codec
}  // namespace core
}  // namespace anthocnet
