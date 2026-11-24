// Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
// SPDX-License-Identifier: GPL-2.0-only

// The livelock watch (--livelockEvents); see livelock-watch.h.

#include "livelock-watch.h"

#include "ns3/global-value.h"
#include "ns3/map-scheduler.h"
#include "ns3/object-factory.h"
#include "ns3/simulator.h"
#include "ns3/type-id.h"

namespace ns3 {
namespace bench {

namespace {

// One simulation runs at a time in a process, and Simulator::SetScheduler
// builds the scheduler from a factory, so the limit and the report live here.
uint64_t g_limit = 0;
LivelockReport g_report;

}  // namespace

NS_OBJECT_ENSURE_REGISTERED(LivelockScheduler);

TypeId LivelockScheduler::GetTypeId() {
    static TypeId tid = TypeId("ns3::bench::LivelockScheduler")
                            .SetParent<Scheduler>()
                            .SetGroupName("RoutingBench")
                            .AddConstructor<LivelockScheduler>();
    return tid;
}

LivelockScheduler::LivelockScheduler() {
    // The scheduler the simulator would have built (--SchedulerType honoured),
    // never this one again.
    TypeIdValue type;
    GlobalValue::GetValueByName("SchedulerType", type);
    TypeId inner = type.Get();
    if (inner == GetTypeId()) inner = MapScheduler::GetTypeId();
    ObjectFactory factory;
    factory.SetTypeId(inner);
    m_inner = factory.Create<Scheduler>();
}

LivelockScheduler::~LivelockScheduler() = default;

void LivelockScheduler::Insert(const Event& ev) {
    m_inner->Insert(ev);
}

bool LivelockScheduler::IsEmpty() const {
    return m_inner->IsEmpty();
}

Scheduler::Event LivelockScheduler::PeekNext() const {
    return m_inner->PeekNext();
}

Scheduler::Event LivelockScheduler::RemoveNext() {
    const Event ev = m_inner->RemoveNext();
    if (ev.key.m_ts != m_ts) {
        m_ts = ev.key.m_ts;
        m_atTs = 0;
    }
    ++m_atTs;
    if (m_atTs > g_report.maxAtTs) g_report.maxAtTs = m_atTs;
    if (m_atTs == g_limit && !g_report.fired) {
        g_report.fired = true;
        g_report.at = TimeStep(m_ts);
        g_report.events = m_atTs;
        Simulator::Stop();  // this event still runs; Run() returns after it
    }
    return ev;
}

void LivelockScheduler::Remove(const Event& ev) {
    m_inner->Remove(ev);
}

void WatchForLivelock(uint64_t limit) {
    g_limit = limit;
    g_report = LivelockReport{};
    if (limit == 0) return;
    ObjectFactory factory;
    factory.SetTypeId(LivelockScheduler::GetTypeId());
    Simulator::SetScheduler(factory);  // moves the events already scheduled
}

LivelockReport LivelockStatus() {
    return g_report;
}

}  // namespace bench
}  // namespace ns3
