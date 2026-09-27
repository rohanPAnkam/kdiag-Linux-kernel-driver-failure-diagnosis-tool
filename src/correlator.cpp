// Builds per-buffer stage chains and identifies each chain's next missing stage.
#include "kdiag/correlator.hpp"

#include <algorithm>
#include <map>
#include <regex>

namespace kdiag {

namespace {

// Matches "buffer 1832", "buffer #1832", "buf 1832", "frame 12" etc.,
// case-insensitively. Deliberately conservative (word boundary before
// the keyword, digits only) to avoid false positives like matching the
// "3" in "video3" as a buffer id.
const std::regex& BufferIdPattern() {
    static const std::regex kPattern(
        R"(\b(?:buffer|buf|frame)\s*#?\s*(\d+)\b)",
        std::regex::icase);
    return kPattern;
}

}  // namespace

std::optional<int64_t> Correlator::ExtractBufferId(const std::string& message) {
    std::smatch match;
    if (std::regex_search(message, match, BufferIdPattern())) {
        return std::stoll(match[1].str());
    }
    return std::nullopt;
}

std::vector<StageOccurrence> Correlator::ExtractStageOccurrences() const {
    std::vector<StageOccurrence> occurrences;
    for (size_t i = 0; i < events_.size(); ++i) {
        const Event& e = events_[i];

        // Snapshot events (/proc/interrupts, lspci) describe cumulative
        // or current state, not a point in a buffer's causal timeline —
        // see kSnapshotTimestampNs in event.hpp — so they're excluded
        // from stage extraction even if their text happens to match a
        // stage keyword.
        if (e.timestamp_ns == kSnapshotTimestampNs) continue;

        const auto stage = stage_classifier_.Classify(e.message);
        if (!stage.has_value()) continue;

        StageOccurrence occ;
        occ.stage = *stage;
        occ.timestamp_ns = e.timestamp_ns;
        occ.buffer_id = ExtractBufferId(e.message);
        occ.event_index = i;
        occurrences.push_back(occ);
    }
    return occurrences;
}

namespace {

// Builds a ChainReport from a set of stage occurrences already merged
// for one buffer (or the session-only chain). Picks the earliest
// occurrence of each stage.
//
// KNOWN LIMITATION (intentional Month-2 scope boundary, see
// correlator.hpp): `first_missing_stage` is "the pipeline stage
// immediately after the highest-index stage actually observed," not a
// verified single point of failure. A driver that simply never logs a
// device_init line on the happy path would show that as a false-
// positive gap. Distinguishing "never instrumented" from "genuinely
// failed here" requires per-driver expected-coverage knowledge. Month 3
// rules only surface selected patterns as candidates; this function's
// job is to report the gap, not confirm it as a fault.
ChainReport BuildChainReport(std::optional<int64_t> buffer_id,
                              const std::vector<StageOccurrence>& merged_occurrences) {
    ChainReport report;
    report.buffer_id = buffer_id;

    // Earliest occurrence per stage.
    std::map<Stage, StageOccurrence> earliest;
    for (const auto& occ : merged_occurrences) {
        auto it = earliest.find(occ.stage);
        if (it == earliest.end() || occ.timestamp_ns < it->second.timestamp_ns) {
            earliest[occ.stage] = occ;
        }
    }

    std::optional<Stage> highest_present;
    for (Stage s : PipelineOrder()) {
        auto it = earliest.find(s);
        if (it != earliest.end()) {
            report.reached_stages.push_back(it->second);
            highest_present = s;
        }
    }

    const auto& order = PipelineOrder();
    report.complete = (earliest.size() == order.size());
    report.last_reached_stage = highest_present;

    if (!report.complete) {
        if (!highest_present.has_value()) {
            report.first_missing_stage = order.front();
        } else {
            const auto it = std::find(order.begin(), order.end(), *highest_present);
            const auto next_it = std::next(it);
            report.first_missing_stage = (next_it != order.end())
                ? std::optional<Stage>(*next_it)
                : std::nullopt;  // highest_present == last pipeline stage but complete==false
                                  // is unreachable given the size check above, kept defensive.
        }
    }

    return report;
}

}  // namespace

std::vector<ChainReport> Correlator::Correlate() const {
    const std::vector<StageOccurrence> occurrences = ExtractStageOccurrences();

    std::vector<StageOccurrence> session_scoped;
    std::map<int64_t, std::vector<StageOccurrence>> buffer_scoped;

    for (const auto& occ : occurrences) {
        if (occ.buffer_id.has_value()) {
            buffer_scoped[*occ.buffer_id].push_back(occ);
        } else {
            session_scoped.push_back(occ);
        }
    }

    std::vector<ChainReport> reports;

    if (buffer_scoped.empty()) {
        // No buffer numbers appeared anywhere in this run's logs — fall
        // back to a single session-wide chain so the collector still
        // produces a useful report on drivers/log formats that don't
        // include per-buffer numbers.
        reports.push_back(BuildChainReport(std::nullopt, session_scoped));
        return reports;
    }

    for (auto& [buffer_id, occs] : buffer_scoped) {
        std::vector<StageOccurrence> merged = occs;
        merged.insert(merged.end(), session_scoped.begin(), session_scoped.end());
        reports.push_back(BuildChainReport(buffer_id, merged));
    }
    return reports;
}

}  // namespace kdiag
