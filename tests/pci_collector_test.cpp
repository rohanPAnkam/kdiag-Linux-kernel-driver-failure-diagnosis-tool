// Verifies PCI driver/link extraction and preservation of the full lspci dump.
#include "kdiag/collectors/pci_collector.hpp"

#include <algorithm>

#include <gtest/gtest.h>

#include "fake_command_runner.hpp"

namespace kdiag {
namespace {

constexpr char kSampleLspci[] =
    "03:00.0 Multimedia controller: Example Corp Camera Device\n"
    "\tSubsystem: Example Corp Camera Device\n"
    "\tKernel driver in use: driver_name\n"
    "\tKernel modules: driver_name\n"
    "\tCapabilities: [80] Express (v2) Endpoint\n"
    "\t\tLnkCap:\tPort #0, Speed 8GT/s, Width x4\n"
    "\t\tLnkSta:\tSpeed 2.5GT/s (downgraded), Width x1 (downgraded)\n"
    "\tCapabilities: [100] Advanced Error Reporting\n";

TEST(PciCollectorTest, ExtractsKernelDriverInUse) {
    testing::FakeCommandRunner runner;
    PciCollector collector(runner, "0000:03:00.0");

    const auto events = collector.ParseOutput(kSampleLspci);

    const auto it = std::find_if(events.begin(), events.end(),
                                  [](const Event& e) { return e.event == "pci_kernel_driver"; });
    ASSERT_NE(it, events.end());
    EXPECT_NE(it->message.find("driver_name"), std::string::npos);
}

TEST(PciCollectorTest, DetectsDowngradedLinkSpeedInLnkSta) {
    testing::FakeCommandRunner runner;
    PciCollector collector(runner, "0000:03:00.0");

    const auto events = collector.ParseOutput(kSampleLspci);

    const auto it = std::find_if(events.begin(), events.end(),
                                  [](const Event& e) { return e.event == "pci_link_status"; });
    ASSERT_NE(it, events.end());
    EXPECT_NE(it->message.find("downgraded"), std::string::npos);
}

TEST(PciCollectorTest, KeepsFullDumpAsSeparateEvidenceEvent) {
    testing::FakeCommandRunner runner;
    PciCollector collector(runner, "0000:03:00.0");

    const auto events = collector.ParseOutput(kSampleLspci);

    ASSERT_FALSE(events.empty());
    EXPECT_EQ(events.front().event, "pci_verbose_dump");
    EXPECT_EQ(events.front().raw, kSampleLspci);
}

TEST(PciCollectorTest, EmptyOutputProducesNoEvents) {
    testing::FakeCommandRunner runner;
    PciCollector collector(runner, "0000:03:00.0");

    EXPECT_TRUE(collector.ParseOutput("").empty());
}

TEST(PciCollectorTest, CollectFailureProducesCollectorErrorEvent) {
    testing::FakeCommandRunner runner;
    runner.SetResponse({"lspci", "-vvv", "-s", "0000:03:00.0"},
                       "lspci: Permission denied", 1);
    PciCollector collector(runner, "0000:03:00.0");

    const auto events = collector.Collect();

    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].event, "collector_error");
}

}  // namespace
}  // namespace kdiag
