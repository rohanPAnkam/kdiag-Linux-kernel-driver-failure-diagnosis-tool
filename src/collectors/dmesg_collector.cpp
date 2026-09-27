// Parses dmesg lines, retaining boot-relative timestamps and raw evidence.
#include "kdiag/collectors/dmesg_collector.hpp"

#include <cstdlib>
#include <sstream>

namespace kdiag {

namespace {

// Parses a leading "[   12.345678] " prefix. Returns true and sets
// *seconds/*rest on success; false if the line doesn't start with the
// expected bracketed-float format (kdiag then falls back to
// timestamp_ns=0 and still keeps the line — a line without a parseable
// timestamp is still evidence, just unordered evidence).
bool ParseTimestampPrefix(const std::string& line, double* seconds, std::string* rest) {
    if (line.empty() || line[0] != '[') return false;
    const auto close = line.find(']');
    if (close == std::string::npos) return false;

    const std::string bracket = line.substr(1, close - 1);
    char* end = nullptr;
    const double value = std::strtod(bracket.c_str(), &end);
    if (end == bracket.c_str()) return false;  // no digits parsed

    *seconds = value;
    // Skip the "] " that follows the bracket, if present.
    size_t rest_start = close + 1;
    while (rest_start < line.size() && (line[rest_start] == ' ')) ++rest_start;
    *rest = line.substr(rest_start);
    return true;
}

}  // namespace

std::vector<Event> DmesgCollector::ParseOutput(const std::string& dmesg_stdout) const {
    std::vector<Event> events;
    std::istringstream stream(dmesg_stdout);
    std::string line;

    while (std::getline(stream, line)) {
        if (line.empty()) continue;

        double seconds = 0.0;
        std::string message = line;
        int64_t timestamp_ns = 0;

        if (ParseTimestampPrefix(line, &seconds, &message)) {
            timestamp_ns = static_cast<int64_t>(seconds * 1e9);
        }

        const Classification classification = classifier_.Classify(message);

        Event e;
        e.timestamp_ns = timestamp_ns;
        e.source = Source::kDmesg;
        e.component = classification.component;
        e.event = classification.event;
        e.message = message;
        e.raw = line;
        events.push_back(std::move(e));
    }
    return events;
}

std::vector<Event> DmesgCollector::Collect() {
    const CommandResult result = runner_.Run({"dmesg"});

    if (!result.ok()) {
        Event error_event;
        error_event.source = Source::kDmesg;
        error_event.component = Component::kUnknown;
        error_event.event = "collector_error";
        error_event.message = "dmesg exited with code " + std::to_string(result.exit_code) +
                               " (are you root, or in the 'adm'/'syslog' group?)";
        error_event.raw = result.stdout_text;
        return {error_event};
    }

    return ParseOutput(result.stdout_text);
}

}  // namespace kdiag
