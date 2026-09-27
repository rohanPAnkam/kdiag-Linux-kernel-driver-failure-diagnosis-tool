// Parses kernel journal records and converts ISO timestamps to epoch nanoseconds.
#include "kdiag/collectors/journalctl_collector.hpp"

#include <ctime>
#include <sstream>

namespace kdiag {

namespace {

// Parses "2026-09-27T07:12:41+0000" (short-iso, the format we request)
// into nanoseconds since the Unix epoch. Returns 0 on parse failure —
// the event is still kept (with fields["parse_error"]=true) rather than
// dropped, since an unparsable-but-present line is still evidence.
int64_t ParseIso8601ToEpochNs(const std::string& ts, bool* ok) {
    std::tm tm{};
    int tz_hour = 0, tz_min = 0;
    char tz_sign = '+';

    const int matched = std::sscanf(
        ts.c_str(), "%d-%d-%dT%d:%d:%d%c%2d%2d",
        &tm.tm_year, &tm.tm_mon, &tm.tm_mday,
        &tm.tm_hour, &tm.tm_min, &tm.tm_sec,
        &tz_sign, &tz_hour, &tz_min);

    if (matched < 6) {
        *ok = false;
        return 0;
    }
    tm.tm_year -= 1900;
    tm.tm_mon -= 1;

    // timegm treats tm as UTC; we then apply the parsed timezone offset
    // (if present) to get true UTC epoch seconds.
    time_t epoch_s = timegm(&tm);
    if (matched >= 9) {
        const int offset_s = (tz_hour * 3600 + tz_min * 60) * (tz_sign == '-' ? -1 : 1);
        epoch_s -= offset_s;
    }
    *ok = true;
    return static_cast<int64_t>(epoch_s) * 1'000'000'000LL;
}

}  // namespace

std::vector<Event> JournalctlCollector::ParseOutput(const std::string& journalctl_stdout) const {
    std::vector<Event> events;
    std::istringstream stream(journalctl_stdout);
    std::string line;

    while (std::getline(stream, line)) {
        if (line.empty()) continue;

        std::istringstream line_stream(line);
        std::string timestamp_token;
        line_stream >> timestamp_token;

        bool parsed_ok = false;
        const int64_t epoch_ns = ParseIso8601ToEpochNs(timestamp_token, &parsed_ok);

        // Message is everything after "<timestamp> <host> kernel: "
        std::string message = line;
        const auto kernel_marker = line.find("kernel: ");
        if (kernel_marker != std::string::npos) {
            message = line.substr(kernel_marker + std::string("kernel: ").size());
        }

        const Classification classification = classifier_.Classify(message);

        Event e;
        e.timestamp_ns = epoch_ns;
        e.source = Source::kJournalctl;
        e.component = classification.component;
        e.event = classification.event;
        e.message = message;
        e.raw = line;
        e.fields = {{"clock_domain", "wall_clock_ns"}, {"parse_error", !parsed_ok}};
        events.push_back(std::move(e));
    }
    return events;
}

std::vector<Event> JournalctlCollector::Collect() {
    std::vector<std::string> command = {
        "journalctl", "-k", "-o", "short-iso", "--no-pager"};
    if (!since_.empty()) {
        command.push_back("--since");
        command.push_back(since_);
    }

    const CommandResult result = runner_.Run(command);
    if (!result.ok()) {
        Event error_event;
        error_event.source = Source::kJournalctl;
        error_event.event = "collector_error";
        error_event.message = "`journalctl -k` failed (exit " + std::to_string(result.exit_code) +
                               "); journald may not be running, or user lacks 'systemd-journal' group";
        error_event.raw = result.stdout_text;
        return {error_event};
    }
    return ParseOutput(result.stdout_text);
}

}  // namespace kdiag
