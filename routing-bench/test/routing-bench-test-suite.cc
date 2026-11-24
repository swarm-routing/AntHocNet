// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2026 Daniel Henrique Joppi

/**
 * Unit tests for the routing-bench arm registry (ADR-0027, #766):
 * registration, lookup, the rejection of a duplicate or empty name, the
 * optional hooks' defaults, and the stock arms' names and stream-free hwmp
 * arm; that an arm's rows do not depend on the arms run before it
 * (--isolateArms, ns3-tools#198); and the ICMP no-route guard of the aodv arm
 * (NS3-001, #238): nsnam/ns-3-dev!3101's own test, and a partitioned AODV
 * network that livelocks on ns-3.49 without the guard and runs with it; and
 * that the scenario's streams do not follow how many the devices take
 * (swarm-routing/AntHocNet#1222).
 */

#include "ns3/adhoc-wifi-mac.h"
#include "ns3/aodv-helper.h"
#include "ns3/aodv-routing-protocol.h"
#include "ns3/boolean.h"
#include "ns3/icmp-error-guard-helper.h"
#include "ns3/icmpv4.h"
#include "ns3/inet-socket-address.h"
#include "ns3/internet-stack-helper.h"
#include "ns3/ipv4-address-helper.h"
#include "ns3/ipv4-l3-protocol.h"
#include "ns3/ipv4-raw-socket-factory.h"
#include "ns3/ipv4-static-routing-helper.h"
#include "ns3/ipv4.h"
#include "ns3/livelock-watch.h"
#include "ns3/random-variable-stream.h"
#include "ns3/routing-bench-arm.h"
#include "ns3/simple-channel.h"
#include "ns3/simple-net-device-helper.h"
#include "ns3/simple-net-device.h"
#include "ns3/simulator.h"
#include "ns3/stock-arms.h"
#include "ns3/test.h"
#include "ns3/udp-socket-factory.h"
#include "ns3/uinteger.h"
#include "ns3/wifi-helper.h"
#include "ns3/wifi-net-device.h"

#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

// TestSuite/TestCase enums became scoped (enum class) in ns-3.42 and the
// unscoped aliases were removed in ns-3.47 (the oracle module's pattern).
#ifdef ROUTING_BENCH_NS3_SCOPED_TEST_ENUMS
#define BENCH_TEST_TYPE_UNIT TestSuite::Type::UNIT
#define BENCH_TEST_QUICK TestCase::Duration::QUICK
#else
#define BENCH_TEST_TYPE_UNIT UNIT
#define BENCH_TEST_QUICK TestCase::QUICK
#endif

// Does this ns-3 have Ipv4L3Protocol::SendIcmpNoRoute (new in 3.49, NS3-001)?
// A non-numeric NS3_VER (3-dev) has it.
#if defined(ROUTING_BENCH_NS3_MINOR) && ROUTING_BENCH_NS3_MINOR < 49
constexpr bool kIcmpNoRoute = false;
#else
constexpr bool kIcmpNoRoute = true;
#endif

// A --coverage build's counters, written before the child's _exit (strong, and
// only in a coverage build: see model/arm-isolation.cc).
#ifdef ROUTING_BENCH_GCOV_DUMP
extern "C" void __gcov_dump();
#endif

