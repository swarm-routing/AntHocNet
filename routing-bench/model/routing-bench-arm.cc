// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2026 Daniel Henrique Joppi

#include "routing-bench-arm.h"

#include "ns3/abort.h"
#include "ns3/pointer.h"
#include "ns3/qos-txop.h"
#include "ns3/txop.h"
#include "ns3/wifi-helper.h"
#include "ns3/wifi-mac.h"
#include "ns3/wifi-net-device.h"
#include "ns3/wifi-phy.h"
#include "ns3/wifi-remote-station-manager.h"

namespace ns3 {
namespace bench {

bool ArmRegistry::Add(const std::string& name, const std::string& summary, ArmFactory make) {
    if (name.empty() || make == nullptr || m_arms.count(name) != 0) return false;
    m_arms.emplace(name, Entry{summary, make});
    return true;
}

Ptr<Arm> ArmRegistry::Make(const std::string& name) const {
    auto it = m_arms.find(name);
    return it == m_arms.end() ? nullptr : it->second.make();
}

bool ArmRegistry::Has(const std::string& name) const { return m_arms.count(name) != 0; }

std::vector<std::string> ArmRegistry::Names() const {
    std::vector<std::string> names;
    for (const auto& kv : m_arms) names.push_back(kv.first);
    return names;
}

std::string ArmRegistry::Summary(const std::string& name) const {
    auto it = m_arms.find(name);
    return it == m_arms.end() ? std::string() : it->second.summary;
}

ArmRegistry& Registry() {
    static ArmRegistry registry;
    return registry;
}

void RegisterArm(const std::string& name, const std::string& summary, ArmFactory make) {
    NS_ABORT_MSG_UNLESS(Registry().Add(name, summary, make),
                        "routing-bench: cannot register arm '"
                            << name << "' (empty name, null factory, or the name is taken)");
}

namespace {

// What WifiHelper::AssignStreams assigns one device up to ns-3.48, in its
// order: every PHY, every station manager, then the Txop (or the four QoS
// Txops). The MAC's own AssignStreams is left to the caller.
int64_t AssignPhyManagersTxops(const Ptr<WifiNetDevice>& wifi, int64_t stream) {
    int64_t cur = stream;
    for (const auto& phy : wifi->GetPhys()) cur += phy->AssignStreams(cur);
    for (const auto& manager : wifi->GetRemoteStationManagers()) {
        cur += manager->AssignStreams(cur);
    }
    Ptr<WifiMac> mac = wifi->GetMac();
    PointerValue ptr;
    if (!mac->GetQosSupported()) {
        mac->GetAttribute("Txop", ptr);
        cur += ptr.Get<Txop>()->AssignStreams(cur);
    } else {
        for (const char* ac : {"VO_Txop", "VI_Txop", "BE_Txop", "BK_Txop"}) {
            mac->GetAttribute(ac, ptr);
            cur += ptr.Get<QosTxop>()->AssignStreams(cur);
        }
    }
    return cur - stream;
}

}  // namespace

int64_t AssignWifiDeviceStreams(const NetDeviceContainer& devices, int64_t stream, int64_t spill) {
    int64_t cur = stream;
    int64_t own = spill;
    for (uint32_t i = 0; i < devices.GetN(); ++i) {
        Ptr<WifiNetDevice> wifi = DynamicCast<WifiNetDevice>(devices.Get(i));
        NS_ABORT_MSG_UNLESS(wifi, "AssignWifiDeviceStreams: device " << i << " is not wifi");
        cur += AssignPhyManagersTxops(wifi, cur);
        own += wifi->GetMac()->AssignStreams(own);
    }
    if (cur - stream == kAdhocDeviceStreams * static_cast<int64_t>(devices.GetN())) {
        return own - spill;
    }
    // Another release's PHY, managers or Txops: re-pin every device stream
    // from `spill` (SetStream again before anything is drawn), so none of
    // them lands where the caller pins the scenario.
    WifiHelper helper;
    return helper.AssignStreams(devices, spill);
}

}  // namespace bench
}  // namespace ns3
