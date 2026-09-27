// Loads a capture, runs deterministic rules, and prints cited hypotheses.
#include "kdiag/diagnose_command.hpp"

#include <iomanip>
#include <iostream>
#include <sstream>

#include "kdiag/correlator.hpp"
#include "kdiag/collection_file.hpp"
#include "kdiag/diagnosis_rules.hpp"
#include "kdiag/event.hpp"

namespace kdiag {

namespace {

// Format timeline timestamps while labeling snapshots as non-temporal data.
std::string FormatTimestamp(int64_t timestamp_ns) {
    if (timestamp_ns == kSnapshotTimestampNs) return "snapshot";
    std::ostringstream output;
    output << std::fixed << std::setprecision(6)
           << static_cast<double>(timestamp_ns) / 1'000'000'000.0 << "s";
    return output.str();
}

// Print one hypothesis with validated event references and investigation steps.
void PrintHypothesis(const DiagnosisHypothesis& hypothesis,
                     const CollectionRun& run) {
    std::cout << "[" << ToString(hypothesis.confidence) << " CONFIDENCE]\n"
              << "Hypothesis: " << hypothesis.title << "\n"
              << "Description: " << hypothesis.description << "\n"
              << "Evidence:\n";
    for (size_t i = 0; i < hypothesis.evidence_event_indices.size(); ++i) {
        const size_t index = hypothesis.evidence_event_indices[i];
        if (index >= run.events.size()) {
            std::cout << "  " << i + 1 << ". [invalid event reference] index "
                      << index << "\n";
            continue;
        }
        const Event& event = run.events[index];
        std::cout << "  " << i + 1 << ". [" << FormatTimestamp(event.timestamp_ns)
                  << "] " << ToString(event.source) << " event #" << index << ": "
                  << hypothesis.evidence_descriptions.at(i) << "\n"
                  << "     " << event.raw << "\n";
    }
    if (hypothesis.confidence != Confidence::kHigh) {
        std::cout << "Caveat: This is a candidate, not a confirmed root cause.\n";
    }
    std::cout << "Suggested Investigation:\n";
    for (const auto& step : hypothesis.suggested_investigation) {
        std::cout << "  - " << step << "\n";
    }
    std::cout << "\n";
}

}  // namespace

int RunDiagnoseCommand(const DiagnoseOptions& options) {
    CollectionRun run;
    try {
        run = LoadCollectionRun(options.input_path);
    } catch (const std::exception& ex) {
        std::cerr << "[kdiag] error: " << ex.what() << "\n";
        return 1;
    }

    Correlator correlator(run.events);
    const auto reports = correlator.Correlate();
    const DiagnosisRuleEngine engine(run);
    const auto diagnosis_reports = engine.DiagnoseAll(reports);

    std::cout << "==========================================\n"
              << "KDIAG DIAGNOSIS REPORT\n"
              << "==========================================\n"
              << "Device: " << (run.pci_bdf.empty() ? "(unspecified)" : run.pci_bdf) << "\n"
              << "Driver: " << (run.driver_name.empty() ? "(unspecified)" : run.driver_name) << "\n"
              << "Chains analyzed: " << reports.size() << "\n";

    size_t hypothesis_count = 0;
    size_t hidden_low_count = 0;
    for (const auto& report : diagnosis_reports) {
        for (const auto& hypothesis : report.hypotheses) {
            if (hypothesis.confidence == Confidence::kLow && !options.verbose) {
                ++hidden_low_count;
                continue;
            }
            ++hypothesis_count;
            std::cout << "----------------------------------------\n";
            if (report.buffer_id) {
                std::cout << "Diagnosis for buffer " << *report.buffer_id << "\n";
            } else {
                std::cout << "Collection-wide diagnosis\n";
            }
            PrintHypothesis(hypothesis, run);
        }
    }

    if (hypothesis_count == 0 && hidden_low_count == 0) {
        std::cout << "No diagnosis rules matched. This does not establish that the pipeline is healthy.\n";
    } else if (hypothesis_count == 0) {
        std::cout << "Only low-confidence patterns matched; pass --verbose to show them.\n";
    }
    std::cout << "Rule matches are evidence-based hypotheses. Missing stages may be uninstrumented or outside the capture window.\n";
    return 0;
}

}  // namespace kdiag