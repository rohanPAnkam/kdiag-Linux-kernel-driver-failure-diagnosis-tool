// kdiag/exec.hpp
//
// Every collector needs to run a system command and capture its output.
// Centralizing argument-based execution here means:
//   1. One place to get quoting/error-handling right.
//   2. Collectors depend on a `CommandRunner` interface, not directly on
//      process APIs, so unit tests can inject a fake runner with canned output
//      instead of requiring root/hardware/systemd in the test environment.
#pragma once

#include <functional>
#include <string>
#include <vector>

namespace kdiag {

struct CommandResult {
    int exit_code = -1;
    std::string stdout_text;

    /// Return whether the command exited successfully.
    bool ok() const { return exit_code == 0; }
};

// Abstract so collectors can be unit-tested without touching the real
// system. `RealCommandRunner` is the production implementation; tests use
// an argv-keyed fake (see tests/fake_command_runner.hpp).
class CommandRunner {
public:
    virtual ~CommandRunner() = default;

    /// Execute argv directly and capture merged standard output and error.
    /// The first argument is the executable; no shell expansion is performed.
    virtual CommandResult Run(const std::vector<std::string>& argv) = 0;
};

// Runs an executable with `execvp`; stderr is merged into stdout because
// several system tools place diagnostic-relevant text on stderr.
class RealCommandRunner : public CommandRunner {
public:
    /// Run the command through the system shell and return its output/status.
    CommandResult Run(const std::vector<std::string>& argv) override;
};

}  // namespace kdiag
