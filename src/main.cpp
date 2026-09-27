// Command-line dispatcher and option parsing for all kdiag subcommands.
//
// Usage:
//   kdiag collect [--device 0000:03:00.0] [--driver driver_name]
//                 [--output ./capture] [--since "-10 min"] [--quiet]
//   kdiag --help

#include <iostream>
#include <string>
#include <vector>

#include "kdiag/collect_command.hpp"
#include "kdiag/correlate_command.hpp"
#include "kdiag/diagnose_command.hpp"
#include "kdiag/explain_command.hpp"
#include "kdiag/evaluate_command.hpp"

namespace {

// Print supported commands and their accepted options.
void PrintUsage() {
    std::cout <<
        "kdiag - Linux kernel/driver failure diagnosis tool\n\n"
        "USAGE:\n"
        "  kdiag collect [OPTIONS]\n"
        "  kdiag correlate [OPTIONS]\n"
        "  kdiag diagnose [OPTIONS]\n\n"
        "  kdiag explain [OPTIONS]\n\n"
        "  kdiag evaluate\n\n"
        "OPTIONS (collect):\n"
        "  --device <BDF>     PCI Bus:Device.Function, e.g. 0000:03:00.0\n"
        "  --driver <name>    Kernel driver name, e.g. driver_name\n"
        "  --output <dir>     Output directory (default: ./capture)\n"
        "  --since <spec>     journalctl --since value, e.g. \"-10 min\"\n"
        "  --quiet            Suppress progress output\n\n"
        "OPTIONS (correlate):\n"
        "  --input <file>     Path to events.json from a prior collect (required)\n"
        "  --show-complete    Also print chains that reached every stage\n\n"
        "OPTIONS (diagnose):\n"
        "  --input <file>     Path to events.json from a prior collect (required)\n\n"
        "  --verbose          Also show low-confidence hypotheses\n\n"
        "OPTIONS (explain):\n"
        "  --input <file>     Path to events.json from a prior collect (required)\n"
        "  --show-diagram     Print each correlated pipeline timeline\n"
        "  --verbose          Include low-confidence hypotheses\n"
        "  --deep-dive        Request more technical explanation\n"
        "  Set ANTHROPIC_API_KEY to enable the remote explanation; without it,\n"
        "  kdiag prints a deterministic local summary.\n\n"
        "  -h, --help         Show this help\n";
}

// Minimal, dependency-free flag parser: no external CLI library pulled
// in for a handful of flags. If this grows past `collect`, revisit.
// Consume the next argument or exit with usage error when it is absent.
std::string TakeValue(const std::vector<std::string>& args, size_t& i) {
    if (i + 1 >= args.size()) {
        std::cerr << "[kdiag] error: " << args[i] << " requires a value\n";
        std::exit(2);
    }
    return args[++i];
}

// Parse collect-specific options and invoke the collection command.
int RunCollect(const std::vector<std::string>& args) {
    kdiag::CollectOptions options;
    for (size_t i = 0; i < args.size(); ++i) {
        const std::string& a = args[i];
        if (a == "--device") options.pci_bdf = TakeValue(args, i);
        else if (a == "--driver") options.driver_name = TakeValue(args, i);
        else if (a == "--output") options.output_dir = TakeValue(args, i);
        else if (a == "--since") options.journalctl_since = TakeValue(args, i);
        else if (a == "--quiet") options.quiet = true;
        else if (a == "-h" || a == "--help") { PrintUsage(); return 0; }
        else {
            std::cerr << "[kdiag] error: unrecognized option '" << a << "'\n";
            return 2;
        }
    }
    return kdiag::RunCollectCommand(options);
}

// Parse correlate-specific options and invoke the correlation command.
int RunCorrelate(const std::vector<std::string>& args) {
    kdiag::CorrelateOptions options;
    for (size_t i = 0; i < args.size(); ++i) {
        const std::string& a = args[i];
        if (a == "--input") options.input_path = TakeValue(args, i);
        else if (a == "--show-complete") options.show_complete_chains = true;
        else if (a == "-h" || a == "--help") { PrintUsage(); return 0; }
        else {
            std::cerr << "[kdiag] error: unrecognized option '" << a << "'\n";
            return 2;
        }
    }
    if (options.input_path.empty()) {
        std::cerr << "[kdiag] error: correlate requires --input <events.json>\n";
        return 2;
    }
    return kdiag::RunCorrelateCommand(options);
}

// Parse diagnose-specific options and invoke deterministic rule evaluation.
int RunDiagnose(const std::vector<std::string>& args) {
    kdiag::DiagnoseOptions options;
    for (size_t i = 0; i < args.size(); ++i) {
        const std::string& a = args[i];
        if (a == "--input") options.input_path = TakeValue(args, i);
        else if (a == "--verbose") options.verbose = true;
        else if (a == "-h" || a == "--help") { PrintUsage(); return 0; }
        else {
            std::cerr << "[kdiag] error: unrecognized option '" << a << "'\n";
            return 2;
        }
    }
    if (options.input_path.empty()) {
        std::cerr << "[kdiag] error: diagnose requires --input <events.json>\n";
        return 2;
    }
    return kdiag::RunDiagnoseCommand(options);
}

// Parse explain options, including diagram, verbosity, and detail controls.
int RunExplain(const std::vector<std::string>& args) {
    kdiag::ExplainOptions options;
    for (size_t i = 0; i < args.size(); ++i) {
        const std::string& a = args[i];
        if (a == "--input") options.input_path = TakeValue(args, i);
        else if (a == "--show-diagram") options.show_diagram = true;
        else if (a == "--verbose") options.verbose = true;
        else if (a == "--deep-dive") options.deep_dive = true;
        else if (a == "-h" || a == "--help") { PrintUsage(); return 0; }
        else {
            std::cerr << "[kdiag] error: unrecognized option '" << a << "'\n";
            return 2;
        }
    }
    if (options.input_path.empty()) {
        std::cerr << "[kdiag] error: explain requires --input <events.json>\n";
        return 2;
    }
    return kdiag::RunExplainCommand(options);
}

}  // namespace

int main(int argc, char** argv) {
    // Dispatch the first positional argument to its subcommand handler.
    std::vector<std::string> args(argv + 1, argv + argc);

    if (args.empty() || args[0] == "-h" || args[0] == "--help") {
        PrintUsage();
        return args.empty() ? 2 : 0;
    }

    const std::string subcommand = args[0];
    const std::vector<std::string> rest(args.begin() + 1, args.end());

    if (subcommand == "collect") {
        return RunCollect(rest);
    }
    if (subcommand == "correlate") {
        return RunCorrelate(rest);
    }
    if (subcommand == "diagnose") {
        return RunDiagnose(rest);
    }
    if (subcommand == "explain") {
        return RunExplain(rest);
    }
    if (subcommand == "evaluate") {
        if (rest.empty()) return kdiag::RunEvaluateCommand();
        if (rest.size() == 1 && (rest[0] == "-h" || rest[0] == "--help")) {
            PrintUsage();
            return 0;
        }
        std::cerr << "[kdiag] error: evaluate does not accept options\n";
        return 2;
    }

    std::cerr << "[kdiag] error: unknown subcommand '" << subcommand << "'\n"
              << "        (implemented: collect, correlate, diagnose, explain, evaluate)\n\n";
    PrintUsage();
    return 2;
}
