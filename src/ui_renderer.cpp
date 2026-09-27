// Formats correlated stages as a compact, plain-text pipeline timeline.
#include "kdiag/ui_renderer.hpp"

#include <iomanip>
#include <sstream>

#include "kdiag/event.hpp"

namespace kdiag {

namespace {

// Render a timestamp in seconds, or identify a snapshot with no event time.
std::string FormatTimestamp(int64_t timestamp_ns) {
    if (timestamp_ns == kSnapshotTimestampNs) return "snapshot";
    std::ostringstream output;
    output << std::fixed << std::setprecision(6)
           << static_cast<double>(timestamp_ns) / 1'000'000'000.0 << "s";
    return output.str();
}

}  // namespace

std::string PipelineRenderer::RenderTimeline(const ChainReport& report) const {
    std::ostringstream output;
    output << "Pipeline";
    if (report.buffer_id) output << " for buffer " << *report.buffer_id;
    else output << " for session";
    output << '\n';

    for (const auto& occurrence : report.reached_stages) {
        output << "  [reached] " << ToString(occurrence.stage) << " @ "
               << FormatTimestamp(occurrence.timestamp_ns) << '\n';
    }

    if (report.complete) {
        output << "  [complete] all pipeline stages were observed\n";
    } else if (report.first_missing_stage) {
        output << "  [break] " << ToString(*report.first_missing_stage)
               << " was not observed after the last reached stage\n";
    } else if (report.reached_stages.empty()) {
        output << "  [unknown] no pipeline stage was classified\n";
    }

    return output.str();
}

}  // namespace kdiag