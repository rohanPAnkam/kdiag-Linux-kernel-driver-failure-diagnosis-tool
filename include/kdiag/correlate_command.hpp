// kdiag/correlate_command.hpp
//
// Implements `kdiag correlate`: reads the events.json written by
// `kdiag collect`, runs the Correlator, and prints a human-readable
// timeline + break report per buffer chain. Month 3 adds a separate
// deterministic diagnosis command, and Month 4's explanation layer can
// consume the same ChainReport/Correlator output rather than re-parsing
// text.
#pragma once

#include <string>

namespace kdiag {

struct CorrelateOptions {
    std::string input_path;  // path to events.json from a prior `kdiag collect`
    bool show_complete_chains = false;  // by default, only print chains with a detected gap
};

/// Load a collection file, correlate its events, and print chain reports.
/// @return 0 on success; nonzero if the file cannot be read or parsed.
int RunCorrelateCommand(const CorrelateOptions& options);

}  // namespace kdiag
