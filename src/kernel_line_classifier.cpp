// Maps kernel log substrings to normalized components and event tags.
#include "kdiag/kernel_line_classifier.hpp"

#include <algorithm>
#include <utility>

namespace kdiag {

// Ordered most-specific-first: the first substring match wins, so put
// narrower event tags before broad subsystem catch-alls.
KernelLineClassifier::KernelLineClassifier(std::string driver_name) {
    rules_ = {
        // --- DMA ---
        {"dma_fence_wait timed out",  Component::kDma, "dma_fence_timeout"},
        {"DMA timeout",               Component::kDma, "dma_timeout"},
        {"dma timeout",               Component::kDma, "dma_timeout"},
        {"DMA-API",                   Component::kDma, "dma_api_warning"},
        {"dma_map_",                  Component::kDma, "dma_mapping"},
        {"dma_unmap_",                Component::kDma, "dma_unmapping"},
        {"descriptor",                Component::kDma, "dma_descriptor"},

        // --- IRQ ---
        {"irq_handler",               Component::kIrq, "irq_handler"},
        {"IRQ", Component::kIrq, "irq_generic"},
        {"nobody cared",              Component::kIrq, "irq_unhandled"},
        {"spurious",                  Component::kIrq, "irq_spurious"},
        {"MSI-X",                     Component::kIrq, "msix_config"},
        {"genirq",                    Component::kIrq, "irq_core"},

        // --- PCI ---
        {"pci_bus_",                  Component::kPci, "pci_bus_event"},
        {"AER:",                      Component::kPci, "pci_aer_error"},
        {"link is down",              Component::kPci, "pci_link_down"},
        {"Link Down",                 Component::kPci, "pci_link_down"},
        {"pcieport",                  Component::kPci, "pcie_port_event"},
        {"BAR ",                      Component::kPci, "pci_bar"},

        // --- V4L2 / driver ---
        {"vb2_buffer_done",           Component::kV4l2, "vb2_buffer_done"},
        {"vb2_queue_",                Component::kV4l2, "vb2_queue_event"},
        {"v4l2_",                     Component::kV4l2, "v4l2_event"},
        {"videobuf2",                 Component::kV4l2, "videobuf2_event"},

        // --- Generic kernel fallback markers ---
        {"Call Trace:",               Component::kKernel, "kernel_oops_trace"},
        {"BUG:",                      Component::kKernel, "kernel_bug"},
        {"WARNING:",                  Component::kKernel, "kernel_warning"},
        {"Kernel panic",              Component::kKernel, "kernel_panic"},
    };
    if (!driver_name.empty()) {
        const auto fallback = std::find_if(rules_.begin(), rules_.end(),
            [](const Rule& rule) { return rule.event_tag == "kernel_oops_trace"; });
        rules_.insert(fallback,
                      {std::move(driver_name), Component::kDriver, "driver_log"});
    }
}

Classification KernelLineClassifier::Classify(const std::string& line) const {
    for (const auto& rule : rules_) {
        if (line.find(rule.substring) != std::string::npos) {
            return {rule.component, rule.event_tag};
        }
    }
    return {Component::kKernel, "generic_kernel_line"};
}

}  // namespace kdiag
