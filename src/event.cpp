// Implements event/run string mappings, JSON conversion, and run sorting.
#include "kdiag/event.hpp"

#include <algorithm>
#include <stdexcept>

namespace kdiag {

std::string ToString(Component c) {
    switch (c) {
        case Component::kKernel:    return "kernel";
        case Component::kDma:       return "dma";
        case Component::kIrq:       return "irq";
        case Component::kPci:       return "pci";
        case Component::kV4l2:      return "v4l2";
        case Component::kGstreamer: return "gstreamer";
        case Component::kDriver:    return "driver";
        case Component::kUnknown:   return "unknown";
    }
    return "unknown";
}

Component ComponentFromString(const std::string& s) {
    if (s == "kernel")    return Component::kKernel;
    if (s == "dma")       return Component::kDma;
    if (s == "irq")       return Component::kIrq;
    if (s == "pci")       return Component::kPci;
    if (s == "v4l2")      return Component::kV4l2;
    if (s == "gstreamer") return Component::kGstreamer;
    if (s == "driver")    return Component::kDriver;
    return Component::kUnknown;
}

std::string ToString(Source s) {
    switch (s) {
        case Source::kDmesg:           return "dmesg";
        case Source::kJournalctl:      return "journalctl";
        case Source::kProcInterrupts:  return "proc_interrupts";
        case Source::kSysfsV4l2:       return "sysfs_v4l2";
        case Source::kLspci:           return "lspci";
        case Source::kGstreamerLog:    return "gstreamer_log";
        case Source::kFtrace:          return "ftrace";
    }
    return "dmesg";
}

namespace {

// Parse a source name and reject values outside the event schema vocabulary.
Source SourceFromString(const std::string& s) {
    if (s == "dmesg")           return Source::kDmesg;
    if (s == "journalctl")      return Source::kJournalctl;
    if (s == "proc_interrupts") return Source::kProcInterrupts;
    if (s == "sysfs_v4l2")      return Source::kSysfsV4l2;
    if (s == "lspci")           return Source::kLspci;
    if (s == "gstreamer_log")   return Source::kGstreamerLog;
    if (s == "ftrace")          return Source::kFtrace;
    throw std::invalid_argument("unknown event source: " + s);
}
}  // namespace

nlohmann::json Event::ToJson() const {
    return nlohmann::json{
        {"timestamp_ns", timestamp_ns},
        {"source", ToString(source)},
        {"component", ToString(component)},
        {"event", event},
        {"message", message},
        {"raw", raw},
        {"fields", fields},
    };
}

Event Event::FromJson(const nlohmann::json& j) {
    Event e;
    e.timestamp_ns = j.at("timestamp_ns").get<int64_t>();
    e.source = SourceFromString(j.at("source").get<std::string>());
    e.component = ComponentFromString(j.at("component").get<std::string>());
    e.event = j.at("event").get<std::string>();
    e.message = j.at("message").get<std::string>();
    e.raw = j.value("raw", "");
    e.fields = j.value("fields", nlohmann::json::object());
    return e;
}

void CollectionRun::SortByTime() {
    std::stable_sort(events.begin(), events.end(),
                      [](const Event& a, const Event& b) {
                          return a.timestamp_ns < b.timestamp_ns;
                      });
}

nlohmann::json CollectionRun::ToJson() const {
    nlohmann::json events_json = nlohmann::json::array();
    for (const auto& e : events) events_json.push_back(e.ToJson());

    return nlohmann::json{
        {"pci_bdf", pci_bdf},
        {"driver_name", driver_name},
        {"kernel_release", kernel_release},
        {"collected_at_unix_s", collected_at_unix_s},
        {"event_count", events.size()},
        {"events", events_json},
    };
}

CollectionRun CollectionRun::FromJson(const nlohmann::json& j) {
    CollectionRun run;
    run.pci_bdf = j.value("pci_bdf", "");
    run.driver_name = j.value("driver_name", "");
    run.kernel_release = j.value("kernel_release", "");
    run.collected_at_unix_s = j.value("collected_at_unix_s", int64_t{0});
    for (const auto& ej : j.at("events")) run.events.push_back(Event::FromJson(ej));
    return run;
}

}  // namespace kdiag
