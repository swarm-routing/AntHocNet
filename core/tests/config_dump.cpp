// SPDX-License-Identifier: MIT OR GPL-2.0-only
// Copyright (C) 2026 Daniel Henrique Joppi

/**
 * Prints every field of a default-constructed anthocnet::core::Config as one
 * JSON object, in declaration order: {"alpha": 0.7, ..., "maxHistory": 4096}.
 *
 * The values are the ones the compiler evaluated, so an initialiser written as
 * an expression, a constant or a conversion prints as the number it becomes.
 * Tools that need the core defaults read this instead of parsing config.h
 * with a regex (#793), such as the learn site's tuning bench
 * (config_fields.py). The doc comments are not in the binary, so such a tool
 * still reads them from config.h.
 *
 * The field list below is checked by the compiler. Config is an aggregate, so
 * fieldCount<Config>() counts its members, and a static_assert fails the build
 * when a field is added to or removed from Config but not from the list. A
 * renamed field fails to compile.
 *
 * Usage: config_dump            (built by `make test` as core/build/tests/config_dump)
 */
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>

#include "anthocnet/core/config.h"

using anthocnet::core::Config;

namespace {

// The X-list of every Config field, in declaration order.
#define ANTHOCNET_CONFIG_FIELDS(X) \
    X(alpha)                       \
    X(hopCountAlpha)               \
    X(gamma)                       \
    X(betaAnts)                    \
    X(betaData)                    \
    X(hopTimeSec)                  \
    X(enableMacMetric)             \
    X(sinrCriticalDb)              \
    X(sinrPenaltyCost)             \
    X(energyCostFloor)             \
    X(minPheromone)                \
    X(enableEvaporation)           \
    X(evaporationInterval)         \
    X(enableAdaptiveEvaporation)   \
    X(adaptiveEvapRefChurn)        \
    X(adaptiveEvapMin)             \
    X(adaptiveEvapMax)             \
    X(adaptiveEvapTau)             \
    X(reactiveRetryInterval)       \
    X(enableProactive)             \
    X(enableDiffusion)             \
    X(maxHelloAdverts)             \
    X(enableReactive)              \
    X(enableRepair)                \
    X(enableLinkFail)              \
    X(enableDirectedReactive)      \
    X(shareKinematics)             \
    X(enableLinkLifetime)          \
    X(linkLifetimeRange)           \
    X(linkLifetimeHorizon)         \
    X(linkLifetimeFloor)           \
    X(enableQuietMode)             \
    X(quietTolerance)              \
    X(quietBackoffFactor)          \
    X(quietMaxBackoff)             \
    X(proactiveBroadcastProb)      \
    X(sessionTtl)                  \
    X(proactiveVirtualMargin)      \
    X(helloInterval)               \
    X(proactiveInterval)           \
    X(lifeAnt)                     \
    X(allowedHelloLoss)            \
    X(repairMaxBroadcasts)         \
    X(linkfailNotifyInterval)      \
    X(txFailureThreshold)          \
    X(reactiveMaxBroadcasts)       \
    X(enableMultipath)             \
    X(antAcceptanceFactor)         \
    X(antAcceptanceFactorNewHop)   \
    X(proactiveMaxBroadcasts)      \
    X(repairWaitFactor)            \
    X(repairTimeout)               \
    X(networkDiameter)             \
    X(maxPathLength)               \
    X(maxHistory)

// Number of members of the aggregate T: the largest N for which T{a1..aN}
// compiles, with each ai convertible to any member type.
struct AnyField {
    template <class T>
    operator T() const;
};

template <class T, class... A>
constexpr auto fieldCount(int) -> decltype(T{A{}..., AnyField{}}, std::size_t{});
template <class T, class... A>
constexpr std::size_t fieldCount(long) {
    return sizeof...(A);
}
template <class T, class... A>
constexpr auto fieldCount(int) -> decltype(T{A{}..., AnyField{}}, std::size_t{}) {
    return fieldCount<T, A..., AnyField>(0);
}

#define ANTHOCNET_COUNT_ONE(name) +1
constexpr std::size_t kListed = 0 ANTHOCNET_CONFIG_FIELDS(ANTHOCNET_COUNT_ONE);
#undef ANTHOCNET_COUNT_ONE

static_assert(fieldCount<Config>(0) == kListed,
              "config_dump's field list no longer matches core::Config: "
              "add or remove the field in ANTHOCNET_CONFIG_FIELDS");

// A double prints as the shortest decimal that reads back as the same value
// (written when the core was C++14, which has no std::to_chars), so 0.7 prints as 0.7 and not
// 0.69999999999999996. A whole number keeps its ".0", so a double field reads
// back as a float and an integer field as an int.
std::string number(double v) {
    char buf[32];
    if (v == static_cast<double>(static_cast<long long>(v)) && v > -1e15 && v < 1e15) {
        std::snprintf(buf, sizeof buf, "%.1f", v);
        return buf;
    }
    for (int precision = 1; precision <= 17; ++precision) {
        std::snprintf(buf, sizeof buf, "%.*g", precision, v);
        if (std::strtod(buf, nullptr) == v) break;
    }
    return buf;
}

std::string value(bool v) { return v ? "true" : "false"; }
std::string value(int v) { return std::to_string(v); }
std::string value(std::size_t v) { return std::to_string(v); }
std::string value(double v) { return number(v); }

}  // namespace

int main() {
    const Config c;
    const char* sep = "{";
#define ANTHOCNET_PRINT_FIELD(name)                             \
    std::cout << sep << "\n  \"" #name "\": " << value(c.name); \
    sep = ",";
    ANTHOCNET_CONFIG_FIELDS(ANTHOCNET_PRINT_FIELD)
#undef ANTHOCNET_PRINT_FIELD
    std::cout << "\n}\n";
    return 0;
}