namespace ns3 {
namespace bench {
namespace {

class FakeArm : public Arm {
public:
    std::string Name() const override { return "fake"; }
    void InstallRouting(InternetStackHelper&, RunContext&) override {}
    int64_t AssignStreams(RunContext&, int64_t) override { return 3; }
};

Ptr<Arm> MakeFakeArm() { return Create<FakeArm>(); }

// ns3-tools#198. Stock AODV, with a second interface on every node: a
// SimpleNetDevice alone on its own channel, so it carries nothing, but AODV
// opens sockets on it, and its RREQ loop draws one jitter per socket in the
// order of std::map<Ptr<Socket>, ...>, which is heap-address order. Which
// draw the Wi-Fi interface gets then depends on the heap: the leo-walker and
// isl-grid shape (swarm-routing/AntHocNet#1039, #362), on the harness's
// one-radio scenario.
class SpareIfAodvArm : public Arm {
public:
    std::string Name() const override { return "aodv-spare-if"; }
    void InstallRouting(InternetStackHelper& internet, RunContext&) override {
        internet.SetRoutingHelper(m_aodv);
    }
    int64_t AssignStreams(RunContext& run, int64_t stream) override {
        return m_aodv.AssignStreams(run.nodes, stream);
    }
    void AfterAddresses(RunContext& run, const Ipv4InterfaceContainer&) override {
        for (uint32_t i = 0; i < run.nodes.GetN(); ++i) {
            Ptr<Node> node = run.nodes.Get(i);
            Ptr<SimpleNetDevice> dev = CreateObject<SimpleNetDevice>();
            dev->SetChannel(CreateObject<SimpleChannel>());
            dev->SetAddress(Mac48Address::ConvertFrom(Mac48Address::Allocate()));
            node->AddDevice(dev);
            Ptr<Ipv4> ipv4 = node->GetObject<Ipv4>();
            const uint32_t ifIndex = ipv4->AddInterface(dev);
            ipv4->AddAddress(ifIndex,
                             Ipv4InterfaceAddress(Ipv4Address(0xac100001u + (i << 8)),
                                                  Ipv4Mask("255.255.255.0")));
            ipv4->SetUp(ifIndex);
        }
    }

private:
    AodvHelper m_aodv;
};

// ns3-tools#198: stock AODV that also leaves the heap in another state, the
// way an earlier arm's own allocations do (AntHocNet#1035's function-local
// static): it allocates blocks of many sizes, frees two in three and keeps
// the rest for the life of the process.
class HeapChurnArm : public Arm {
public:
    std::string Name() const override { return "heap-churn"; }
    void InstallRouting(InternetStackHelper& internet, RunContext&) override {
        static std::vector<char*> kept;
        std::vector<char*> blocks;
        for (uint32_t k = 0; k < 6000; ++k) blocks.push_back(new char[16 + (k % 256) * 8]);
        for (std::size_t k = 0; k < blocks.size(); ++k) {
            if (k % 3 == 0)
                kept.push_back(blocks[k]);
            else
                delete[] blocks[k];
        }
        internet.SetRoutingHelper(m_aodv);
    }
    int64_t AssignStreams(RunContext& run, int64_t stream) override {
        return m_aodv.AssignStreams(run.nodes, stream);
    }

private:
    AodvHelper m_aodv;
};

// NS3-002 for DSDV (#301). Stock DSDV, with the nodes also joined in a ring
// of wired links (SimpleNetDevice pairs, one SimpleChannel per link), so
// every node has three active interfaces. DSDV keeps one socket per
// interface in std::map<Ptr<Socket>, Ipv4InterfaceAddress> and sends each
// update on them in that map's order, heap-address order; it draws no random
// number inside those loops, so what the heap can change is the order of the
// sends, and with it the order of the simultaneous receptions they cause.
class RingDsdvArm : public Arm {
public:
    std::string Name() const override { return "dsdv-ring"; }
    void InstallRouting(InternetStackHelper& internet, RunContext& run) override {
        m_dsdv->InstallRouting(internet, run);
    }
    int64_t AssignStreams(RunContext& run, int64_t stream) override {
        return m_dsdv->AssignStreams(run, stream);
    }
    void AfterAddresses(RunContext& run, const Ipv4InterfaceContainer&) override {
        const uint32_t n = run.nodes.GetN();
        // Buffering off: with it, LookForQueuedPackets() uses the route to a
        // next hop without checking that LookupRoute() found one, and on these
        // nodes a next hop can be an address with no entry of its own, so a
        // queued packet goes out on an empty route (NS_ASSERT "interface >= 0"
        // in Ipv4L3Protocol::SendRealOut). That is NS3-003 (docs/ns3-bugs.md);
        // this test is about the send order.
        for (uint32_t k = 0; k < n; ++k) {
            run.nodes.Get(k)
                ->GetObject<Ipv4>()
                ->GetRoutingProtocol()
                ->SetAttribute("EnableBuffering", BooleanValue(false));
        }
        for (uint32_t k = 0; k < n; ++k) {
            Ptr<SimpleChannel> link = CreateObject<SimpleChannel>();
            for (uint32_t end = 0; end < 2; ++end) {
                Ptr<Node> node = run.nodes.Get((k + end) % n);
                Ptr<SimpleNetDevice> dev = CreateObject<SimpleNetDevice>();
                dev->SetChannel(link);
                dev->SetAddress(Mac48Address::ConvertFrom(Mac48Address::Allocate()));
                node->AddDevice(dev);
                Ptr<Ipv4> ipv4 = node->GetObject<Ipv4>();
                const uint32_t ifIndex = ipv4->AddInterface(dev);
                ipv4->AddAddress(ifIndex,
                                 Ipv4InterfaceAddress(Ipv4Address(0xac110001u + (k << 8) + end),
                                                      Ipv4Mask("255.255.255.0")));
                ipv4->SetUp(ifIndex);
            }
        }
    }

private:
    Ptr<Arm> m_dsdv = Registry().Make("dsdv");
};

// NS3-001's shape without ns-3's bug: from t = 2 s an event that schedules
// itself with zero delay forever, so simulated time never moves again and
// only the livelock watch ends the run.
void ZeroTimeStorm() {
    Simulator::ScheduleNow(&ZeroTimeStorm);
}

class ZeroTimeStormArm : public Arm {
public:
    std::string Name() const override { return "zero-time-storm"; }
    void InstallRouting(InternetStackHelper&, RunContext&) override {}
    int64_t AssignStreams(RunContext&, int64_t) override { return 0; }
    void AfterAddresses(RunContext&, const Ipv4InterfaceContainer&) override {
        Simulator::Schedule(Seconds(2), &ZeroTimeStorm);
    }
};

Ptr<Arm> MakeSpareIfAodvArm() { return Create<SpareIfAodvArm>(); }
Ptr<Arm> MakeZeroTimeStormArm() { return Create<ZeroTimeStormArm>(); }
Ptr<Arm> MakeRingDsdvArm() { return Create<RingDsdvArm>(); }
Ptr<Arm> MakeHeapChurnArm() { return Create<HeapChurnArm>(); }

// The harness run once per command line, each in a child process, every
// child forked from this process in the same state (#211). Every argument
// vector, temporary file and status slot is made before the first fork, and
// nothing is allocated between two forks, so each child enters RunHarness
// with the same heap whatever its arguments, as routing-compare does from
// main(), whose argv lives on the stack. --isolateArms forks its workers from
// the state RunHarness is entered with: when the test built one child's argv,
// read its output and only then forked the next, each later child started
// from another heap, and an arm's rows could differ between "alone" and
// "after other arms" for that reason alone (seen under the bare test runner
// and beside AntHocNet's arm modules, not under test.py).
struct ChildRun {
    std::string output;
    std::string errors;  // stderr, with `captureStderr` only
    int status = -1;
};

std::string Slurp(const char* path) {
    std::ifstream in(path);
    std::stringstream text;
    text << in.rdbuf();
    std::remove(path);
    return text.str();
}

std::vector<ChildRun> RunHarnessInChildren(const std::vector<std::vector<std::string>>& runs,
                                           bool captureStderr = false) {
    const std::size_t n = runs.size();
    std::vector<std::vector<std::string>> words(n);
    std::vector<std::vector<char*>> argvs(n);
    std::vector<std::array<char, 40>> paths(n);
    std::vector<std::array<char, 40>> errPaths(n);
    std::vector<int> fds(n, -1);
    std::vector<int> errFds(n, -1);
    std::vector<int> statuses(n, -1);
    for (std::size_t i = 0; i < n; ++i) {
        words[i].push_back("routing-compare");
        words[i].insert(words[i].end(), runs[i].begin(), runs[i].end());
        for (std::string& w : words[i]) argvs[i].push_back(w.data());
        argvs[i].push_back(nullptr);
        std::snprintf(paths[i].data(), paths[i].size(), "/tmp/routing-bench-isolation-XXXXXX");
        fds[i] = mkstemp(paths[i].data());
        NS_ABORT_MSG_IF(fds[i] < 0, "mkstemp failed");
        if (captureStderr) {
            std::snprintf(errPaths[i].data(), errPaths[i].size(),
                          "/tmp/routing-bench-stderr-XXXXXX");
            errFds[i] = mkstemp(errPaths[i].data());
            NS_ABORT_MSG_IF(errFds[i] < 0, "mkstemp failed");
        }
    }
    std::cout.flush();
    std::fflush(nullptr);
    // From here to the last waitpid: no allocation in this process.
    for (std::size_t i = 0; i < n; ++i) {
        const pid_t pid = fork();
        if (pid < 0) break;  // statuses[i] stays -1
        if (pid == 0) {
            dup2(fds[i], 1);
            if (errFds[i] >= 0) dup2(errFds[i], 2);
            const int rc =
                RunHarness(static_cast<int>(words[i].size()), argvs[i].data(), "routing-compare");
            std::cout.flush();
            std::fflush(nullptr);
#ifdef ROUTING_BENCH_GCOV_DUMP
            __gcov_dump();
#endif
            _exit(rc);
        }
        waitpid(pid, &statuses[i], 0);
    }
    std::vector<ChildRun> out(n);
    for (std::size_t i = 0; i < n; ++i) {
        close(fds[i]);
        out[i].output = Slurp(paths[i].data());
        if (errFds[i] >= 0) {
            close(errFds[i]);
            out[i].errors = Slurp(errPaths[i].data());
        }
        out[i].status = statuses[i];
    }
    return out;
}

// Every line that starts with `marker`.
std::string MarkerLines(const std::string& output, const std::string& marker) {
    std::istringstream in(output);
    std::string line;
    std::string lines;
    while (std::getline(in, line))
        if (line.rfind(marker, 0) == 0) lines += line + "\n";
    return lines;
}

// One arm's per-seed result rows (everything printed per arm and seed except
// ##PERF##, which is cost, not result).
std::string ArmRows(const std::string& output, const std::string& arm) {
    std::istringstream in(output);
    std::string line;
    std::string rows;
    for (const char* marker : {"##RUN## ", "##GOODPUT## ", "##ROUTE## ", "##HOLD## "}) {
        in.clear();
        in.seekg(0);
        while (std::getline(in, line)) {
            if (line.rfind(marker, 0) != 0) continue;
            std::istringstream f(line);
            std::string m, seed, name;
            f >> m >> seed >> name;
            if (name == arm) rows += line + "\n";
        }
    }
    return rows;
}

/// A measurement no hook may touch: every count is zero.
class NullMeasurement : public Measurement {
public:
    uint64_t ControlPackets() const override { return 0; }
    uint64_t ControlBytes() const override { return 0; }
    double DeliveredDataBytes() const override { return 0.0; }
    void TrackReinjectionIdentity() override {}
    void MacReinjectedData(const Ipv4Header&, Ptr<const Packet>) override {}
    void ArpReinjectedData(const Ipv4Header&, Ptr<const Packet>) override {}
    uint64_t ReinjectedData() const override { return 0; }
    void TrackPacketFate() override {}
    void TrackPendingQueue() override {}
    void MeshManagementEnqueue(Ptr<const BENCH_WIFI_MPDU>) override {}
    void MeshDataEnqueue(uint32_t, Ptr<const BENCH_WIFI_MPDU>) override {}
    uint64_t MeshPeeringFrames() const override { return 0; }
    uint64_t MeshBeacons() const override { return 0; }
    void TypedControlTx(uint8_t, uint8_t, bool) override {}
    void TypedControlRx(uint8_t, uint8_t) override {}
    uint64_t TypedControlTxCount(uint8_t) const override { return 0; }
    uint64_t TypedControlRxCount(uint8_t) const override { return 0; }
};

}  // namespace

class RegistryRulesTest : public TestCase {
public:
    RegistryRulesTest() : TestCase("arm registry: add, lookup, duplicate and empty names") {}

private:
    void DoRun() override {
        ArmRegistry r;
        NS_TEST_ASSERT_MSG_EQ(r.Names().empty(), true, "starts empty");
        NS_TEST_ASSERT_MSG_EQ(r.Add("fake", "a test arm", &MakeFakeArm), true, "first add");
        NS_TEST_ASSERT_MSG_EQ(r.Add("fake", "again", &MakeFakeArm), false, "duplicate refused");
        NS_TEST_ASSERT_MSG_EQ(r.Summary("fake"), "a test arm", "duplicate changed nothing");
        NS_TEST_ASSERT_MSG_EQ(r.Add("", "no name", &MakeFakeArm), false, "empty name refused");
        NS_TEST_ASSERT_MSG_EQ(r.Add("null", "no factory", nullptr), false, "null factory refused");
        NS_TEST_ASSERT_MSG_EQ(r.Names().size(), 1u, "one arm");

        NS_TEST_ASSERT_MSG_EQ(r.Has("fake"), true, "found");
        NS_TEST_ASSERT_MSG_EQ(r.Has("aodv"), false, "absent");
        NS_TEST_ASSERT_MSG_EQ(r.Make("aodv"), Ptr<Arm>(), "unknown name gives no arm");
        Ptr<Arm> a = r.Make("fake");
        Ptr<Arm> b = r.Make("fake");
        NS_TEST_ASSERT_MSG_NE(a, Ptr<Arm>(), "known name gives an arm");
        NS_TEST_ASSERT_MSG_NE(a, b, "a fresh arm for every run");
        RunContext run;
        NS_TEST_ASSERT_MSG_EQ(a->AssignStreams(run, 0), 3, "the arm's own hook");
    }
};

class DefaultHooksTest : public TestCase {
public:
    DefaultHooksTest()
        : TestCase("arm hooks: an arm that overrides none of the optional hooks changes nothing") {}

private:
    void DoRun() override {
        Ptr<Arm> arm = MakeFakeArm();
        RunContext run;
        NullMeasurement m;
        std::ostringstream out;
        arm->Configure(run);
        arm->Validate(run);
        arm->EmitConfig(out);
        NS_TEST_ASSERT_MSG_EQ(arm->ProvidesDevices(), false, "ad-hoc Wi-Fi by default");
        NetDeviceContainer radios;
        DeviceRequest req{};
        NS_TEST_ASSERT_MSG_EQ(arm->InstallDevices(run, req, radios).GetN(), 0u, "no devices");
        NS_TEST_ASSERT_MSG_EQ(radios.GetN(), 0u, "no radios");
        NS_TEST_ASSERT_MSG_EQ(arm->AssignDeviceStreams(run, 7), 0, "no device streams");
        arm->AfterStack(run);
        arm->ConnectTraces(run, m);
        arm->AfterAddresses(run, Ipv4InterfaceContainer());
        NS_TEST_ASSERT_MSG_EQ(arm->StartReplay(run, ReplayRequest(), nullptr, nullptr), false,
                              "no replay");
        arm->FinishReplay(1, 1, 100.0);
        RunCounts c;
        c.txPackets = 10;
        c.dropRoute = 2;
        arm->AdjustCounts(run, c);
        NS_TEST_ASSERT_MSG_EQ(c.txPackets, 10u, "counts untouched");
        NS_TEST_ASSERT_MSG_EQ(c.dropRoute, 2u, "counts untouched");
        arm->EmitRunNote(run, m, out);
        DropDetail d;
        arm->FillDropDetail(run, d);
        NS_TEST_ASSERT_MSG_EQ(d.holdCauses, false, "no drop detail");
        std::vector<HoldTime> hold;
        NS_TEST_ASSERT_MSG_EQ(arm->CollectHoldTimes(run, hold), false, "no pending queue");
        NS_TEST_ASSERT_MSG_EQ(hold.empty(), true, "no hold times");
        arm->AppendDiag(run, m, out);
        arm->EmitDiagnostics(run, m, 0.0, out);
        arm->Check(run, m);
        NS_TEST_ASSERT_MSG_EQ(out.str(), "", "the default hooks print nothing");
    }
};

class StockArmsTest : public TestCase {
public:
    StockArmsTest() : TestCase("stock arms: aodv, dsdv, hwmp and olsr register once") {}

private:
    void DoRun() override {
        // The process-wide registry: other suites in this binary may have
        // registered already, so register only if absent.
        if (!Registry().Has("aodv")) RegisterStockArms();
        const std::vector<std::string> want{"aodv", "dsdv", "hwmp", "olsr"};
        for (const auto& name : want) {
            NS_TEST_ASSERT_MSG_EQ(Registry().Has(name), true, name << " registered");
            Ptr<Arm> arm = Registry().Make(name);
            NS_TEST_ASSERT_MSG_EQ(arm->Name(), name, "the arm answers to its token");
        }
        // hwmp's streams are pinned with the mesh devices; static routing
        // takes none (#708).
        RunContext run;
        NS_TEST_ASSERT_MSG_EQ(Registry().Make("hwmp")->AssignStreams(run, 100), 0,
                              "hwmp takes no routing streams");
        // An olsr arm with no refusals leaves the counts alone.
        RunCounts c;
        c.txPackets = 10;
        c.dropRoute = 2;
        Registry().Make("olsr")->AdjustCounts(run, c);
        NS_TEST_ASSERT_MSG_EQ(c.txPackets, 10u, "no refusals, no change");
        NS_TEST_ASSERT_MSG_EQ(c.dropRoute, 2u, "no refusals, no change");
    }
};

class ArmIsolationTest : public TestCase {
public:
    ArmIsolationTest()
        : TestCase("isolateArms: an arm's rows do not depend on the arms run before it (#198)") {}

private:
    void DoRun() override {
        if (!Registry().Has("aodv")) RegisterStockArms();
        if (!Registry().Has("aodv-spare-if")) {
            RegisterArm("aodv-spare-if", "test: AODV with a second, idle interface",
                        &MakeSpareIfAodvArm);
        }
        if (!Registry().Has("heap-churn")) {
            RegisterArm("heap-churn", "test: AODV that reshapes the heap", &MakeHeapChurnArm);
        }
        if (!Registry().Has("dsdv-ring")) {
            RegisterArm("dsdv-ring", "test: DSDV with a wired ring as well", &MakeRingDsdvArm);
        }
        const std::vector<std::string> base{"--scenario=paper", "--nNodes=8", "--time=8",
                                            "--runs=2", "--perf=0"};
        const auto with = [&base](std::vector<std::string> extra) {
            std::vector<std::string> a = base;
            a.insert(a.end(), extra.begin(), extra.end());
            return a;
        };
        const std::vector<std::string> arms{"aodv-spare-if", "aodv", "olsr", "dsdv"};
        // hwmp, the fourth stock arm, brings its own 802.11s devices and needs
        // 802.11a (#708). Its multi-arm run spells -protocols with one dash, a
        // spelling CommandLine reads too: the worker replaces it.
        const std::vector<std::string> mesh{"--standard=80211a", "--rateManager=constant6"};
        auto meshArgs = [&with, &mesh](const std::string& protocols) {
            std::vector<std::string> a = with(mesh);
            a.push_back(protocols);
            return a;
        };
        // Every run is made up front and forked from one state (#211).
        std::vector<std::vector<std::string>> runs;
        // 0: every arm after another arm and after a reshaped heap, in one run.
        runs.push_back(with({"--protocols=heap-churn,olsr,aodv-spare-if,aodv,dsdv"}));
        for (const std::string& arm : arms) runs.push_back(with({"--protocols=" + arm}));  // 1-4
        runs.push_back(meshArgs("--protocols=hwmp"));                                      // 5
        runs.push_back(meshArgs("-protocols=heap-churn,aodv,hwmp"));                       // 6
        // 7, 8: the same comparison with --isolateArms=0, reported below.
        runs.push_back(with({"--isolateArms=0", "--protocols=aodv-spare-if"}));
        runs.push_back(with({"--isolateArms=0", "--protocols=heap-churn,aodv-spare-if"}));
        // 9, 10 (#218): the cross-arm rows of hwmp and olsr, isolated and in one
        // process. The hwmp worker numbers its address types in another order.
        runs.push_back(meshArgs("--protocols=hwmp,olsr"));
        runs.push_back(meshArgs("--protocols=hwmp,olsr"));
        runs.back().push_back("--isolateArms=0");
        // 11-13 (#301): dsdv-ring in one process, alone, after one heap-churn
        // arm and after it and two more arms: three levels of reshaping.
        runs.push_back(with({"--isolateArms=0", "--protocols=dsdv-ring"}));
        runs.push_back(with({"--isolateArms=0", "--protocols=heap-churn,dsdv-ring"}));
        runs.push_back(with({"--isolateArms=0", "--protocols=heap-churn,olsr,aodv,dsdv-ring"}));
        const std::vector<ChildRun> res = RunHarnessInChildren(runs);
        for (std::size_t i = 0; i < res.size(); ++i) {
            NS_TEST_ASSERT_MSG_EQ(res[i].status, 0, "run " << i << " exits cleanly");
        }
        const std::string& after = res[0].output;
        for (std::size_t k = 0; k < arms.size(); ++k) {
            const std::string& arm = arms[k];
            const std::string rows = ArmRows(res[1 + k].output, arm);
            NS_TEST_ASSERT_MSG_NE(rows.find("##RUN## 2 " + arm), std::string::npos,
                                  arm << " printed its rows");
            NS_TEST_ASSERT_MSG_EQ(ArmRows(after, arm), rows,
                                  arm << ": same rows alone and after other arms");
        }
        const std::string hwmpRows = ArmRows(res[5].output, "hwmp");
        NS_TEST_ASSERT_MSG_NE(hwmpRows.find("##RUN## 2 hwmp"), std::string::npos,
                              "hwmp printed its rows");
        NS_TEST_ASSERT_MSG_EQ(ArmRows(res[6].output, "hwmp"), hwmpRows,
                              "hwmp: same rows alone and after other arms");
        // #218: ##MATCH## and ##COMMON## span arms, so the parent compares
        // keys from different workers; they must be what one process prints.
        for (const char* marker : {"##MATCH## ", "##COMMON## "}) {
            const std::string iso = MarkerLines(res[9].output, marker);
            NS_TEST_ASSERT_MSG_NE(iso, "", "hwmp,olsr isolated printed " << marker << "rows");
            NS_TEST_ASSERT_MSG_EQ(iso, MarkerLines(res[10].output, marker),
                                  "hwmp,olsr: " << marker << "rows isolated = in one process");
        }
        // The same with the reshaping in the arm's own process is not this
        // test's business: --isolateArms isolates arms, not code. What it
        // fixes shows with it off: the aodv-spare-if rows after heap-churn
        // differ from the rows alone on glibc's allocator (not under ASan,
        // which does not reuse freed blocks soon), so that half is reported,
        // not asserted.
        const std::string& inAlone = res[7].output;
        const std::string& inAfter = res[8].output;
        std::clog << "isolateArms=0: aodv-spare-if after heap-churn "
                  << (ArmRows(inAlone, "aodv-spare-if") == ArmRows(inAfter, "aodv-spare-if")
                          ? "matches"
                          : "DIFFERS from")
                  << " its rows alone (#198)\n";
        const std::string dsdvAlone = ArmRows(res[11].output, "dsdv-ring");
        NS_TEST_ASSERT_MSG_NE(dsdvAlone.find("##RUN## 2 dsdv-ring"), std::string::npos,
                              "dsdv-ring printed its rows");
        std::clog << "isolateArms=0: dsdv-ring alone (#301):\n"
                  << MarkerLines(dsdvAlone, "##RUN## ");
        for (std::size_t i : {std::size_t{12}, std::size_t{13}}) {
            const std::string rows = ArmRows(res[i].output, "dsdv-ring");
            std::clog << "isolateArms=0: dsdv-ring after "
                      << (i == 12 ? "heap-churn" : "heap-churn,olsr,aodv") << " "
                      << (rows == dsdvAlone ? "matches" : "DIFFERS from")
                      << " its rows alone (#301)\n";
            if (rows != dsdvAlone) std::clog << "alone:\n" << dsdvAlone << "after:\n" << rows;
        }
    }
};

namespace {

// The router's Destination Unreachable messages, as the source's raw ICMP
// socket sees them.
struct IcmpSink {
    uint32_t unreach = 0;
    Ipv4Address from;

