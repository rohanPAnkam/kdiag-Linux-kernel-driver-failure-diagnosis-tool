// kdiag/correlator.hpp
//
// Turns a flat, time-sorted Event list into per-buffer causal chains and
// finds the first point where a chain fails to reach the next expected
// Stage. This is Month 2 of the project brief — "turn IRQ/DMA/V4L2/PCI/
// GStreamer into a timeline, then detect where the chain breaks."
//
// Two kinds of stage events exist in practice:
//   - buffer-scoped: log lines that name a specific buffer/frame number
//     ("descriptor submitted for buffer 1832", "vb2_buffer_done: buffer
//     3"). These build one chain per buffer id.
//   - session-scoped: lines with no buffer number (PCI probe, device
//     init, "streaming started") that happen once per capture session
//     and apply as context to every buffer's chain.
// A buffer's *effective* chain is the union of its buffer-scoped stage
// events and every session-scoped stage event, which mirrors how a real
// V4L2 pipeline actually behaves (you don't re-probe the PCI device once
// per frame).
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "kdiag/event.hpp"
#include "kdiag/stage.hpp"

namespace kdiag {

// One occurrence of a Stage in the timeline, with a pointer back to the
// originating Event for evidence citation.
struct StageOccurrence {
    Stage stage;  ///< Classified pipeline stage.
    int64_t timestamp_ns;  ///< Timestamp copied from the source event.
    std::optional<int64_t> buffer_id;  ///< Empty for a session-scoped occurrence.
    size_t event_index;  ///< Index of the source event in the original input vector.
};

// The result of correlating one buffer's chain: how far it got, and
// where (if anywhere) it broke.
struct ChainReport {
    std::optional<int64_t> buffer_id;  ///< Empty for a session-only chain.
    std::vector<StageOccurrence> reached_stages;  ///< Earliest occurrence per stage, in pipeline order.
    bool complete = false;  ///< True when every pipeline stage was observed.

    // Populated when !complete: the last stage the chain reached, and
    // the next stage in the pipeline that never showed up.
    std::optional<Stage> last_reached_stage;  ///< Highest pipeline stage observed, if any.
    std::optional<Stage> first_missing_stage;  ///< Next stage after the highest stage observed.
};

class Correlator {
public:
    /// Take ownership of the event list used to build causal chains.
    explicit Correlator(std::vector<Event> events) : events_(std::move(events)) {}

    // Extracts a buffer/frame number from `message` if one is present,
    // e.g. "buffer 1832", "buf #1832", "frame 12". Returns nullopt if no
    // recognizable numeric buffer reference is found. Exposed publicly
    // for unit testing in isolation.
    static std::optional<int64_t> ExtractBufferId(const std::string& message);

    // Runs stage classification + buffer-id extraction over every event
    // once and caches the result; safe to call Correlate() multiple
    // times afterward without re-scanning.
    std::vector<StageOccurrence> ExtractStageOccurrences() const;

    // Builds one ChainReport per distinct buffer id seen, plus one
    // additional report (buffer_id == nullopt) covering session-scoped
    // stages only, if no buffer ids were found at all in the run.
    std::vector<ChainReport> Correlate() const;

private:
    std::vector<Event> events_;
    StageClassifier stage_classifier_;
};

}  // namespace kdiag
