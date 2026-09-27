// Extracts PCI link/device state from lspci while preserving the full dump.
#include "kdiag/collectors/pci_collector.hpp"

#include <sstream>

namespace kdiag {

namespace {

// Remove surrounding whitespace from an lspci detail line.
std::string Trim(const std::string& s) {
    const auto start = s.find_first_not_of(" \t");
    if (start == std::string::npos) return "";
    const auto end = s.find_last_not_of(" \t\r");
    return s.substr(start, end - start + 1);
}

}  // namespace

std::vector<Event> PciCollector::ParseOutput(const std::string& lspci_stdout) const {
    std::vector<Event> events;
    if (lspci_stdout.empty()) return events;

    // One event carrying the full dump, for evidence/citation purposes.
    Event dump;
    dump.timestamp_ns = kSnapshotTimestampNs;  // see note below
    dump.source = Source::kLspci;
    dump.component = Component::kPci;
    dump.event = "pci_verbose_dump";
    dump.message = "full `lspci -vvv" + (bdf_.empty() ? std::string() : (" -s " + bdf_)) + "` output";
    dump.raw = lspci_stdout;
    events.push_back(dump);

    std::istringstream stream(lspci_stdout);
    std::string line;
    std::string current_device_header;

    while (std::getline(stream, line)) {
        if (line.empty()) continue;

        // Device header lines start at column 0, e.g.
        // "03:00.0 Multimedia controller: ..."
        if (!line.empty() && line[0] != '\t' && line[0] != ' ') {
            current_device_header = line;
            continue;
        }

        const std::string trimmed = Trim(line);

        struct Match {
            std::string prefix;
            std::string event_tag;
        };
        static const std::vector<Match> kMatches = {
            {"LnkSta:", "pci_link_status"},
            {"LnkCap:", "pci_link_capability"},
            {"DevSta:", "pci_device_status"},
            {"Kernel driver in use:", "pci_kernel_driver"},
            {"Kernel modules:", "pci_kernel_modules"},
            {"Status:", "pci_status_register"},
        };

        for (const auto& m : kMatches) {
            if (trimmed.compare(0, m.prefix.size(), m.prefix) == 0) {
                Event e;
                e.timestamp_ns = kSnapshotTimestampNs;
                e.source = Source::kLspci;
                e.component = Component::kPci;
                e.event = m.event_tag;
                e.message = current_device_header + " | " + trimmed;
                e.raw = line;
                e.fields = {{"device_header", current_device_header}, {"snapshot", true}};
                events.push_back(std::move(e));
                break;
            }
        }
    }
    return events;
}

std::vector<Event> PciCollector::Collect() {
    std::vector<std::string> command = {"lspci", "-vvv"};
    if (!bdf_.empty()) {
        command.push_back("-s");
        command.push_back(bdf_);
    }
    const CommandResult result = runner_.Run(command);

    if (!result.ok()) {
        Event error_event;
        error_event.source = Source::kLspci;
        error_event.component = Component::kPci;
        error_event.event = "collector_error";
        error_event.message = "`lspci -vvv` failed (exit " + std::to_string(result.exit_code) +
                               "); -vvv usually needs root for full register decode";
        error_event.raw = result.stdout_text;
        return {error_event};
    }
    return ParseOutput(result.stdout_text);
}

}  // namespace kdiag
