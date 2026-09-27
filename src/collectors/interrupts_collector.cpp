// Parses numeric /proc/interrupts rows into cumulative IRQ snapshot events.
#include "kdiag/collectors/interrupts_collector.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <sstream>

namespace kdiag {

namespace {

// A typical line:
//   16:         12          0   IO-APIC   16-fasteoi   driver_name
// Fields: irq label, one count per CPU column, chip type, chip name,
// then a comma-separated list of driver names sharing the line. The
// number of CPU columns is only known from the header line, so we
// tokenize generically: irq label, then tokens that parse as integers
// are counts, the first non-numeric token after them is the chip type,
// the rest of the line is the action/driver-name list.
struct ParsedLine {
    bool valid = false;
    std::string irq_label;
    int64_t total_count = 0;
    std::string chip;
    std::string actions;
};

ParsedLine ParseInterruptLine(const std::string& line) {
    ParsedLine result;
    std::istringstream tokens(line);
    std::string first;
    if (!(tokens >> first)) return result;
    if (first.empty() || first.back() != ':') return result;  // not an "NN:" IRQ line

    result.irq_label = first.substr(0, first.size() - 1);

    // Real hardware IRQ lines are labeled with a bare number ("16:").
    // Per-CPU architecture counters use alphabetic labels instead (NMI,
    // LOC, SPU, PMI, IWI, RES, CAL, TLB, TRM, THR, DFR, MCE, MCP, ERR,
    // MIS, ...) and aren't associated with any driver/device, so they're
    // out of scope for Month 1's driver-failure diagnosis and are
    // skipped rather than misreported as an "IRQ" belonging to nothing.
    const bool all_digits = !result.irq_label.empty() &&
        std::all_of(result.irq_label.begin(), result.irq_label.end(), ::isdigit);
    if (!all_digits) return result;  // result.valid stays false

    std::string tok;
    std::vector<std::string> remaining;
    while (tokens >> tok) remaining.push_back(tok);

    size_t i = 0;
    for (; i < remaining.size(); ++i) {
        char* end = nullptr;
        const long long value = std::strtoll(remaining[i].c_str(), &end, 10);
        if (end != remaining[i].c_str() && *end == '\0') {
            result.total_count += value;
        } else {
            break;  // first non-integer token: start of chip/action fields
        }
    }
    if (i < remaining.size()) {
        result.chip = remaining[i];
        ++i;
    }
    for (; i < remaining.size(); ++i) {
        if (!result.actions.empty()) result.actions += ' ';
        result.actions += remaining[i];
    }

    result.valid = true;
    return result;
}

}  // namespace

std::vector<Event> InterruptsCollector::ParseOutput(const std::string& proc_interrupts_text) const {
    std::vector<Event> events;
    std::istringstream stream(proc_interrupts_text);
    std::string line;

    // First line is the CPU column header; skip it.
    bool first_line = true;
    while (std::getline(stream, line)) {
        if (first_line) {
            first_line = false;
            continue;
        }
        if (line.empty()) continue;

        const ParsedLine parsed = ParseInterruptLine(line);
        if (!parsed.valid) continue;  // footer lines (NMI, LOC, ...) with non-numeric labels are skipped for now

        Event e;
        e.timestamp_ns = kdiag::kSnapshotTimestampNs;
        e.source = Source::kProcInterrupts;
        e.component = Component::kIrq;
        e.event = "irq_snapshot";
        e.message = "IRQ " + parsed.irq_label + " (" + parsed.chip + "): " +
                    std::to_string(parsed.total_count) + " total, actions=[" + parsed.actions + "]";
        e.raw = line;
        e.fields = {
            {"snapshot", true},
            {"irq", parsed.irq_label},
            {"chip", parsed.chip},
            {"total_count", parsed.total_count},
            {"actions", parsed.actions},
        };
        events.push_back(std::move(e));
    }
    return events;
}

std::vector<Event> InterruptsCollector::Collect() {
    const CommandResult result = runner_.Run({"cat", "/proc/interrupts"});
    if (!result.ok()) {
        Event error_event;
        error_event.timestamp_ns = kdiag::kSnapshotTimestampNs;
        error_event.source = Source::kProcInterrupts;
        error_event.event = "collector_error";
        error_event.message = "failed to read /proc/interrupts (exit " +
                               std::to_string(result.exit_code) + ")";
        error_event.raw = result.stdout_text;
        return {error_event};
    }
    return ParseOutput(result.stdout_text);
}

}  // namespace kdiag
