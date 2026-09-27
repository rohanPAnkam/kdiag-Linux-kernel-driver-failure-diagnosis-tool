// kdiag/collect_command.hpp
//
// Implements `kdiag collect`: runs every collector, merges + sorts the
// resulting events into one CollectionRun, and writes it to
// <output_dir>/events.json. This is deliberately the *only* place that
// knows the full list of collectors — adding a Month-2/3 data source
// later means adding one line here, not touching the collectors
// themselves.
#pragma once

#include <string>

namespace kdiag {

struct CollectOptions {
    std::string output_dir = "./capture";  ///< Directory receiving `events.json`.
    std::string pci_bdf;  ///< Optional PCI BDF used to scope `lspci`.
    std::string driver_name;  ///< Optional kernel module name for log classification.
    std::string journalctl_since;  ///< Optional `journalctl -k --since` value.
    bool quiet = false;  ///< Suppress progress output.
};

// Returns the process exit code (0 on success). Writes progress to
// stdout unless options.quiet.
int RunCollectCommand(const CollectOptions& options);

}  // namespace kdiag
