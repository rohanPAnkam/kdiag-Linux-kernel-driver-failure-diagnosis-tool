// Verifies legacy diagnosis predicates and their required positive evidence.
#include "kdiag/diagnoser.hpp"

#include <gtest/gtest.h>

namespace kdiag {
namespace {

ChainReport MakeReport(std::initializer_list<Stage> stages) {
    ChainReport report;
    report.buffer_id = 42;
    size_t event_index = 0;
    for (Stage stage : stages) {
        report.reached_stages.push_back({stage, static_cast<int64_t>(event_index),
                                         report.buffer_id, event_index});
        ++event_index;
    }
    return report;
}

TEST(DiagnoserTest, DetectsDescriptorWithoutIrqOrCompletion) {
    const auto findings = Diagnoser().Diagnose(
        {MakeReport({Stage::kDescriptorSubmission})});

    ASSERT_EQ(findings.size(), 1u);
    EXPECT_EQ(findings[0].rule_id, "KDIAG-DMA-001");
    EXPECT_EQ(findings[0].evidence[0].stage, Stage::kDescriptorSubmission);
    EXPECT_EQ(findings[0].not_observed,
              (std::vector<Stage>{Stage::kIrq, Stage::kDmaCompletion}));
}

TEST(DiagnoserTest, CompletionSuppressesDmaIrqFinding) {
    const auto findings = Diagnoser().Diagnose(
        {MakeReport({Stage::kDescriptorSubmission, Stage::kDmaCompletion})});

    ASSERT_EQ(findings.size(), 1u);
    EXPECT_EQ(findings[0].rule_id, "KDIAG-DRV-001");
}

TEST(DiagnoserTest, DetectsCompletionWithoutVb2BufferDone) {
    const auto findings = Diagnoser().Diagnose(
        {MakeReport({Stage::kDmaCompletion})});

    ASSERT_EQ(findings.size(), 1u);
    EXPECT_EQ(findings[0].rule_id, "KDIAG-DRV-001");
    EXPECT_EQ(findings[0].evidence[0].stage, Stage::kDmaCompletion);
    EXPECT_EQ(findings[0].not_observed,
              (std::vector<Stage>{Stage::kVb2BufferDone}));
}

TEST(DiagnoserTest, DetectsV4l2DequeueWithoutGstreamerConsumption) {
    const auto findings = Diagnoser().Diagnose(
        {MakeReport({Stage::kV4l2Dequeue})});

    ASSERT_EQ(findings.size(), 1u);
    EXPECT_EQ(findings[0].rule_id, "KDIAG-USER-001");
    EXPECT_EQ(findings[0].evidence[0].stage, Stage::kV4l2Dequeue);
}

TEST(DiagnoserTest, DoesNotReportRulesWhenTheirPositiveEvidenceIsAbsent) {
    const auto findings = Diagnoser().Diagnose(
        {MakeReport({Stage::kDeviceInit, Stage::kStreamOn})});

    EXPECT_TRUE(findings.empty());
}

}  // namespace
}  // namespace kdiag