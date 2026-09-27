// Verifies IRQ row parsing, CPU-count summation, and snapshot semantics.
#include "kdiag/collectors/interrupts_collector.hpp"

#include <gtest/gtest.h>

#include "fake_command_runner.hpp"

namespace kdiag {
namespace {

constexpr char kSampleProcInterrupts[] =
    "           CPU0       CPU1\n"
    " 16:         12          3   IO-APIC   16-fasteoi   driver_name\n"
    " 47:          0          0   PCI-MSI 524288-edge   xhci_hcd\n"
    "NMI:          9          8   Non-maskable interrupts\n";

TEST(InterruptsCollectorTest, ParsesIrqLineWithSummedCpuCounts) {
    testing::FakeCommandRunner runner;
    InterruptsCollector collector(runner);

    const auto events = collector.ParseOutput(kSampleProcInterrupts);

    ASSERT_GE(events.size(), 2u);
    EXPECT_EQ(events[0].fields.at("irq"), "16");
    EXPECT_EQ(events[0].fields.at("total_count"), 15);  // 12 + 3
    EXPECT_EQ(events[0].fields.at("chip"), "IO-APIC");
    EXPECT_NE(events[0].fields.at("actions").get<std::string>().find("driver_name"), std::string::npos);
}

TEST(InterruptsCollectorTest, SkipsNonNumericFooterLines) {
    testing::FakeCommandRunner runner;
    InterruptsCollector collector(runner);

    const auto events = collector.ParseOutput(kSampleProcInterrupts);

    for (const auto& e : events) {
        EXPECT_NE(e.fields.value("irq", ""), "NMI");
    }
}

TEST(InterruptsCollectorTest, AllEventsCarrySnapshotTimestampSentinel) {
    testing::FakeCommandRunner runner;
    InterruptsCollector collector(runner);

    const auto events = collector.ParseOutput(kSampleProcInterrupts);

    for (const auto& e : events) {
        EXPECT_EQ(e.timestamp_ns, kSnapshotTimestampNs);
        EXPECT_TRUE(e.fields.value("snapshot", false));
    }
}

TEST(InterruptsCollectorTest, CollectFailureProducesCollectorErrorEvent) {
    testing::FakeCommandRunner runner;
    runner.SetResponse({"cat", "/proc/interrupts"},
                       "cat: /proc/interrupts: Permission denied", 1);
    InterruptsCollector collector(runner);

    const auto events = collector.Collect();

    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].event, "collector_error");
}

}  // namespace
}  // namespace kdiag
