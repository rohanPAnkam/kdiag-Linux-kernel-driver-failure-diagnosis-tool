// Runs collectors, combines their events, and writes the collection JSON file.
#include "kdiag/collect_command.hpp"

#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <vector>

#include "kdiag/collector.hpp"
#include "kdiag/collectors/dmesg_collector.hpp"
#include "kdiag/collectors/interrupts_collector.hpp"
#include "kdiag/collectors/journalctl_collector.hpp"
#include "kdiag/collectors/pci_collector.hpp"
#include "kdiag/collectors/v4l2_sysfs_collector.hpp"
#include "kdiag/event.hpp"
#include "kdiag/exec.hpp"

namespace kdiag {

namespace {

// Read the running kernel release, returning empty text when uname fails.
std::string ReadUnameRelease(CommandRunner& runner) {
    const CommandResult r = runner.Run({"uname", "-r"});
    if (!r.ok()) return "";
    std::string s = r.stdout_text;
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
    return s;
}

}  // namespace

int RunCollectCommand(const CollectOptions& options) {
    RealCommandRunner runner;

    CollectionRun run;
    run.pci_bdf = options.pci_bdf;
    run.driver_name = options.driver_name;
    run.kernel_release = ReadUnameRelease(runner);
    run.collected_at_unix_s = static_cast<int64_t>(std::time(nullptr));
    const std::string classifier_driver_name = options.driver_name.empty()
        ? "driver_name" : options.driver_name;

    std::vector<std::unique_ptr<Collector>> collectors;
    collectors.push_back(std::make_unique<DmesgCollector>(runner, classifier_driver_name));
    collectors.push_back(std::make_unique<InterruptsCollector>(runner));
    collectors.push_back(std::make_unique<V4l2SysfsCollector>(runner));
    collectors.push_back(std::make_unique<PciCollector>(runner, options.pci_bdf));
    collectors.push_back(std::make_unique<JournalctlCollector>(
        runner, options.journalctl_since, classifier_driver_name));

    for (auto& collector : collectors) {
        if (!options.quiet) {
            std::cout << "[kdiag] collecting: " << collector->Name() << "... " << std::flush;
        }
        const std::vector<Event> events = collector->Collect();
        run.events.insert(run.events.end(), events.begin(), events.end());
        if (!options.quiet) {
            std::cout << events.size() << " events\n";
        }
    }

    run.SortByTime();

    if (options.output_dir.empty()) {
        std::cerr << "[kdiag] failed to create output directory: path is empty\n";
        return 1;
    }
    std::error_code directory_error;
    std::filesystem::create_directories(options.output_dir, directory_error);
    if (directory_error) {
        std::cerr << "[kdiag] failed to create output directory " << options.output_dir
                  << ": " << directory_error.message() << "\n";
        return 1;
    }

    const std::filesystem::path out_path =
        std::filesystem::path(options.output_dir) / "events.json";
    std::ofstream out(out_path);
    if (!out) {
        std::cerr << "[kdiag] failed to open " << out_path << " for writing\n";
        return 1;
    }
    out << run.ToJson().dump(2) << "\n";
    out.close();
    if (!out) {
        std::cerr << "[kdiag] failed to write collection data to "
                  << out_path.string() << "\n";
        return 1;
    }

    if (!options.quiet) {
        std::cout << "[kdiag] wrote " << run.events.size() << " total events to "
                  << out_path.string() << "\n";
    }
    return 0;
}

}  // namespace kdiag