    void Receive(Ptr<Socket> s) {
        Address addr;
        while (Ptr<Packet> p = s->RecvFrom(0xffffffff, 0, addr)) {
            Ipv4Header ip;
            if (p->GetSize() < ip.GetSerializedSize()) continue;
            p->RemoveHeader(ip);
            if (ip.GetProtocol() != 1 || p->GetSize() < 4) continue;
            Icmpv4Header icmp;
            p->RemoveHeader(icmp);
            if (icmp.GetType() == Icmpv4Header::ICMPV4_DEST_UNREACH) {
                ++unreach;
                from = ip.GetSource();
            }
        }
    }
};

void SendIcmpOfType(Ptr<Socket> s, uint8_t type) {
    Ptr<Packet> p = Create<Packet>(8);
    Icmpv4Header icmp;
    icmp.SetType(type);
    icmp.SetCode(0);
    p->AddHeader(icmp);
    s->SendTo(p, 0, InetSocketAddress(Ipv4Address("10.9.9.9"), 0));
}

}  // namespace

// NS3-001: nsnam/ns-3-dev!3101's change to ns-3's ipv4-icmp-no-route test,
// with the router's routing (static, so any protocol does) behind the guard.
//   A (10.0.0.1) ---- (10.0.0.2) R (10.0.1.1) ---- (nothing)
// R forwards and has no route to 10.9.9.0/24. A sends one ICMP message of a
// type to 10.9.9.9: an Echo Request (8) is answered with one net unreachable
// from R, an ICMP error (3, 4, 5, 11, 12) with none. Before ns-3.49 there is
// no no-route error at all, so the Echo Request gets none either.
class IcmpNoRouteGuardTest : public TestCase {
public:
    IcmpNoRouteGuardTest()
        : TestCase("icmp guard: ns-3-dev!3101's no-route test, router guarded (NS3-001)") {}

private:
    void DoRun() override {
        for (uint8_t type : {8, 3, 4, 5, 11, 12}) RunType(type);
    }

