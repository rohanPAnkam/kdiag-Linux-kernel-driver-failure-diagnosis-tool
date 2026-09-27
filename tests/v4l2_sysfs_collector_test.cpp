// Verifies V4L2 node parsing and the empty-sysfs snapshot event.
#include "kdiag/collectors/v4l2_sysfs_collector.hpp"

#include <gtest/gtest.h>

#include "fake_command_runner.hpp"

namespace kdiag {
namespace {

TEST(V4l2SysfsCollectorTest, ParsesSingleNode) {
    testing::FakeCommandRunner runner;
    V4l2SysfsCollector collector(runner);

    const std::string listing =
        "NODE:/sys/class/video4linux/video3\n"
        "NAME:driver_name camera\n"
        "INDEX:0\n"
        "DEVICE:/sys/devices/pci0000:00/0000:00:1c.0/0000:03:00.0\n"
        "---\n";

    const auto events = collector.ParseOutput(listing);

    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].event, "v4l2_node_present");
    EXPECT_EQ(events[0].fields.at("node"), "/sys/class/video4linux/video3");
    EXPECT_EQ(events[0].fields.at("name"), "driver_name camera");
    EXPECT_EQ(events[0].fields.at("index"), "0");
}

TEST(V4l2SysfsCollectorTest, ParsesMultipleNodes) {
    testing::FakeCommandRunner runner;
    V4l2SysfsCollector collector(runner);

    const std::string listing =
        "NODE:/sys/class/video4linux/video3\n"
        "NAME:cam0\n"
        "INDEX:0\n"
        "DEVICE:/sys/devices/x\n"
        "---\n"
        "NODE:/sys/class/video4linux/video4\n"
        "NAME:cam0 metadata\n"
        "INDEX:1\n"
        "DEVICE:/sys/devices/x\n"
        "---\n";

    const auto events = collector.ParseOutput(listing);

    ASSERT_EQ(events.size(), 2u);
    EXPECT_EQ(events[1].fields.at("node"), "/sys/class/video4linux/video4");
}

TEST(V4l2SysfsCollectorTest, EmptyListingProducesNoNodesFoundEvent) {
    testing::FakeCommandRunner runner;
    V4l2SysfsCollector collector(runner);

    const auto events = collector.ParseOutput("");

    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].event, "v4l2_no_nodes_found");
}

TEST(V4l2SysfsCollectorTest, AllEventsAreSnapshotTagged) {
    testing::FakeCommandRunner runner;
    V4l2SysfsCollector collector(runner);

    const auto events = collector.ParseOutput(
        "NODE:/sys/class/video4linux/video3\nNAME:cam\nINDEX:0\nDEVICE:/x\n---\n");

    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].timestamp_ns, kSnapshotTimestampNs);
}

}  // namespace
}  // namespace kdiag
