// Verifies dmesg timestamp parsing, raw-line retention, and driver classification.
#include "kdiag/collectors/dmesg_collector.hpp"

#include <gtest/gtest.h>

#include "fake_command_runner.hpp"

namespace kdiag {
namespace {

// This is a condensed version of the exact failure scenario from the
// project brief: DMA descriptor submitted, no completion interrupt,
// V4L2 times out.
constexpr char kSampleDmesg[] =
    "[   10.100000] driver_name 0000:03:00.0: dma_map_single: descriptor submitted for buffer 1832\n"
    "[   10.101000] driver_name 0000:03:00.0: streaming started on video3\n"
    "[   10.200000] pcieport 0000:00:1c.0: AER: Corrected error received\n"
    "[   10.325000] driver_name 0000:03:00.0: vb2 timeout waiting for buffer 1832\n";

TEST(DmesgCollectorTest, ParsesTimestampsAsNanoseconds) {
    testing::FakeCommandRunner runner;
    DmesgCollector collector(runner);

    const auto events = collector.ParseOutput(kSampleDmesg);

    ASSERT_EQ(events.size(), 4u);
    EXPECT_EQ(events[0].timestamp_ns, 10'100'000'000LL);
    EXPECT_EQ(events[1].timestamp_ns, 10'101'000'000LL);
    EXPECT_EQ(events[2].timestamp_ns, 10'200'000'000LL);
    EXPECT_EQ(events[3].timestamp_ns, 10'325'000'000LL);
}

TEST(DmesgCollectorTest, ClassifiesEachLineIntoExpectedComponent) {
    testing::FakeCommandRunner runner;
    DmesgCollector collector(runner);

    const auto events = collector.ParseOutput(kSampleDmesg);

    ASSERT_EQ(events.size(), 4u);
    EXPECT_EQ(events[0].component, Component::kDma);
    EXPECT_EQ(events[2].component, Component::kPci);
    EXPECT_EQ(events[2].event, "pci_aer_error");
}

TEST(DmesgCollectorTest, PreservesRawLineVerbatimForEvidenceCitation) {
    testing::FakeCommandRunner runner;
    DmesgCollector collector(runner);

    const auto events = collector.ParseOutput(kSampleDmesg);

    EXPECT_EQ(events[0].raw,
              "[   10.100000] driver_name 0000:03:00.0: dma_map_single: descriptor submitted for buffer 1832");
}

TEST(DmesgCollectorTest, LineWithoutBracketedTimestampGetsZeroTimestampNotDropped) {
    testing::FakeCommandRunner runner;
    DmesgCollector collector(runner);

    const auto events = collector.ParseOutput("no leading bracket here\n");

    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].timestamp_ns, 0);
    EXPECT_EQ(events[0].message, "no leading bracket here");
}

TEST(DmesgCollectorTest, NonZeroExitProducesSingleCollectorErrorEvent) {
    testing::FakeCommandRunner runner;
    runner.SetResponse({"dmesg"}, "dmesg: read kernel buffer failed: Operation not permitted", 1);
    DmesgCollector collector(runner);

    const auto events = collector.Collect();

    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].event, "collector_error");
}

TEST(DmesgCollectorTest, SuccessfulRunDelegatesToParseOutput) {
    testing::FakeCommandRunner runner;
    runner.SetResponse({"dmesg"}, kSampleDmesg, 0);
    DmesgCollector collector(runner);

    const auto events = collector.Collect();

    EXPECT_EQ(events.size(), 4u);
}

TEST(DmesgCollectorTest, UsesConfiguredDriverNameForGenericDriverLines) {
    testing::FakeCommandRunner runner;
    DmesgCollector collector(runner, "my_camera");

    const auto events = collector.ParseOutput(
        "[   10.000000] my_camera: firmware state changed\n");

    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].component, Component::kDriver);
    EXPECT_EQ(events[0].event, "driver_log");
}

}  // namespace
}  // namespace kdiag