    void RunType(uint8_t type) {
        Ptr<Node> a = CreateObject<Node>();
        Ptr<Node> r = CreateObject<Node>();
        SimpleNetDeviceHelper link;
        link.SetNetDevicePointToPointMode(true);
        NetDeviceContainer net0 = link.Install(NodeContainer(a, r));
        NetDeviceContainer net1 = link.Install(NodeContainer(r));

        InternetStackHelper plain;
        plain.Install(a);
        IcmpErrorGuardHelper guard{Ipv4StaticRoutingHelper()};
        InternetStackHelper guarded;
        guarded.SetRoutingHelper(guard);
        guarded.Install(r);

        auto addIf = [](Ptr<Node> n, Ptr<NetDevice> d, const char* addr) {
            Ptr<Ipv4> ipv4 = n->GetObject<Ipv4>();
            const uint32_t i = ipv4->AddInterface(d);
            ipv4->AddAddress(i, Ipv4InterfaceAddress(Ipv4Address(addr), Ipv4Mask("255.255.255.0")));
            ipv4->SetUp(i);
        };
        addIf(a, net0.Get(0), "10.0.0.1");
        addIf(r, net0.Get(1), "10.0.0.2");
        addIf(r, net1.Get(0), "10.0.1.1");
        r->GetObject<Ipv4>()->SetAttribute("IpForward", BooleanValue(true));
        Ipv4StaticRoutingHelper().GetStaticRouting(a->GetObject<Ipv4>())->SetDefaultRoute(
            Ipv4Address("10.0.0.2"), 1);

        IcmpSink sink;
        Ptr<SocketFactory> raw = a->GetObject<Ipv4RawSocketFactory>();
        Ptr<Socket> rx = raw->CreateSocket();
        rx->SetAttribute("Protocol", UintegerValue(1));
        rx->Bind(InetSocketAddress(Ipv4Address::GetAny(), 0));
        rx->SetRecvCallback(MakeCallback(&IcmpSink::Receive, &sink));
        Ptr<Socket> tx = raw->CreateSocket();
        tx->SetAttribute("Protocol", UintegerValue(1));
        tx->Bind(InetSocketAddress(Ipv4Address("10.0.0.1"), 0));

        Simulator::ScheduleWithContext(a->GetId(), Seconds(1), &SendIcmpOfType, tx, type);
        Simulator::Stop(Seconds(5));
        Simulator::Run();
        Simulator::Destroy();

        const IcmpErrorGuardCounters& k = guard.Counters();
        const bool echo = type == 8;
        NS_TEST_ASSERT_MSG_EQ(sink.unreach, (echo && kIcmpNoRoute) ? 1u : 0u,
                              "type " << +type << ": net unreachable messages A received");
        if (sink.unreach) {
            NS_TEST_ASSERT_MSG_EQ(sink.from, Ipv4Address("10.0.0.2"), "the error is R's");
        }
        // The guard sees the failure on every release; it has an error to stop
        // only where ns-3 builds one.
        NS_TEST_ASSERT_MSG_EQ(k.noRoute, 1u, "type " << +type << ": one no-route failure");
        NS_TEST_ASSERT_MSG_EQ(k.allowed, echo ? 1u : 0u, "type " << +type << ": allowed");
        NS_TEST_ASSERT_MSG_EQ(k.armed[IcmpErrorGuardCounters::ICMP_ERROR], echo ? 0u : 1u,
                              "type " << +type << ": classified as an ICMP error");
        NS_TEST_ASSERT_MSG_EQ(k.Vetoed(), (!echo && kIcmpNoRoute) ? 1u : 0u,
                              "type " << +type << ": errors the guard stopped");
    }
};

namespace {

// A partitioned AODV network, the NS3-001 trigger in miniature: nodes 0 and 1
// share one channel, node 2 is alone on its own, all in 10.1.1.0/24. Node 0
// sends 100 packets/s of UDP to node 1 (delivered) and as many to node 2 (no
// route ever): AODV's request queue fills, and on ns-3.49 each drop raises
// an ICMP error that AODV defers into the same full queue, at one instant.
// A sink on Ipv4L3Protocol's Drop trace stops the simulator once one
// instant has seen 10000 route-error drops (a livelock, as simulated time
// stands still), so the test ends either way.
struct PartitionRun {
    uint32_t delivered = 0;
    bool livelock = false;
    Time end;
    IcmpErrorGuardCounters counters;
};

struct LivelockWatch {
    Time at;
    uint32_t atNow = 0;
    bool fired = false;

