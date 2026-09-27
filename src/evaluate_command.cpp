// Runs the synthetic evaluation suite and prints its pass/fail report.
#include "kdiag/evaluate_command.hpp"

#include <iostream>

#include "kdiag/evaluator.hpp"

namespace kdiag {

int RunEvaluateCommand() {
    const eval::Evaluator evaluator;
    const auto results = evaluator.EvaluateAll();
    std::cout << evaluator.GenerateReport(results);
    for (const auto& result : results) {
        if (!result.passed) return 1;
    }
    return 0;
}

}  // namespace kdiag