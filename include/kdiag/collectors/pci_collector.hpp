// kdiag/collectors/pci_collector.hpp
//
// Runs `lspci -vvv [-s <bdf>]` and extracts the handful of lines that
// matter for driver-failure diagnosis: link status/speed (a downgraded
// or flapping PCIe link is a classic cause of DMA/IRQ symptoms), current
// device status flags, and which kernel driver/module is actually bound.
// The full verbose dump is kept as one "pci_verbose_dump" event so later
// analysis can grep it even though we don't structure every field today.
#pragma once

#include "kdiag/collector.hpp"

namespace kdiag {

class PciCollector : public Collector {
public:
    // `bdf` is the PCI Bus:Device.Function address, e.g. "0000:03:00.0".
    // Empty means "dump every PCI device" (useful when you don't yet
    // know which device is the camera).
    /// Borrow the command runner and optionally scope collection to one BDF.
    PciCollector(CommandRunner& runner, std::string bdf)
        : runner_(runner), bdf_(std::move(bdf)) {}

    /// Return the source label used in progress output and event metadata.
    std::string Name() const override { return "lspci"; }

    /// Collect PCI status, link capability, and the full verbose dump.
    std::vector<Event> Collect() override;

    /// Convert `lspci -vvv` output into snapshot events with raw evidence.
    std::vector<Event> ParseOutput(const std::string& lspci_stdout) const;

private:
    CommandRunner& runner_;
    std::string bdf_;
};

}  // namespace kdiag
