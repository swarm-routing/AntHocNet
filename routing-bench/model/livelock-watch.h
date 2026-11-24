// Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
// SPDX-License-Identifier: GPL-2.0-only

/*
 * The livelock watch (--livelockEvents): stop a run whose simulated time
 * stands still.
 *
 * NS3-001 (docs/ns3-bugs.md): ns-3.49's AODV, and the AOMDV/GPSR family
 * vendored beside it, can fall into a zero-time ICMP "net unreachable" storm:
 * millions of events at one simulated instant, so the run never reaches its
 * end and CI saw it only as a job timeout. A check scheduled for a later time
 * never runs in such a storm, so the watch sits where every event passes: it
 * is a Scheduler that forwards every call to the scheduler the simulator
 * would have used (the SchedulerType global value, MapScheduler by default)
 * and, in RemoveNext, counts the events taken at the current timestamp. When
 * that count reaches the limit it calls Simulator::Stop(): the event in hand
 * still runs, then Simulator::Run() returns, and the caller reads
 * LivelockStatus() and fails the run.
 *
 * It decides nothing else. Every ns-3 scheduler orders events by (timestamp,
 * uid), which the simulator assigns, so the wrapped scheduler hands out the
 * same events in the same order and a run the watch does not stop is the
 * run without it, byte for byte. The limit is an event count, never a wall
 * clock, so whether a run stops is itself deterministic for a seed.
 */
#ifndef ROUTING_BENCH_LIVELOCK_WATCH_H
#define ROUTING_BENCH_LIVELOCK_WATCH_H

#include "ns3/nstime.h"
#include "ns3/ptr.h"
#include "ns3/scheduler.h"

#include <cstdint>

namespace ns3 {
namespace bench {

/// The default --livelockEvents: far above the most events any scenario runs
/// at one instant (see the measurement in ns3/routing-bench/README.md).
constexpr uint64_t kDefaultLivelockEvents = 1000000;

/// The exit status of a harness run the watch stopped.
constexpr int kLivelockExit = 70;

/// The Scheduler decorator behind WatchForLivelock. Not for direct use.
class LivelockScheduler : public Scheduler {
public:
    static TypeId GetTypeId();
    LivelockScheduler();
    ~LivelockScheduler() override;

    void Insert(const Event& ev) override;
    bool IsEmpty() const override;
    Event PeekNext() const override;
    Event RemoveNext() override;
    void Remove(const Event& ev) override;

private:
    Ptr<Scheduler> m_inner;  ///< the scheduler that actually holds the events
    uint64_t m_ts = 0;       ///< the timestamp of the last event taken
    uint64_t m_atTs = 0;     ///< events taken at m_ts so far
};

/// Watch the current simulation: from now until Simulator::Destroy(), stop it
/// once `limit` events have run at one simulated instant. 0 does nothing.
/// Call it after the world is built, before Simulator::Run(); call it again
/// for the next simulation (Simulator::Destroy() drops the scheduler).
void WatchForLivelock(uint64_t limit);

/// What the watch saw in the current simulation.
struct LivelockReport {
    bool fired = false;    ///< the watch stopped the run
    Time at;               ///< the simulated time that stood still
    uint64_t events = 0;   ///< events run at `at` when it fired (the limit)
    uint64_t maxAtTs = 0;  ///< the most events seen at any one instant
};

LivelockReport LivelockStatus();

}  // namespace bench
}  // namespace ns3

#endif  // ROUTING_BENCH_LIVELOCK_WATCH_H
