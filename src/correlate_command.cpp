// Loads collection JSON and prints per-buffer correlation chains.
#include "kdiag/correlate_command.hpp"

#include <iomanip>
#include <iostream>
#include <sstream>

#include "kdiag/correlator.hpp"
#include "kdiag/collection_file.hpp"
#include "kdiag/event.hpp"

namespace kdiag {

namespace {

// Format timeline timestamps while keeping snapshot time distinct.
std::string FormatTimestamp(int64_t ns) {
    if (ns == kSnapshotTimestampNs) return "(snapshot)";
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(6) << (static_cast<double>(ns) / 1e9) << "s";
    return oss.str();
}

// Print reached stages, their raw evidence, and the first reported chain gap.
void PrintChainReport(const ChainReport& report, const CollectionRun& run) {
    std::cout << "----------------------------------------\n";
    if (report.buffer_id.has_value()) {
        std::cout << "Chain: buffer " << *report.buffer_id << "\n";
    } else {
        std::cout << "Chain: session-scoped (no per-buffer log data found)\n";
    }

    std::cout << "Reached stages:\n";
    for (const auto& occ : report.reached_stages) {
        std::cout << "  [" << FormatTimestamp(occ.timestamp_ns) << "] "
                  << ToString(occ.stage) << "\n"
                  << "      evidence: " << run.events.at(occ.event_index).raw << "\n";
    }

    if (report.complete) {
        std::cout << "Status: COMPLETE — reached every pipeline stage.\n";
        return;
    }

    std::cout << "Status: INCOMPLETE\n";
    std::cout << "  last stage reached: "
              << (report.last_reached_stage ? ToString(*report.last_reached_stage) : "(none)") << "\n";
    std::cout << "  first missing stage: "
              << (report.first_missing_stage ? ToString(*report.first_missing_stage) : "(n/a)") << "\n";
    std::cout << "  NOTE: a missing stage is a candidate gap, not a confirmed root cause —\n"
                 "  it may simply mean this driver/build doesn't log that stage. Month 3\n"
                 "  rules identify selected patterns as candidates; they do not prove cause.\n";
}

}  // namespace

int RunCorrelateCommand(const CorrelateOptions& options) {
    CollectionRun run;
    try {
        run = LoadCollectionRun(options.input_path);
    } catch (const std::exception& ex) {
        std::cerr << "[kdiag] error: " << ex.what() << "\n";
        return 1;
    }

    std::cout << "==========================================\n";
    std::cout << "KDIAG CORRELATION REPORT\n";
    std::cout << "==========================================\n";
    std::cout << "Device: " << (run.pci_bdf.empty() ? "(unspecified)" : run.pci_bdf) << "\n";
    std::cout << "Driver: " << (run.driver_name.empty() ? "(unspecified)" : run.driver_name) << "\n";
    std::cout << "Kernel: " << run.kernel_release << "\n";
    std::cout << "Total events: " << run.events.size() << "\n";

    Correlator correlator(run.events);
    const std::vector<ChainReport> reports = correlator.Correlate();

    int incomplete_count = 0;
    for (const auto& report : reports) {
        if (!report.complete) ++incomplete_count;
        if (report.complete && !options.show_complete_chains) continue;
        PrintChainReport(report, run);
    }

    std::cout << "----------------------------------------\n";
    std::cout << reports.size() << " chain(s) analyzed, " << incomplete_count
              << " incomplete.\n";
    if (!options.show_complete_chains) {
        std::cout << "(pass --show-complete to also print fully-reached chains)\n";
    }

    return 0;
}

}  // namespace kdiag
