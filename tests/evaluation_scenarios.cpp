// Verifies all built-in synthetic scenarios and benchmark report generation.
#include "kdiag/evaluator.hpp"

#include <algorithm>

#include <gtest/gtest.h>

namespace kdiag::eval {
namespace {

TEST(EvaluationTest, EvaluatesTenScenarios) {
    const auto results = Evaluator().EvaluateAll();

    ASSERT_EQ(results.size(), 10u);
    for (const auto& result : results) {
        EXPECT_TRUE(result.passed) << "scenario: " << result.scenario_name
                                  << " detected=" << result.detected
                                  << " expected=" << result.root_cause_expected
                                  << " actual=" << result.root_cause_detected;
    }
}

TEST(EvaluationTest, CleanCaptureIsNegativeTest) {
    const auto results = Evaluator().EvaluateAll();
    const auto clean = std::find_if(results.begin(), results.end(),
        [](const EvaluationResult& result) {
            return result.scenario_name == "clean_complete_capture";
        });

    ASSERT_NE(clean, results.end());
    EXPECT_FALSE(clean->detected);
    EXPECT_TRUE(clean->root_cause_match);
}

TEST(EvaluationTest, ReportIncludesScenarioSummary) {
    const auto results = Evaluator().EvaluateAll();
    const std::string report = Evaluator().GenerateReport(results);

    EXPECT_NE(report.find("KDIAG EVALUATION REPORT"), std::string::npos);
    EXPECT_NE(report.find("dma_timeout"), std::string::npos);
    EXPECT_NE(report.find("10/10"), std::string::npos);
}

}  // namespace
}  // namespace kdiag::eval