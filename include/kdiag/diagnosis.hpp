// Shared confidence and hypothesis types used by rules, CLI, and explanations.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace kdiag {

enum class Confidence {
    kLow,
    kMedium,
    kHigh,
};

enum class DiagnosisScope {
    kBuffer,
    kCollection,
};

/// Convert a confidence level to its stable uppercase display name.
std::string ToString(Confidence confidence);
/// Convert a diagnosis scope to its stable display name.
std::string ToString(DiagnosisScope scope);

struct DiagnosisHypothesis {
    std::string title;  ///< Concise name of the possible failure pattern.
    std::string description;  ///< Explanation limited to what the evidence supports.
    Confidence confidence = Confidence::kLow;  ///< Qualitative strength, not probability.
    std::vector<size_t> evidence_event_indices;  ///< Indices into `CollectionRun::events`.
    std::vector<std::string> evidence_descriptions;  ///< Descriptions aligned with evidence indices.
    std::vector<std::string> suggested_investigation;  ///< Non-mutating checks for an operator.
    std::optional<int64_t> buffer_id;  ///< Related buffer, or empty for a run-wide hypothesis.
    std::string rule_id;  ///< Stable rule identifier, independent of display wording.
    DiagnosisScope scope = DiagnosisScope::kBuffer;  ///< Whether the finding applies to one buffer or the run.
};

struct DiagnosisReport {
    std::optional<int64_t> buffer_id;  ///< Buffer ID, or empty for a collection-wide report.
    std::vector<DiagnosisHypothesis> hypotheses;  ///< Sorted highest-confidence first.
    bool has_root_cause_candidate = false;  ///< True when at least one hypothesis is HIGH.
};

}  // namespace kdiag