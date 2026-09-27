// kdiag/collector.hpp
//
// Common interface for everything that produces Events. `kdiag collect`
// (src/collect_command.cpp) owns a list of these and merges their output
// into one CollectionRun. Each collector:
//   - takes a CommandRunner by reference (never owns/creates one), so
//     tests can substitute a fake;
//   - never throws for "source unavailable" (e.g. no PCI device, no
//     video4linux nodes) — it returns as many events as it honestly
//     could and records a diagnostic event with event=="collector_error"
//     if a source it expected to read was missing. Partial data is the
//     normal case on a real embedded box (you often run kdiag *because*
//     the camera isn't there), so collectors must degrade gracefully.
#pragma once

#include <string>
#include <vector>

#include "kdiag/event.hpp"
#include "kdiag/exec.hpp"

namespace kdiag {

class Collector {
public:
    virtual ~Collector() = default;

    // Human-readable name for logging/progress output, e.g. "dmesg".
    /// Return the short source name used in collection progress output.
    virtual std::string Name() const = 0;

    // Performs the collection and returns whatever events it could
    // produce. Must not throw for expected failure modes (missing
    // device, permission denied, tool not installed) — emit a
    // "collector_error" event instead so the failure itself becomes
    // evidence for the Month-3 diagnosis engine ("we couldn't read
    // /proc/interrupts" is itself diagnostically relevant).
    virtual std::vector<Event> Collect() = 0;
};

}  // namespace kdiag
