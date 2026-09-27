// Structured request/response types and transport boundary for LLM explanations.
#ifndef A60A179A_9CA0_4969_9199_60868FA397F7
#define A60A179A_9CA0_4969_9199_60868FA397F7

#include <functional>
#include <string>

#include "kdiag/diagnosis.hpp"
#include "kdiag/event.hpp"

namespace kdiag {

struct LlmExplanationRequest {
    DiagnosisReport diagnosis_report;  ///< Ranked hypotheses to explain.
    const CollectionRun& run;  ///< Source events referenced by hypothesis evidence indices.
    bool include_technical_deep_dive = false;  ///< Request expanded technical detail.
};

struct LlmExplanation {
    std::string executive_summary;  ///< Concise explanation grounded in supplied evidence.
    std::string technical_explanation;  ///< Subsystem-level explanation with event citations.
    std::string remediation_steps;  ///< Suggested investigation actions.
    std::string references;  ///< Provider-returned documentation references, if any.
    bool used_local_fallback = false;  ///< True when no remote request was made.
};

/// Injectable HTTP POST operation, primarily used to test without network access.
using LlmPostRequest = std::function<std::string(
    const std::string& endpoint, const std::string& api_key,
    const std::string& request_body)>;

class LlmExplainer {
public:
    /// Configure provider credentials and transport; an empty key selects local output.
    explicit LlmExplainer(
        std::string api_key = {},
        std::string model = "claude-sonnet-4-20250514",
        std::string endpoint = "https://api.anthropic.com/v1/messages",
        LlmPostRequest post_request = {});

    /// Explain hypotheses; rejects invalid evidence references and uncited responses.
    /// @throws std::invalid_argument if a hypothesis references an absent event.
    /// @throws std::runtime_error for transport or response-validation failures.
    LlmExplanation Explain(const LlmExplanationRequest& request) const;

private:
    std::string api_key_;
    std::string model_;
    std::string endpoint_;
    LlmPostRequest post_request_;

    /// Create provider instructions that require grounded, cited explanations.
    std::string ConstructSystemPrompt(bool include_technical_deep_dive) const;

    /// Serialize hypotheses and only the source events referenced by their indices.
    std::string ConstructEvidencePayload(const LlmExplanationRequest& request) const;

    /// Produce a deterministic local summary without contacting a provider.
    LlmExplanation LocalFallback(const DiagnosisReport& report) const;
};

}  // namespace kdiag


#endif /* A60A179A_9CA0_4969_9199_60868FA397F7 */