    void Drop(const Ipv4Header&, Ptr<const Packet>, Ipv4L3Protocol::DropReason reason,
              Ptr<Ipv4>, uint32_t) {
        if (reason != Ipv4L3Protocol::DROP_ROUTE_ERROR) return;
        if (Simulator::Now() != at) {
            at = Simulator::Now();
            atNow = 0;
        }
        if (++atNow == 10000) {
            fired = true;
            Simulator::Stop();
        }
    }
};

void CountRx(uint32_t* n, Ptr<Socket> s) {
    while (s->Recv()) ++*n;
}

void SendUdp(Ptr<Socket> s, Ipv4Address to, uint32_t left) {
    s->SendTo(Create<Packet>(64), 0, InetSocketAddress(to, 9));
    if (left > 1) Simulator::Schedule(MilliSeconds(10), &SendUdp, s, to, left - 1);
}

PartitionRun RunPartition(bool guarded) {
    NodeContainer nodes;
    nodes.Create(3);
    SimpleNetDeviceHelper link;
    NetDeviceContainer devs = link.Install(NodeContainer(nodes.Get(0), nodes.Get(1)));
    devs.Add(link.Install(nodes.Get(2)));

    AodvHelper aodv;
    IcmpErrorGuardHelper guard{aodv};
    InternetStackHelper internet;
    if (guarded) {
        internet.SetRoutingHelper(guard);
    } else {
        internet.SetRoutingHelper(aodv);
    }
    internet.Install(nodes);
    Ipv4AddressHelper addresses("10.1.1.0", "255.255.255.0");
    Ipv4InterfaceContainer ifs = addresses.Assign(devs);
    for (uint32_t i = 0; i < nodes.GetN(); ++i) {
        nodes.Get(i)->GetObject<aodv::RoutingProtocol>()->AssignStreams(10 * i);
    }

    LivelockWatch watch;
    for (uint32_t i = 0; i < nodes.GetN(); ++i) {
        nodes.Get(i)->GetObject<Ipv4L3Protocol>()->TraceConnectWithoutContext(
            "Drop", MakeCallback(&LivelockWatch::Drop, &watch));
    }
    PartitionRun out;
    Ptr<Socket> rx = Socket::CreateSocket(nodes.Get(1), UdpSocketFactory::GetTypeId());
    rx->Bind(InetSocketAddress(Ipv4Address::GetAny(), 9));
    rx->SetRecvCallback(MakeBoundCallback(&CountRx, &out.delivered));
    Ptr<Socket> tx = Socket::CreateSocket(nodes.Get(0), UdpSocketFactory::GetTypeId());
    tx->Bind();
    for (uint32_t dst : {1u, 2u}) {
        Simulator::ScheduleWithContext(nodes.Get(0)->GetId(), Seconds(1), &SendUdp, tx,
                                       ifs.GetAddress(dst), 900u);
    }
    Simulator::Stop(Seconds(15));
    Simulator::Run();
    out.livelock = watch.fired;
    out.end = Simulator::Now();
    out.counters = guard.Counters();
    Simulator::Destroy();
    return out;
}

}  // namespace

class AodvPartitionGuardTest : public TestCase {
public:
    AodvPartitionGuardTest()
        : TestCase("icmp guard: partitioned AODV livelocks on ns-3.49 unguarded, runs guarded "
                   "(NS3-001)") {}

private:
    void DoRun() override {
        const PartitionRun bare = RunPartition(false);
        const PartitionRun guarded = RunPartition(true);
        const IcmpErrorGuardCounters& k = guarded.counters;
        std::clog << "NS3-001 partition: unguarded "
                  << (bare.livelock ? "livelocked" : "finished") << " at "
                  << bare.end.GetSeconds() << " s, delivered " << bare.delivered
                  << "; guarded finished at " << guarded.end.GetSeconds() << " s, delivered "
                  << guarded.delivered << ", no-route " << k.noRoute << ", vetoed "
                  << k.Vetoed() << " (source-own " << k.vetoed[IcmpErrorGuardCounters::SOURCE_OWN]
                  << "), unmatched " << k.unmatched << '\n';
#if defined(ROUTING_BENCH_NS3_MINOR) && ROUTING_BENCH_NS3_MINOR == 49
        // Strict, as ns3-containers' XFAIL: if 3.49 stops livelocking, this
        // test no longer proves the guard.
        NS_TEST_ASSERT_MSG_EQ(bare.livelock, true, "unguarded AODV livelocks on ns-3.49");
#elif defined(ROUTING_BENCH_NS3_MINOR) && ROUTING_BENCH_NS3_MINOR < 49
        NS_TEST_ASSERT_MSG_EQ(bare.livelock, false, "no SendIcmpNoRoute, no livelock");
#endif
        // Later releases only report the unguarded run: a release with the
        // fix stops livelocking (then retire the guard, NS3-001).
        NS_TEST_ASSERT_MSG_EQ(guarded.livelock, false, "guarded AODV does not livelock");
        NS_TEST_ASSERT_MSG_EQ(guarded.end, Seconds(15), "guarded AODV runs to the end");
        NS_TEST_ASSERT_MSG_GT(guarded.delivered, 800u, "the reachable flow is delivered");
        NS_TEST_ASSERT_MSG_GT(k.noRoute, 0u, "the unreachable flow met no-route failures");
        NS_TEST_ASSERT_MSG_EQ(k.allowed, 0u, "!3101 sends none of them");
        if (kIcmpNoRoute) {
            NS_TEST_ASSERT_MSG_EQ(k.Vetoed(), k.noRoute, "each error stopped");
            NS_TEST_ASSERT_MSG_EQ(k.unmatched, 0u, "every veto met its error");
        } else {
            NS_TEST_ASSERT_MSG_EQ(k.Vetoed(), 0u, "no error to stop before ns-3.49");
        }
    }
};

// AntHocNet#1222: an ad-hoc MAC that takes one stream more than the stock
// one, as ns-3.49's AdhocWifiMac took two more than 3.48's (IBSS beacons).
class ExtraStreamAdhocMac : public AdhocWifiMac {
public:
    static TypeId GetTypeId() {
        static TypeId tid = TypeId("ns3::bench::ExtraStreamAdhocMac")
                                .SetParent<AdhocWifiMac>()
                                .SetGroupName("RoutingBench")
                                .AddConstructor<ExtraStreamAdhocMac>();
        return tid;
    }
    int64_t AssignStreams(int64_t stream) override {
        m_extra->SetStream(stream);
        return 1 + AdhocWifiMac::AssignStreams(stream + 1);
    }
    Ptr<UniformRandomVariable> m_extra = CreateObject<UniformRandomVariable>();
};
NS_OBJECT_ENSURE_REGISTERED(ExtraStreamAdhocMac);

// The devices' streams: the stock ad-hoc PHY and Txop keep the running
// counter's reserved range, a MAC's own streams go to the device sub-block,
// and a device set that needs another count goes there whole (#1222).
class DeviceStreamLayoutTest : public TestCase {
public:
    DeviceStreamLayoutTest()
        : TestCase("device streams: the reserved range, the MAC's own, the fallback "
                   "(AntHocNet#1222)") {}

private:
    static constexpr uint32_t kNodes = 8;
    static constexpr int64_t kStream = 1000;  // the running counter at the devices
    static constexpr int64_t kSpill = 5000;   // the device sub-block

