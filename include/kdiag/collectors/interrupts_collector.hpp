// kdiag/collectors/interrupts_collector.hpp
//
// /proc/interrupts is a *snapshot* (cumulative counts since boot), not a
// timeline — there's no per-line timestamp to parse. We still model each
// IRQ line as an Event (rather than a separate struct) so it flows
// through the same merge/sort/JSON pipeline as everything else, but we
// mark it explicitly: fields["snapshot"] == true and
// timestamp_ns == kSnapshotTimestampNs. The Month-2 correlator is
// expected to treat snapshot events differently from timeline events —
// e.g. diffing two collection runs' interrupt counts for the same IRQ
// rather than placing a single snapshot inside a causal chain. Presenting
// a cumulative counter as if it were a point-in-time timeline event would
// be exactly the kind of invented precision the project's diagnosis
// engine is supposed to avoid.
#pragma once

#include <cstdint>

#include "kdiag/collector.hpp"

namespace kdiag {

class InterruptsCollector : public Collector {
public:
    /// Borrow the command runner used to read `/proc/interrupts`.
    explicit InterruptsCollector(CommandRunner& runner) : runner_(runner) {}

    /// Return the source label used in progress output and event metadata.
    std::string Name() const override { return "proc_interrupts"; }

    /// Read IRQ counts as snapshot events, not timeline occurrences.
    std::vector<Event> Collect() override;

    // Exposed for unit testing against captured /proc/interrupts text.
    /// Parse numeric IRQ rows and preserve per-CPU totals in event fields.
    std::vector<Event> ParseOutput(const std::string& proc_interrupts_text) const;

private:
    CommandRunner& runner_;
};

}  // namespace kdiag
