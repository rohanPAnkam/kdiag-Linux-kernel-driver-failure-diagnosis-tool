// kdiag/collectors/journalctl_collector.hpp
//
// On systems where the kernel ring buffer has already wrapped past the
// failure (common on long-uptime embedded boxes), `journalctl -k` can
// reach further back than `dmesg`. We ask for ISO 8601 wall-clock
// timestamps and convert to nanoseconds-since-epoch — a *different*
// clock domain than DmesgCollector's boot-relative monotonic seconds.
// fields["clock_domain"] on every event this collector emits records
// which domain it's in ("wall_clock_ns") so the Month-2 correlator does
// not silently compare incomparable timestamps; reconciling the two
// domains (via a boottime/walltime offset sampled once per run) is
// explicitly a Month-2 task, not solved here.
#pragma once

#include <utility>

#include "kdiag/collector.hpp"
#include "kdiag/kernel_line_classifier.hpp"

namespace kdiag {

class JournalctlCollector : public Collector {
public:
    // `since` is passed verbatim to `journalctl -k --since <since>`,
    // e.g. "-10 min" or "2026-09-27 07:00:00". Empty means "today".
    /// Borrow the runner and configure the time window and driver marker.
    JournalctlCollector(CommandRunner& runner, std::string since,
                        std::string driver_name = "driver_name")
        : runner_(runner), since_(std::move(since)),
          classifier_(std::move(driver_name)) {}

    /// Return the source label used in progress output and event metadata.
    std::string Name() const override { return "journalctl"; }

    /// Read kernel journal records for the configured time window.
    std::vector<Event> Collect() override;

    // Expects `journalctl -k -o short-iso` formatted lines, e.g.
    //   2026-09-27T07:12:41+0000 host kernel: driver_name: probe failed
    /// Parse journal records and tag their wall-clock time domain.
    std::vector<Event> ParseOutput(const std::string& journalctl_stdout) const;

private:
    CommandRunner& runner_;
    std::string since_;
    KernelLineClassifier classifier_;
};

}  // namespace kdiag