    struct Layout {
        int64_t spilled = 0;      // streams the devices took from kSpill
        int64_t helperCount = 0;  // what WifiHelper::AssignStreams counts for them
        int64_t extraStream = -1;
    };

    static Layout Assign(const std::string& macType, bool qos) {
        NodeContainer nodes;
        nodes.Create(kNodes);
        WifiHelper wifi;
        wifi.SetStandard(WIFI_STANDARD_80211b);
        ApplyRateManager(wifi, "constant2", "80211b");
        YansWifiChannelHelper channel = YansWifiChannelHelper::Default();
        YansWifiPhyHelper phy;
        phy.SetChannel(channel.Create());
        WifiMacHelper mac;
        mac.SetType(macType, "QosSupported", BooleanValue(qos));
        NetDeviceContainer devices = wifi.Install(phy, mac, nodes);
        Layout l;
        l.spilled = AssignWifiDeviceStreams(devices, kStream, kSpill);
        Ptr<WifiNetDevice> d0 = DynamicCast<WifiNetDevice>(devices.Get(0));
        if (auto extra = DynamicCast<ExtraStreamAdhocMac>(d0->GetMac())) {
            l.extraStream = extra->m_extra->GetStream();
        }
        // Counted last: WifiHelper::AssignStreams re-pins the devices.
        l.helperCount = WifiHelper().AssignStreams(devices, kStream);
        Simulator::Destroy();
        return l;
    }

