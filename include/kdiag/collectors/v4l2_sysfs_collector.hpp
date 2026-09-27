// kdiag/collectors/v4l2_sysfs_collector.hpp
//
// Enumerates /sys/class/video4linux/videoN nodes to answer the first
// question any driver-failure investigation needs: does the device node
// exist at all right now, and what does the kernel think it's called and
// which driver/device it's bound to? This is deliberately done via
// `find`+`cat` through the CommandRunner (not std::filesystem) so the
// exact same collector code path is exercised by both the real run and
// unit tests with a fake runner — no #ifdef TESTING branches.
#pragma once

#include "kdiag/collector.hpp"

namespace kdiag {

class V4l2SysfsCollector : public Collector {
public:
    /// Borrow the command runner used to enumerate V4L2 sysfs nodes.
    explicit V4l2SysfsCollector(CommandRunner& runner) : runner_(runner) {}

    /// Return the source label used in progress output and event metadata.
    std::string Name() const override { return "sysfs_v4l2"; }

    /// Enumerate current nodes, including an event when none are found.
    std::vector<Event> Collect() override;

    // `listing` is the output of
    //   for d in /sys/class/video4linux/*; do
    //     echo "NODE:$d"; echo "NAME:$(cat $d/name 2>/dev/null)";
    //     echo "INDEX:$(cat $d/index 2>/dev/null)";
    //     echo "DEVICE:$(readlink -f $d/device 2>/dev/null)";
    //     echo "---";
    //   done
    // (see Collect() for the exact shell pipeline).
    /// Parse the collector's line-oriented node listing into snapshot events.
    std::vector<Event> ParseOutput(const std::string& listing) const;

private:
    CommandRunner& runner_;
};

}  // namespace kdiag
