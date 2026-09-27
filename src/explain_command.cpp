// Orchestrates correlation, diagnosis, optional rendering, and explanation output.
#include "kdiag/explain_command.hpp"

#include <cstdlib>
#include <iostream>
#include <utility>

#include "kdiag/correlator.hpp"
#include "kdiag/collection_file.hpp"
#include "kdiag/diagnosis_rules.hpp"
#include "kdiag/event.hpp"
#include "kdiag/llm_explainer.hpp"
#include "kdiag/ui_renderer.hpp"

namespace kdiag {

namespace {

// Return a nonempty environment setting, treating unset and empty alike.
const char* EnvironmentValue(const char* name) {
    const char* value = std::getenv(name);
    return value && *value ? value : nullptr;
}

// Print the four user-facing sections of a local or remote explanation.
void PrintExplanation(const LlmExplanation& explanation) {
    if (explanation.used_local_fallback) {
        std::cout << "Mode: local evidence-only summary (no remote request was made)\n";
    } else {
        std::cout << "Mode: LLM-assisted explanation\n";
    }
    std::cout << "\nExecutive Summary\n"
              << explanation.executive_summary << "\n\n"
              << "Technical Explanation\n"
              << explanation.technical_explanation << "\n\n"
              << "Remediation Steps\n"
              << explanation.remediation_steps << "\n\n"
              << "References\n"
              << explanation.references << "\n";
}

}  // namespace

int RunExplainCommand(const ExplainOptions& options) {
    CollectionRun run;
    try {
        run = LoadCollectionRun(options.input_path);
    } catch (const std::exception& ex) {
        std::cerr << "[kdiag] error: " << ex.what() << "\n";
        return 1;
    }

    const Correlator correlator(run.events);
    const auto chains = correlator.Correlate();
    const DiagnosisRuleEngine engine(run);
    const auto reports = engine.DiagnoseAll(chains);

    if (options.show_diagram) {
        const PipelineRenderer renderer;
        for (const auto& chain : chains) {
            std::cout << renderer.RenderTimeline(chain) << '\n';
        }
    }

    DiagnosisReport combined;
    size_t hidden_low_confidence = 0;
    for (const auto& report : reports) {
        for (const auto& hypothesis : report.hypotheses) {
            if (hypothesis.confidence == Confidence::kLow && !options.verbose) {
                ++hidden_low_confidence;
                continue;
            }
            combined.hypotheses.push_back(hypothesis);
        }
    }
    combined.has_root_cause_candidate = std::any_of(
        combined.hypotheses.begin(), combined.hypotheses.end(),
        [](const DiagnosisHypothesis& hypothesis) {
            return hypothesis.confidence == Confidence::kHigh;
        });

    if (combined.hypotheses.empty() && hidden_low_confidence > 0) {
        std::cout << "Only low-confidence patterns matched; pass --verbose to include them.\n";
        return 0;
    }

    const char* key = EnvironmentValue("ANTHROPIC_API_KEY");
    const char* model = EnvironmentValue("KDIAG_LLM_MODEL");
    const char* endpoint = EnvironmentValue("KDIAG_LLM_ENDPOINT");
    if (key && !combined.hypotheses.empty()) {
        std::cout << "Notice: selected diagnosis evidence and device metadata will be sent to the configured LLM endpoint.\n";
    }
    const LlmExplainer explainer(
        key ? key : "",
        model ? model : "claude-sonnet-4-20250514",
        endpoint ? endpoint : "https://api.anthropic.com/v1/messages");

    try {
        PrintExplanation(explainer.Explain({combined, run, options.deep_dive}));
    } catch (const std::exception& ex) {
        std::cerr << "[kdiag] error: explanation failed: " << ex.what() << "\n";
        return 1;
    }
    return 0;
}

}  // namespace kdiag