// Synthetic scenarios and result types for checking deterministic diagnoses.
#ifndef DADC67B7_E246_45A4_8C95_A340F105F48A
#define DADC67B7_E246_45A4_8C95_A340F105F48A

#include <optional>
#include <string>
#include <vector>

#include "kdiag/diagnosis.hpp"
#include "kdiag/event.hpp"
#include "kdiag/stage.hpp"

namespace kdiag::eval {

struct FailureScenario {
    std::string name;  ///< Stable scenario identifier in benchmark output.
    std::string description;  ///< Failure pattern represented by the fixture.
    std::vector<Event> injected_events;  ///< Synthetic capture input.
    std::optional<Stage> expected_failure_stage;  ///< Expected first missing stage, if comparable.
    std::vector<std::string> expected_root_causes;  ///< Titles expected among generated hypotheses.
    std::optional<Confidence> expected_confidence;  ///< Expected confidence for those hypotheses.
};

struct EvaluationResult {
    std::string scenario_name;  ///< Identifier of the evaluated fixture.
    bool detected = false;  ///< Whether any diagnosis hypothesis was produced.
    std::optional<Stage> actual_failure_stage;  ///< First reported missing stage, if any.
    std::optional<Stage> expected_failure_stage;  ///< Scenario's expected localization.
    std::optional<bool> localization_correct;  ///< Empty when localization is not applicable.
    bool root_cause_match = false;  ///< Whether all expected hypothesis titles were found.
    std::string root_cause_detected;  ///< Joined hypothesis titles from the run.
    std::string root_cause_expected;  ///< Joined expected hypothesis titles.
    bool confidence_correct = false;  ///< Whether expected confidence labels matched.
    bool passed = false;  ///< Combined root-cause, confidence, and applicable localization checks.
};

class Evaluator {
public:
    /// Run all built-in synthetic scenarios through correlation and diagnosis.
    std::vector<EvaluationResult> EvaluateAll() const;

    /// Format scenario outcomes, including separately assessed localization.
    std::string GenerateReport(const std::vector<EvaluationResult>& results) const;

private:
    /// Construct the deterministic set of built-in synthetic failure captures.
    std::vector<FailureScenario> InitializeScenarios() const;
};

}  // namespace kdiag::eval


#endif /* DADC67B7_E246_45A4_8C95_A340F105F48A */
