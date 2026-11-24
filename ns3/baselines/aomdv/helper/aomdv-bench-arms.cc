// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2026 Daniel Henrique Joppi

/*
 * AOMDV as an arm of the comparison harness (ADR-0027, #768), compiled in
 * only when routing-bench is installed beside this module. The vendored
 * multipath baseline (#296); not in the default protocol list, it joins a
 * comparison when named.
 */

#include "ns3/aomdv-helper.h"
#include "ns3/aomdv-routing-protocol.h"
#include "ns3/icmp-error-guard-helper.h"
#include "ns3/routing-bench-arm.h"

namespace ns3 {
namespace {

// NS3-001 (#843, swarm-routing/sim-tools#238): AOMDV is ns-3's AODV with the
// multipath delta, and livelocks on ns-3.49 the same way, so it runs behind
// routing-bench's ICMP no-route guard (nsnam/ns-3-dev!3101's rules), as the
// stock aodv arm does. On 3.48 and earlier the guard has nothing to stop and
// the rows are byte-identical to unguarded AOMDV.
class AomdvArm : public bench::Arm {
public:
    std::string Name() const override { return "aomdv"; }
    void InstallRouting(InternetStackHelper& internet, bench::RunContext&) override {
        internet.SetRoutingHelper(m_guard);
    }
    // What AomdvHelper::AssignStreams does, in the same node order with the
    // same numbering; it casts Ipv4::GetRoutingProtocol(), which is now the
    // guard, so reach AOMDV through the node it is aggregated to.
    int64_t AssignStreams(bench::RunContext& run, int64_t stream) override {
        int64_t used = 0;
        for (auto it = run.nodes.Begin(); it != run.nodes.End(); ++it) {
            Ptr<aomdv::RoutingProtocol> aomdv = (*it)->GetObject<aomdv::RoutingProtocol>();
            NS_ABORT_MSG_IF(!aomdv, "expected aomdv::RoutingProtocol on this node (NS3-001 guard)");
            used += aomdv->AssignStreams(stream + used);
        }
        return used;
    }

private:
    bench::IcmpErrorGuardHelper m_guard{AomdvHelper()};
};

Ptr<bench::Arm> MakeAomdvArm() { return Create<AomdvArm>(); }

}  // namespace

namespace bench {
void RegisterArms_aomdv() {
    RegisterArm("aomdv", "AOMDV, multipath AODV (Marina & Das 2001), vendored (#296)",
                &MakeAomdvArm);
}
}  // namespace bench
}  // namespace ns3
