// Verifies journal timestamp conversion, prefix parsing, and source tagging.
#include "kdiag/collectors/journalctl_collector.hpp"

#include <ctime>

#include <gtest/gtest.h>

#include "fake_command_runner.hpp"

namespace kdiag {
namespace {

TEST(JournalctlCollectorTest, ParsesIso8601TimestampToEpochNanoseconds) {
    testing::FakeCommandRunner runner;
    JournalctlCollector collector(runner, "");

    const std::string line = "2026-09-27T07:12:41+0000 host kernel: driver_name: probe failed\n";
    const auto events = collector.ParseOutput(line);

    ASSERT_EQ(events.size(), 1u);
    // 2026-09-27T07:12:41Z in epoch seconds, computed independently below
    // via the same libc call the implementation uses, so this test pins
    // behavior rather than re-deriving the constant by hand.
    std::tm tm{};
    tm.tm_year = 2026 - 1900; tm.tm_mon = 9 - 1; tm.tm_mday = 27;
    tm.tm_hour = 7; tm.tm_min = 12; tm.tm_sec = 41;
    const int64_t expected_ns = static_cast<int64_t>(timegm(&tm)) * 1'000'000'000LL;

    EXPECT_EQ(events[0].timestamp_ns, expected_ns);
}

TEST(JournalctlCollectorTest, StripsHostAndKernelPrefixFromMessage) {
    testing::FakeCommandRunner runner;
    JournalctlCollector collector(runner, "");

    const auto events = collector.ParseOutput(
        "2026-09-27T07:12:41+0000 myhost kernel: driver_name: probe failed\n");

    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].message, "driver_name: probe failed");
}

TEST(JournalctlCollectorTest, ClassifiesStrippedMessage) {
    testing::FakeCommandRunner runner;
    JournalctlCollector collector(runner, "");

    const auto events = collector.ParseOutput(
        "2026-09-27T07:12:41+0000 myhost kernel: irq 47: nobody cared\n");

    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].component, Component::kIrq);
    EXPECT_EQ(events[0].event, "irq_unhandled");
}

TEST(JournalctlCollectorTest, UnparsableTimestampMarksFieldButKeepsEvent) {
    testing::FakeCommandRunner runner;
    JournalctlCollector collector(runner, "");

    const auto events = collector.ParseOutput("not-a-timestamp host kernel: something happened\n");

    ASSERT_EQ(events.size(), 1u);
    EXPECT_TRUE(events[0].fields.value("parse_error", false));
}

TEST(JournalctlCollectorTest, EventsAreTaggedWithWallClockDomain) {
    testing::FakeCommandRunner runner;
    JournalctlCollector collector(runner, "");

    const auto events = collector.ParseOutput(
        "2026-09-27T07:12:41+0000 host kernel: something happened\n");

    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].fields.at("clock_domain"), "wall_clock_ns");
}

TEST(JournalctlCollectorTest, SinceFlagIsIncludedInCommandWhenSet) {
    testing::FakeCommandRunner runner;
    runner.SetResponse({"journalctl", "-k", "-o", "short-iso", "--no-pager",
                        "--since", "-10 min"}, "", 0);
    JournalctlCollector collector(runner, "-10 min");

    const auto events = collector.Collect();

    EXPECT_TRUE(events.empty());  // empty stdout, exit 0 -> no error event, no lines
}

}  // namespace
}  // namespace kdiag
