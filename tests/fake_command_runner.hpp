// tests/fake_command_runner.hpp
//
// Test double for kdiag::CommandRunner. Every collector takes a
// CommandRunner& rather than constructing RealCommandRunner itself
// specifically so tests can inject this instead of shelling out to a
// real (and, in CI, likely absent or permission-denied) dmesg/lspci/etc.
// Reusable command-runner fake for collector tests with deterministic output.
#pragma once

#include <string>
#include <map>
#include <vector>

#include "kdiag/exec.hpp"

namespace kdiag::testing {

class FakeCommandRunner : public CommandRunner {
public:
    // Registers the exact stdout text + exit code to return when `Run`
    // is called with `argv`. Commands not registered return exit code 127
    // with a diagnostic message for collector_error-path tests.
    void SetResponse(const std::vector<std::string>& argv, std::string stdout_text,
                     int exit_code = 0) {
        responses_[argv] = {exit_code, std::move(stdout_text)};
    }

    CommandResult Run(const std::vector<std::string>& argv) override {
        const auto it = responses_.find(argv);
        if (it == responses_.end()) {
            return CommandResult{127, "kdiag_test: no fake response registered"};
        }
        return it->second;
    }

private:
    std::map<std::vector<std::string>, CommandResult> responses_;
};

}  // namespace kdiag::testing
