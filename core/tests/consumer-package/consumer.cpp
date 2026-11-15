// Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
// SPDX-License-Identifier: MIT OR GPL-2.0-only
//
// The scratch consumer of the installed anthocnet-core package: a codec
// roundtrip, so a function compiled into the installed library is linked.
#include "anthocnet/core/ant_message_codec.h"

#include <cstdio>

int main() {
    anthocnet::core::AntMessage m;
    const auto bytes = anthocnet::core::codec::serialize(m);
    anthocnet::core::AntMessage back;
    const bool ok = anthocnet::core::codec::deserialize(bytes, back);
    std::puts(ok ? "consumer: ok" : "consumer: FAILED");
    return ok ? 0 : 1;
}