    void DoRun() override {
        const int64_t n = kNodes;
        const Layout stock = Assign("ns3::AdhocWifiMac", false);
        const Layout extra = Assign("ns3::bench::ExtraStreamAdhocMac", false);
        // The QoS MAC's four Txops need another count than the reserved one,
        // as a future release's devices might.
        const Layout qos = Assign("ns3::AdhocWifiMac", true);
        // Stock: the PHY and Txop stay on the reserved range; only the MAC's
        // own streams (none before ns-3.49, two per device on 3.49) spill.
        NS_TEST_ASSERT_MSG_EQ(stock.spilled + kAdhocDeviceStreams * n, stock.helperCount,
                              "stock ad-hoc devices: reserved range + the MAC's own");
        NS_TEST_ASSERT_MSG_EQ(extra.spilled, stock.spilled + n,
                              "the extra MAC stream comes from the device sub-block");
        NS_TEST_ASSERT_MSG_EQ(extra.extraStream, kSpill, "device 0's extra stream");
        NS_TEST_ASSERT_MSG_GT(qos.helperCount, stock.helperCount, "the QoS MAC takes more");
        NS_TEST_ASSERT_MSG_EQ(qos.spilled, qos.helperCount,
                              "another count: every device stream from the sub-block");
    }
};

// The harness's placement and movement do not follow the devices' stream
// count (#1222: ns-3.49's ad-hoc MAC took two more streams per device than
// 3.48's, and the same seed drew another field). The dummy consumer is the
// QoS ad-hoc MAC (four Txops instead of one), set through the command line;
// the node movement --dumpTrace writes must be the same with and without it.
class PlacementStreamTest : public TestCase {
public:
    PlacementStreamTest()
        : TestCase("placement: the field does not follow the device stream count "
                   "(AntHocNet#1222)") {}

private:
    void DoRun() override {
        if (!Registry().Has("aodv")) RegisterStockArms();
        const std::string stem = "/tmp/routing-bench-placement-" + std::to_string(getpid());
        const std::string stock = stem + "-stock.tcl";
        const std::string more = stem + "-qos.tcl";
        const std::vector<std::string> base{"--nNodes=10", "--time=20", "--runs=1",
                                            "--firstRun=2", "--perf=0", "--protocols=aodv"};
        std::vector<std::vector<std::string>> runs{base, base};
        runs[0].push_back("--dumpTrace=" + stock);
        runs[1].push_back("--dumpTrace=" + more);
        runs[1].push_back("--ns3::WifiMac::QosSupported=true");
        const std::vector<ChildRun> res = RunHarnessInChildren(runs);
        auto slurp = [](const std::string& path) {
            std::ifstream in(path);
            std::stringstream text;
            text << in.rdbuf();
            std::remove(path.c_str());
            return text.str();
        };
        const std::string a = slurp(stock);
        const std::string b = slurp(more);
        for (std::size_t i = 0; i < res.size(); ++i) {
            NS_TEST_ASSERT_MSG_EQ(res[i].status, 0, "run " << i << " exits cleanly");
        }
        NS_TEST_ASSERT_MSG_NE(a.find("$node_(9) set X_"), std::string::npos,
                              "the trace places every node");
        NS_TEST_ASSERT_MSG_EQ(a == b, true,
                              "the device stream count moved the placement or the movement");
    }
};

namespace {

void Record(std::vector<uint32_t>* order, uint32_t k) {
    order->push_back(k);
}

// 300 events at each of three instants, scheduled in a scrambled order; what
// ran, in which order, and what the watch saw.
struct SameInstantRun {
    std::vector<uint32_t> order;
    LivelockReport report;
};

SameInstantRun RunSameInstant(uint64_t limit) {
    SameInstantRun out;
    for (uint32_t k = 0; k < 900; ++k) {
        const uint32_t at = (k * 7) % 3;
        Simulator::Schedule(Seconds(1 + at), &Record, &out.order, k);
    }
    WatchForLivelock(limit);
    Simulator::Run();
    out.report = LivelockStatus();
    Simulator::Destroy();
    return out;
}

}  // namespace

// NS3-001 showed in CI only as job timeouts. The livelock watch fails a run
// once --livelockEvents events have run at one simulated instant: in the
// simulator alone; through the harness, isolated and not, with the message CI
// logs show; and a run it does not stop prints the same bytes as without it.
class LivelockWatchTest : public TestCase {
public:
    LivelockWatchTest()
        : TestCase("livelock watch: a zero-time storm fails fast; other runs are unchanged "
                   "(NS3-001)") {}

private:
    void DoRun() override {
        // The simulator alone: the watch stops the storm at its limit, at the
        // instant it began.
        Simulator::Schedule(Seconds(1), &ZeroTimeStorm);
        WatchForLivelock(5000);
        Simulator::Run();
        const LivelockReport storm = LivelockStatus();
        NS_TEST_ASSERT_MSG_EQ(storm.fired, true, "the storm is stopped");
        NS_TEST_ASSERT_MSG_EQ(storm.at, Seconds(1), "at the instant time stood still");
        NS_TEST_ASSERT_MSG_EQ(storm.events, 5000u, "after exactly --livelockEvents events");
        NS_TEST_ASSERT_MSG_EQ(Simulator::Now(), Seconds(1), "and no later");
        Simulator::Destroy();

        // Below the limit: same events, same order, nothing stopped.
        const SameInstantRun off = RunSameInstant(0);
        const SameInstantRun on = RunSameInstant(300 + 1);
        NS_TEST_ASSERT_MSG_EQ(off.order.size(), 900u, "every event ran without the watch");
        NS_TEST_ASSERT_MSG_EQ(on.order == off.order, true, "the watch keeps the event order");
        NS_TEST_ASSERT_MSG_EQ(on.report.fired, false, "300 events at one instant pass 301");
        NS_TEST_ASSERT_MSG_EQ(on.report.maxAtTs, 300u, "the watch counted each instant");
        NS_TEST_ASSERT_MSG_EQ(RunSameInstant(300).report.fired, true, "and stop at 300");

        // Through the harness.
        if (!Registry().Has("aodv")) RegisterStockArms();
        if (!Registry().Has("zero-time-storm")) {
            RegisterArm("zero-time-storm", "test: a zero-time event storm from t = 2 s",
                        &MakeZeroTimeStormArm);
        }
        const std::vector<std::string> small{"--nNodes=8", "--time=8", "--runs=2", "--perf=0"};
        auto with = [&small](std::vector<std::string> extra) {
            std::vector<std::string> a = small;
            a.insert(a.end(), extra.begin(), extra.end());
            return a;
        };
        const std::vector<std::vector<std::string>> runs{
            // 0: isolated (the default): the worker fails, the parent names the arm.
            with({"--protocols=aodv,zero-time-storm", "--livelockEvents=100000"}),
            // 1: in one process, at the default limit.
            with({"--protocols=zero-time-storm", "--isolateArms=0"}),
            // 2, 3: the stock arms with the watch at its default and off.
            with({"--scenario=paper", "--protocols=aodv,olsr,dsdv"}),
            with({"--scenario=paper", "--protocols=aodv,olsr,dsdv", "--livelockEvents=0"}),
        };
        const auto t0 = std::chrono::steady_clock::now();
        const std::vector<ChildRun> res = RunHarnessInChildren(runs, true);
        const double wallS =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        std::clog << "livelock watch: four harness runs in " << wallS << " s\n"
                  << res[0].errors << res[1].errors;

        const std::string line =
            "routing-bench: livelock: arm 'zero-time-storm' seed 1: simulated time stuck at "
            "2.000000000 s for ";
        NS_TEST_ASSERT_MSG_NE(res[0].status, 0, "isolated: the run fails");
        NS_TEST_ASSERT_MSG_NE(res[0].errors.find(line + "100000 events (--livelockEvents=100000)"),
                              std::string::npos, "isolated: the worker's line");
        NS_TEST_ASSERT_MSG_NE(res[0].errors.find("docs/ns3-bugs.md"), std::string::npos,
                              "isolated: the pointer to the bug register");
        NS_TEST_ASSERT_MSG_NE(
            res[0].errors.find("arm 'zero-time-storm' failed in its own process (exit " +
                               std::to_string(kLivelockExit)),
            std::string::npos, "isolated: the parent names the arm");
        NS_TEST_ASSERT_MSG_NE(res[0].output.find("##RUN## 1 aodv"), std::string::npos,
                              "isolated: the arm before it printed its rows");
        NS_TEST_ASSERT_MSG_EQ(WIFEXITED(res[1].status) &&
                                  WEXITSTATUS(res[1].status) == kLivelockExit,
                              true, "one process: exit " << kLivelockExit);
        NS_TEST_ASSERT_MSG_NE(
            res[1].errors.find(line + std::to_string(kDefaultLivelockEvents) + " events"),
            std::string::npos, "one process: the line, at the default limit");
        NS_TEST_ASSERT_MSG_EQ(res[1].output.find("##RUN## "), std::string::npos,
                              "one process: no row for the stopped run");

        NS_TEST_ASSERT_MSG_EQ(res[2].status, 0, "stock arms, watch on: clean exit");
        NS_TEST_ASSERT_MSG_EQ(res[3].status, 0, "stock arms, watch off: clean exit");
        NS_TEST_ASSERT_MSG_NE(res[2].output.find("##RUN## 2 dsdv"), std::string::npos,
                              "stock arms printed their rows");
        NS_TEST_ASSERT_MSG_EQ(res[2].output, res[3].output,
                              "a run the watch does not stop is byte-identical");
    }
};

class RoutingBenchTestSuite : public TestSuite {
public:
    RoutingBenchTestSuite() : TestSuite("routing-bench", BENCH_TEST_TYPE_UNIT) {
        AddTestCase(new RegistryRulesTest, BENCH_TEST_QUICK);
        AddTestCase(new DefaultHooksTest, BENCH_TEST_QUICK);
        AddTestCase(new StockArmsTest, BENCH_TEST_QUICK);
        AddTestCase(new ArmIsolationTest, BENCH_TEST_QUICK);
        AddTestCase(new IcmpNoRouteGuardTest, BENCH_TEST_QUICK);
        AddTestCase(new AodvPartitionGuardTest, BENCH_TEST_QUICK);
        AddTestCase(new DeviceStreamLayoutTest, BENCH_TEST_QUICK);
        AddTestCase(new PlacementStreamTest, BENCH_TEST_QUICK);
        AddTestCase(new LivelockWatchTest, BENCH_TEST_QUICK);
    }
};

static RoutingBenchTestSuite g_routingBenchTestSuite;

}  // namespace bench
}  // namespace ns3
