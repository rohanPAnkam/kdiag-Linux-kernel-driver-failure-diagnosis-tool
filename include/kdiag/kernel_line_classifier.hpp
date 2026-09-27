// kdiag/kernel_line_classifier.hpp
//
// dmesg and `journalctl -k` both ultimately emit kernel ring-buffer lines
// in slightly different wrappers. Rather than duplicate classification
// logic in two collectors, both call into this shared classifier, which
// takes one already-timestamp-stripped line of kernel log text and
// decides (a) which Component it belongs to and (b) a short event tag.
//
// The rule set here is intentionally small and readable rather than
// exhaustive — Month 1's job is a *correct, honest* skeleton the Month-3
// diagnosis engine can build deterministic rules on top of, not a
// complete driver-failure taxonomy. Extending the pattern table is the
// expected way to grow this; each new driver/subsystem you debug should
// add a line or two here.
#pragma once

#include <string>
#include <vector>

#include "kdiag/event.hpp"

namespace kdiag {

struct Classification {
    Component component = Component::kKernel;
    std::string event;  // e.g. "dma_timeout"; empty means "generic_kernel_line"
};

// Stateless; safe to share across threads/collectors.
class KernelLineClassifier {
public:
    /// Match generic driver log lines using the configured module name.
    explicit KernelLineClassifier(std::string driver_name = "driver_name");

    // `line` should already have any leading `[   12.345678]` or
    // journalctl timestamp/hostname prefix stripped by the caller.
    /// Classify one timestamp-stripped kernel line; first matching rule wins.
    Classification Classify(const std::string& line) const;

private:
    struct Rule {
        std::string substring;   // case-sensitive match, checked with find()
        Component component;
        std::string event_tag;
    };

    std::vector<Rule> rules_;
};

}  // namespace kdiag
