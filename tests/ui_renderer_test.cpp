// Verifies rendered timeline stages, completion markers, and empty reports.
#include "kdiag/ui_renderer.hpp"

#include <gtest/gtest.h>

namespace kdiag {
namespace {

TEST(PipelineRendererTest, MarksReachedAndFirstMissingStages) {
    ChainReport report;
    report.buffer_id = 1832;
    report.reached_stages = {
        {Stage::kStreamOn, 1'000'000'000, 1832, 0},
        {Stage::kDescriptorSubmission, 2'000'000'000, 1832, 1},
    };
    report.first_missing_stage = Stage::kIrq;

    const std::string rendered = PipelineRenderer().RenderTimeline(report);

    EXPECT_NE(rendered.find("Pipeline for buffer 1832"), std::string::npos);
    EXPECT_NE(rendered.find("[reached] stream_on @ 1.000000s"), std::string::npos);
    EXPECT_NE(rendered.find("[reached] descriptor_submission @ 2.000000s"),
              std::string::npos);
    EXPECT_NE(rendered.find("[break] irq"), std::string::npos);
}

TEST(PipelineRendererTest, MarksCompleteChain) {
    ChainReport report;
    report.complete = true;

    const std::string rendered = PipelineRenderer().RenderTimeline(report);

    EXPECT_NE(rendered.find("[complete]"), std::string::npos);
    EXPECT_EQ(rendered.find("[break]"), std::string::npos);
}

TEST(PipelineRendererTest, HandlesNoClassifiedStages) {
    const std::string rendered = PipelineRenderer().RenderTimeline(ChainReport{});

    EXPECT_NE(rendered.find("[unknown]"), std::string::npos);
}

}  // namespace
}  // namespace kdiag