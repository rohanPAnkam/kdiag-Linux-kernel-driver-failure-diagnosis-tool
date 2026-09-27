// Options and entry point for explaining a captured diagnosis.
#ifndef E37F8D85_3868_4558_84C6_E707E7B84471
#define E37F8D85_3868_4558_84C6_E707E7B84471

#include <string>

namespace kdiag {

struct ExplainOptions {
    std::string input_path;  ///< Collection JSON produced by `kdiag collect`.
    bool show_diagram = false;  ///< Render a timeline for every correlated chain.
    bool verbose = false;  ///< Include LOW-confidence hypotheses in the prompt.
    bool deep_dive = false;  ///< Ask the remote model for extra technical detail.
};

/// Correlate and diagnose a capture, then print a local or LLM explanation.
/// @return 0 on success; nonzero if input parsing or explanation fails.
int RunExplainCommand(const ExplainOptions& options);

}  // namespace kdiag


#endif /* E37F8D85_3868_4558_84C6_E707E7B84471 */
