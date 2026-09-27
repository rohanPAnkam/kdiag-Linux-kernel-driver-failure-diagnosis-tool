// Executes system commands and captures their combined output and exit status.
#include "kdiag/exec.hpp"

#include <array>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

namespace kdiag {

CommandResult RealCommandRunner::Run(const std::vector<std::string>& argv) {
    CommandResult result;
    if (argv.empty() || argv.front().empty()) {
        result.stdout_text = "cannot run an empty command";
        return result;
    }

    int pipe_fds[2];
    if (pipe(pipe_fds) != 0) {
        result.stdout_text = "pipe() failed: " + std::string(std::strerror(errno));
        return result;
    }

    std::vector<char*> child_argv;
    child_argv.reserve(argv.size() + 1);
    for (const auto& argument : argv) {
        child_argv.push_back(const_cast<char*>(argument.c_str()));
    }
    child_argv.push_back(nullptr);

    const pid_t child = fork();
    if (child < 0) {
        const int error = errno;
        close(pipe_fds[0]);
        close(pipe_fds[1]);
        result.stdout_text = "fork() failed: " + std::string(std::strerror(error));
        return result;
    }

    if (child == 0) {
        close(pipe_fds[0]);
        if (dup2(pipe_fds[1], STDOUT_FILENO) < 0 ||
            dup2(pipe_fds[1], STDERR_FILENO) < 0) {
            _exit(126);
        }
        close(pipe_fds[1]);
        execvp(child_argv[0], child_argv.data());
        _exit(errno == ENOENT ? 127 : 126);
    }

    close(pipe_fds[1]);
    std::array<char, 4096> buffer{};
    while (true) {
        const ssize_t bytes = read(pipe_fds[0], buffer.data(), buffer.size());
        if (bytes > 0) {
            result.stdout_text.append(buffer.data(), static_cast<size_t>(bytes));
        } else if (bytes == 0) {
            break;
        } else if (errno != EINTR) {
            result.stdout_text += "read() failed: ";
            result.stdout_text += std::strerror(errno);
            break;
        }
    }
    close(pipe_fds[0]);

    int status = 0;
    pid_t waited;
    do {
        waited = waitpid(child, &status, 0);
    } while (waited < 0 && errno == EINTR);
    if (waited < 0) {
        result.stdout_text += "waitpid() failed: ";
        result.stdout_text += std::strerror(errno);
        return result;
    }

    if (WIFEXITED(status)) result.exit_code = WEXITSTATUS(status);
    else if (WIFSIGNALED(status)) result.exit_code = 128 + WTERMSIG(status);
    return result;
}

}  // namespace kdiag
