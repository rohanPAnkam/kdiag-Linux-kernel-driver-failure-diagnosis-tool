// Verifies buffer-id extraction, chain merging, and first-gap detection.
#include "kdiag/correlator.hpp"

#include <map>

#include <gtest/gtest.h>

namespace kdiag {
namespace {

Event MakeEvent(int64_t ts_ns, const std::string& message) {
    Event e;
    e.timestamp_ns = ts_ns;
    e.source = Source::kDmesg;
    e.message = message;
    e.raw = message;
    return e;
}

// ---- ExtractBufferId ----

TEST(CorrelatorBufferIdTest, ExtractsBufferKeyword) {
    EXPECT_EQ(Correlator::ExtractBufferId("descriptor submitted for buffer 1832"), 1832);
}

TEST(CorrelatorBufferIdTest, ExtractsBufWithHash) {
    EXPECT_EQ(Correlator::ExtractBufferId("buf #7 dequeued"), 7);
}

TEST(CorrelatorBufferIdTest, ExtractsFrameKeyword) {
    EXPECT_EQ(Correlator::ExtractBufferId("frame 12 dropped"), 12);
}

TEST(CorrelatorBufferIdTest, DoesNotMatchUnrelatedTrailingDigits) {
    // "video3" must not be misread as a buffer id.
    EXPECT_FALSE(Correlator::ExtractBufferId("v4l2src device=/dev/video3").has_value());
}

TEST(CorrelatorBufferIdTest, NoDigitsReturnsNullopt) {
    EXPECT_FALSE(Correlator::ExtractBufferId("streaming started").has_value());
}

// ---- Correlate(): the worked failure example from the project brief ----

TEST(CorrelatorTest, DetectsBreakAtMissingIrqAfterDescriptorSubmission) {
    std::vector<Event> events = {
        MakeEvent(1'000, "driver_name 0000:03:00.0: probe succeeded"),
        MakeEvent(2'000, "device initialized"),
        MakeEvent(3'000, "buffer allocated for capture"),
        MakeEvent(4'000, "dma_map_single: mapped buffer 1832"),
        MakeEvent(5'000, "streaming started on video3"),
        MakeEvent(6'000, "descriptor submitted for buffer 1832"),
        // No IRQ, no DMA completion, no vb2_buffer_done: the chain stops here.
    };

    Correlator correlator(events);
    const auto reports = correlator.Correlate();

    ASSERT_EQ(reports.size(), 1u);
    const ChainReport& report = reports[0];

    ASSERT_TRUE(report.buffer_id.has_value());
    EXPECT_EQ(*report.buffer_id, 1832);
    EXPECT_FALSE(report.complete);
    ASSERT_TRUE(report.last_reached_stage.has_value());
    EXPECT_EQ(*report.last_reached_stage, Stage::kDescriptorSubmission);
    ASSERT_TRUE(report.first_missing_stage.has_value());
    EXPECT_EQ(*report.first_missing_stage, Stage::kIrq);
}

TEST(CorrelatorTest, CompleteChainReachesEveryStage) {
    std::vector<Event> events = {
        MakeEvent(1'000, "driver_name 0000:03:00.0: probe succeeded"),
        MakeEvent(2'000, "device initialized"),
        MakeEvent(3'000, "buffer allocated for capture"),
        MakeEvent(4'000, "dma_map_single: mapped buffer 42"),
        MakeEvent(5'000, "streaming started on video3"),
        MakeEvent(6'000, "descriptor submitted for buffer 42"),
        MakeEvent(7'000, "interrupt received for buffer 42"),
        MakeEvent(8'000, "dma completion for buffer 42"),
        MakeEvent(9'000, "vb2_buffer_done: buffer 42"),
        MakeEvent(10'000, "VIDIOC_DQBUF buffer 42"),
        MakeEvent(11'000, "appsink pulled buffer 42"),
    };

    Correlator correlator(events);
    const auto reports = correlator.Correlate();

    ASSERT_EQ(reports.size(), 1u);
    EXPECT_TRUE(reports[0].complete);
    EXPECT_EQ(reports[0].last_reached_stage, Stage::kGstreamerConsume);
    EXPECT_FALSE(reports[0].first_missing_stage.has_value());
    EXPECT_EQ(reports[0].reached_stages.size(), 11u);
}

TEST(CorrelatorTest, MultipleBuffersProduceSeparateChains) {
    std::vector<Event> events = {
        MakeEvent(1'000, "streaming started on video3"),
        MakeEvent(2'000, "descriptor submitted for buffer 1"),
        MakeEvent(3'000, "vb2_buffer_done: buffer 1"),
        MakeEvent(4'000, "descriptor submitted for buffer 2"),
        // buffer 2 never reaches vb2_buffer_done.
    };

    Correlator correlator(events);
    const auto reports = correlator.Correlate();

    ASSERT_EQ(reports.size(), 2u);
    std::map<int64_t, const ChainReport*> by_id;
    for (const auto& r : reports) by_id[*r.buffer_id] = &r;

    EXPECT_EQ(by_id.at(1)->last_reached_stage, Stage::kVb2BufferDone);
    EXPECT_EQ(by_id.at(2)->last_reached_stage, Stage::kDescriptorSubmission);
    EXPECT_EQ(by_id.at(2)->first_missing_stage, Stage::kIrq);
}

TEST(CorrelatorTest, SnapshotEventsAreExcludedFromStageExtraction) {
    Event snapshot = MakeEvent(0, "vb2_buffer_done: buffer 99");
    snapshot.timestamp_ns = kSnapshotTimestampNs;
    snapshot.fields = {{"snapshot", true}};

    Correlator correlator({snapshot});
    const auto occurrences = correlator.ExtractStageOccurrences();

    EXPECT_TRUE(occurrences.empty());
}

TEST(CorrelatorTest, NoBufferIdsFallsBackToSingleSessionChain) {
    std::vector<Event> events = {
        MakeEvent(1'000, "driver_name 0000:03:00.0: probe succeeded"),
        MakeEvent(2'000, "streaming started on video3"),
    };

    Correlator correlator(events);
    const auto reports = correlator.Correlate();

    ASSERT_EQ(reports.size(), 1u);
    EXPECT_FALSE(reports[0].buffer_id.has_value());
}

}  // namespace
}  // namespace kdiag
