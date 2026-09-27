// kdiag/event.hpp
//
// The normalized event format is the contract between Month 1 (collectors)
// and Month 2 (correlation engine). Every collector, regardless of source
// (dmesg, PCI config space, /proc/interrupts, V4L2 sysfs, journalctl,
// GStreamer logs), must reduce what it sees to a stream of Event objects.
//
// Design goals:
//   - Timestamps are always monotonic-clock nanoseconds since boot where
//     the source provides one (dmesg, ftrace); wall-clock sources
//     (journalctl, GStreamer logs) are converted at collection time so
//     every event in the merged timeline is comparable.
//   - `component` is a closed, small vocabulary (see Component enum) so
//     the Month-2 correlator can pattern-match without string comparison.
//   - `fields` carries source-specific structured data (register values,
//     IRQ numbers, PCI BDF, buffer indices) without polluting the core
//     schema. It round-trips through JSON as an arbitrary object.
//   - Every event keeps `raw` (the original line/record) for evidence
//     citation in the Month-4 LLM report ("evidence must cite exact
//     source lines, never invented").

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace kdiag {

// Closed vocabulary of subsystems an event can belong to. Keep this in
// sync with docs/event-schema.md. Adding a component here is a
// deliberate schema change, not something a collector should do
// unilaterally by emitting a free-form string.
enum class Component {
    kKernel,      // generic dmesg lines not attributable to a subsystem below
    kDma,
    kIrq,
    kPci,
    kV4l2,
    kGstreamer,
    kDriver,      // driver_name-specific log lines (module name matched)
    kUnknown,
};

/// Return the stable JSON/display name for a component value.
std::string ToString(Component c);
/// Parse a component name; unknown names map to `Component::kUnknown`.
Component ComponentFromString(const std::string& s);

// Which tool produced the event. Kept distinct from Component: a single
// source (dmesg) can yield events across several components.
// Sentinel timestamp for snapshot-sourced events (e.g. /proc/interrupts,
// `lspci -vvv`) that describe cumulative/current state rather than a
// point in the causal timeline. Placed at the end of the monotonic range
// so snapshots sort after all timeline events instead of appearing to
// have happened at t=0. Shared across collectors so every snapshot event
// is comparable/filterable by this one constant.
inline constexpr int64_t kSnapshotTimestampNs = INT64_MAX;

enum class Source {
    kDmesg,
    kJournalctl,
    kProcInterrupts,
    kSysfsV4l2,
    kLspci,
    kGstreamerLog,
    kFtrace,
};

/// Return the stable JSON/display name for an event source.
std::string ToString(Source s);

// A single normalized observation in the timeline.
struct Event {
    // Nanoseconds since an arbitrary but fixed epoch for this collection
    // run (CLOCK_MONOTONIC at collector start, or converted wall-clock
    // for sources that only give wall-clock timestamps). Never compare
    // across separate `kdiag collect` runs.
    int64_t timestamp_ns = 0;

    Source source = Source::kDmesg;
    Component component = Component::kUnknown;

    // Short machine-matchable event tag, e.g. "dma_timeout",
    // "irq_handled", "buffer_dequeued", "pci_link_down". The Month-2
    // correlator switches on this, not on `message`.
    std::string event;

    // Human-readable original text, free-form.
    std::string message;

    // Exact original line/record this event was derived from, kept
    // verbatim for evidence citation. May be identical to `message`.
    std::string raw;

    // Source-specific structured payload (IRQ number, PCI BDF, buffer
    // index, register value, ...). Arbitrary JSON object.
    nlohmann::json fields = nlohmann::json::object();

    /// Serialize this event using the normalized event schema.
    nlohmann::json ToJson() const;

    /// Parse a normalized event; throws if required fields are missing or invalid.
    static Event FromJson(const nlohmann::json& j);
};

// A collection run: device identity + the merged, time-sorted event list.
// This is exactly what `kdiag collect` writes to <output>/events.json and
// what Month 2's correlator reads back in.
struct CollectionRun {
    std::string pci_bdf;        // e.g. "0000:03:00.0"
    std::string driver_name;    // e.g. "driver_name"
    std::string kernel_release; // uname -r
    int64_t collected_at_unix_s = 0;

    std::vector<Event> events;

    // Sorts `events` by timestamp_ns. Collectors append in whatever
    // order they run; call this once after merging all collector output.
    /// Stable-sort events by timestamp while preserving order for equal times.
    void SortByTime();

    /// Serialize run metadata and all events to the collection JSON schema.
    nlohmann::json ToJson() const;

    /// Parse collection metadata and events; throws on malformed required data.
    static CollectionRun FromJson(const nlohmann::json& j);
};

}  // namespace kdiag
