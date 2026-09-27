// kdiag/collectors/dmesg_collector.hpp
//
// Reads the kernel ring buffer via `dmesg` (default, unprefixed format:
// `[   12.345678] message text`) and classifies each line with
// KernelLineClassifier. The bracketed float is seconds since boot as
// measured by CLOCK_MONOTONIC — the same clock domain ftrace and
// /proc/uptime use — which is exactly why we don't ask for `dmesg -T`
// (wall clock, harder to correlate precisely, and subject to NTP steps).
#pragma once

#include <string>
#include <utility>

#include "kdiag/collector.hpp"
#include "kdiag/kernel_line_classifier.hpp"

namespace kdiag {

class DmesgCollector : public Collector {
public:
    /// Borrow a runner and classify driver-specific lines by module name.
    explicit DmesgCollector(CommandRunner& runner,
                            std::string driver_name = "driver_name")
        : runner_(runner), classifier_(std::move(driver_name)) {}

    /// Return the source label used in progress output and event metadata.
    std::string Name() const override { return "dmesg"; }

    /// Read and parse the current kernel ring buffer.
    std::vector<Event> Collect() override;

    // Exposed for unit testing: parses raw `dmesg` stdout text into
    // events without touching the system.
    /// Return one normalized event per parsed kernel line.
    std::vector<Event> ParseOutput(const std::string& dmesg_stdout) const;

private:
    CommandRunner& runner_;
    KernelLineClassifier classifier_;
};

}  // namespace kdiag
