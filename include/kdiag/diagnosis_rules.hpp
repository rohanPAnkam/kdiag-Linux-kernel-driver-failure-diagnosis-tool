// Rule predicates and engine for evidence-backed diagnosis hypotheses.
#ifndef A0456B60_204A_4103_9239_12CBFA0C863D
#define A0456B60_204A_4103_9239_12CBFA0C863D

#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "kdiag/correlator.hpp"
#include "kdiag/diagnosis.hpp"
#include "kdiag/event.hpp"

namespace kdiag {

struct DiagnosisRule {
    using Matcher = std::function<bool(const ChainReport&, const CollectionRun&)>;
    using Generator = std::function<std::vector<DiagnosisHypothesis>(
        const ChainReport&, const CollectionRun&)>;

    DiagnosisRule(std::string rule_name, Matcher rule_matches,
                  Generator rule_generate,
                  DiagnosisScope rule_scope = DiagnosisScope::kBuffer)
        : name(std::move(rule_name)),
          matches(std::move(rule_matches)),
          generate(std::move(rule_generate)),
          scope(rule_scope) {}

    std::string name;  ///< Stable identifier for this condition/action rule.
    Matcher matches;  ///< Decide whether the rule applies.
    Generator generate;  ///< Build hypotheses when matched.
    DiagnosisScope scope;  ///< Whether matches are reported per buffer or once per run.
};

class DiagnosisRuleEngine {
public:
    /// Bind the engine to a collection run, which must outlive this instance.
    explicit DiagnosisRuleEngine(const CollectionRun& run);

    /// Apply every matching rule to one chain and rank hypotheses by confidence.
    std::vector<DiagnosisHypothesis> Diagnose(const ChainReport& report) const;

    /// Diagnose all chains and append deduplicated collection-wide hypotheses.
    std::vector<DiagnosisReport> DiagnoseAll(
        const std::vector<ChainReport>& chains) const;

private:
    const CollectionRun& run_;
    std::vector<DiagnosisRule> rules_;

    /// Initialize deterministic rule predicates and hypothesis generators.
    void InitializeRules();
};

}  // namespace kdiag


#endif /* A0456B60_204A_4103_9239_12CBFA0C863D */
