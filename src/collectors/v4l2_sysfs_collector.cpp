// Enumerates current V4L2 sysfs nodes and records node metadata as snapshots.
#include "kdiag/collectors/v4l2_sysfs_collector.hpp"

#include <sstream>

namespace kdiag {

namespace {

// Returns true and sets *value if `line` starts with "<key>:"; leaves
// *value untouched otherwise so callers can chain checks cleanly.
bool TryExtract(const std::string& line, const std::string& key, std::string* value) {
    const std::string prefix = key + ":";
    if (line.compare(0, prefix.size(), prefix) != 0) return false;
    *value = line.substr(prefix.size());
    return true;
}

}  // namespace

std::vector<Event> V4l2SysfsCollector::ParseOutput(const std::string& listing) const {
    std::vector<Event> events;
    std::istringstream stream(listing);
    std::string line;

    std::string node, name, index, device;
    auto flush_node = [&]() {
        if (node.empty()) return;
        Event e;
        e.timestamp_ns = kSnapshotTimestampNs;
        e.source = Source::kSysfsV4l2;
        e.component = Component::kV4l2;
        e.event = "v4l2_node_present";
        e.message = node + " name=\"" + name + "\" index=" + index + " device=" + device;
        e.raw = "NODE:" + node + " NAME:" + name + " INDEX:" + index + " DEVICE:" + device;
        e.fields = {
            {"snapshot", true},
            {"node", node},
            {"name", name},
            {"index", index},
            {"device_path", device},
        };
        events.push_back(std::move(e));
        node.clear(); name.clear(); index.clear(); device.clear();
    };

    while (std::getline(stream, line)) {
        if (line == "---") { flush_node(); continue; }
        if (TryExtract(line, "NODE", &node)) continue;
        if (TryExtract(line, "NAME", &name)) continue;
        if (TryExtract(line, "INDEX", &index)) continue;
        if (TryExtract(line, "DEVICE", &device)) continue;
    }
    flush_node();  // in case output didn't end with a trailing "---"

    if (events.empty()) {
        Event e;
        e.timestamp_ns = kSnapshotTimestampNs;
        e.source = Source::kSysfsV4l2;
        e.component = Component::kV4l2;
        e.event = "v4l2_no_nodes_found";
        e.message = "no /sys/class/video4linux/videoN nodes exist right now";
        e.fields = {{"snapshot", true}};
        events.push_back(std::move(e));
    }
    return events;
}

std::vector<Event> V4l2SysfsCollector::Collect() {
    static const char* kScript =
        "for d in /sys/class/video4linux/*; do "
        "  [ -d \"$d\" ] || continue; "
        "  echo \"NODE:$d\"; "
        "  echo \"NAME:$(cat \"$d/name\" 2>/dev/null)\"; "
        "  echo \"INDEX:$(cat \"$d/index\" 2>/dev/null)\"; "
        "  echo \"DEVICE:$(readlink -f \"$d/device\" 2>/dev/null)\"; "
        "  echo '---'; "
        "done";
    const CommandResult result = runner_.Run({"sh", "-c", kScript});
    // Note: an empty /sys/class/video4linux/* glob is not a shell error
    // (the loop body just never runs), so we don't treat result.ok()==false
    // as fatal here the way other collectors do — empty output legitimately
    // means "no V4L2 devices right now", which ParseOutput reports as its
    // own event rather than a collector_error.
    return ParseOutput(result.stdout_text);
}

}  // namespace kdiag
