// Verifies event/run JSON round-trips, enum mappings, and time sorting.
#include "kdiag/event.hpp"

#include <gtest/gtest.h>

namespace kdiag {
namespace {

TEST(EventTest, JsonRoundTripPreservesAllFields) {
    Event e;
    e.timestamp_ns = 123456789;
    e.source = Source::kDmesg;
    e.component = Component::kDma;
    e.event = "dma_timeout";
    e.message = "DMA timeout on channel 2";
    e.raw = "[   12.345678] DMA timeout on channel 2";
    e.fields = {{"channel", 2}, {"retries", 3}};

    const Event round_tripped = Event::FromJson(e.ToJson());

    EXPECT_EQ(round_tripped.timestamp_ns, e.timestamp_ns);
    EXPECT_EQ(round_tripped.source, e.source);
    EXPECT_EQ(round_tripped.component, e.component);
    EXPECT_EQ(round_tripped.event, e.event);
    EXPECT_EQ(round_tripped.message, e.message);
    EXPECT_EQ(round_tripped.raw, e.raw);
    EXPECT_EQ(round_tripped.fields, e.fields);
}

TEST(EventTest, ComponentStringRoundTrip) {
    for (auto c : {Component::kKernel, Component::kDma, Component::kIrq, Component::kPci,
                   Component::kV4l2, Component::kGstreamer, Component::kDriver, Component::kUnknown}) {
        EXPECT_EQ(ComponentFromString(ToString(c)), c);
    }
}

TEST(CollectionRunTest, SortByTimeOrdersEventsAscending) {
    CollectionRun run;
    run.pci_bdf = "0000:03:00.0";

    Event a; a.timestamp_ns = 300; a.message = "third";
    Event b; b.timestamp_ns = 100; b.message = "first";
    Event c; c.timestamp_ns = 200; c.message = "second";
    run.events = {a, b, c};

    run.SortByTime();

    ASSERT_EQ(run.events.size(), 3u);
    EXPECT_EQ(run.events[0].message, "first");
    EXPECT_EQ(run.events[1].message, "second");
    EXPECT_EQ(run.events[2].message, "third");
}

TEST(CollectionRunTest, JsonRoundTripPreservesEventsAndMetadata) {
    CollectionRun run;
    run.pci_bdf = "0000:03:00.0";
    run.driver_name = "driver_name";
    run.kernel_release = "7.0.0-34-generic";
    run.collected_at_unix_s = 1780000000;

    Event e;
    e.timestamp_ns = 42;
    e.event = "irq_snapshot";
    run.events = {e};

    const CollectionRun round_tripped = CollectionRun::FromJson(run.ToJson());

    EXPECT_EQ(round_tripped.pci_bdf, run.pci_bdf);
    EXPECT_EQ(round_tripped.driver_name, run.driver_name);
    EXPECT_EQ(round_tripped.kernel_release, run.kernel_release);
    EXPECT_EQ(round_tripped.collected_at_unix_s, run.collected_at_unix_s);
    ASSERT_EQ(round_tripped.events.size(), 1u);
    EXPECT_EQ(round_tripped.events[0].event, "irq_snapshot");
}

}  // namespace
}  // namespace kdiag
