// Verifies pipeline stage names, ordering, and message classification.
#include "kdiag/stage.hpp"

#include <gtest/gtest.h>

namespace kdiag {
namespace {

class StageClassifierTest : public ::testing::Test {
protected:
    StageClassifier classifier_;
};

TEST_F(StageClassifierTest, ClassifiesEachStageOfTheWorkedExample) {
    // Mirrors the exact failure scenario from the project brief.
    EXPECT_EQ(classifier_.Classify("driver_name 0000:03:00.0: dma_map_single: descriptor submitted"),
              Stage::kDmaMapping);  // dma_map_single matches before "descriptor submitted" in rule order
    EXPECT_EQ(classifier_.Classify("streaming started on video3"), Stage::kStreamOn);
    EXPECT_EQ(classifier_.Classify("descriptor submitted for buffer 1832"),
              Stage::kDescriptorSubmission);
    EXPECT_EQ(classifier_.Classify("vb2_buffer_done: buffer 1832 marked DONE"),
              Stage::kVb2BufferDone);
}

TEST_F(StageClassifierTest, UnrelatedLineReturnsNullopt) {
    EXPECT_FALSE(classifier_.Classify("systemd[1]: Started apt-daily.service").has_value());
}

TEST_F(StageClassifierTest, PipelineOrderMatchesBriefExactly) {
    const auto& order = PipelineOrder();
    ASSERT_EQ(order.size(), 11u);
    EXPECT_EQ(order[0], Stage::kPciProbe);
    EXPECT_EQ(order[1], Stage::kDeviceInit);
    EXPECT_EQ(order[2], Stage::kBufferAlloc);
    EXPECT_EQ(order[3], Stage::kDmaMapping);
    EXPECT_EQ(order[4], Stage::kStreamOn);
    EXPECT_EQ(order[5], Stage::kDescriptorSubmission);
    EXPECT_EQ(order[6], Stage::kIrq);
    EXPECT_EQ(order[7], Stage::kDmaCompletion);
    EXPECT_EQ(order[8], Stage::kVb2BufferDone);
    EXPECT_EQ(order[9], Stage::kV4l2Dequeue);
    EXPECT_EQ(order[10], Stage::kGstreamerConsume);
}

TEST_F(StageClassifierTest, ToStringCoversEveryStage) {
    for (Stage s : PipelineOrder()) {
        EXPECT_NE(ToString(s), "unknown_stage");
    }
}

}  // namespace
}  // namespace kdiag
