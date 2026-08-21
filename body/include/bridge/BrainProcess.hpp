#pragma once

#include <string>
#include <string_view>

#if defined(_WIN32)
#include <windows.h>
using ChildProcessHandle = HANDLE;
using IoHandle = HANDLE;
static constexpr IoHandle kInvalidIoHandle = nullptr;
static const ChildProcessHandle kInvalidProcessHandle = nullptr;
#else
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
using ChildProcessHandle = pid_t;
using IoHandle = int;
static constexpr IoHandle kInvalidIoHandle = -1;
static constexpr ChildProcessHandle kInvalidProcessHandle = -1;
#endif

namespace aura::bridge
{
    /// Owns the Python brain process and its standard-input/output pipes.
    ///
    /// The class presents the same request-response interface on Windows and POSIX.
    /// Each observation and response is a single newline-terminated JSON message.
    class BrainProcess
    {
    public:
        /// Stores the process configuration without starting the child process.
        BrainProcess(
            std::string pythonExecutable,
            std::string scriptPath,
            std::string workingDirectory
        );

        /// Closes all parent pipe handles and terminates the child when still running.
        ~BrainProcess();

        /// Starts the configured child process and connects its input and output pipes.
        ///
        /// Returns false when pipe creation or process creation fails.
        [[nodiscard]] bool launch();

        /// Sends one observation and blocks until one complete response is received.
        ///
        /// Returns an empty string when communication is unavailable or interrupted.
        [[nodiscard]] std::string exchange(std::string_view observationJson);

    private:
        std::string pythonExecutable_;
        std::string scriptPath_;
        std::string workingDirectory_;

        /// Child identifier and parent-owned pipe endpoints retained after launch.
        ChildProcessHandle processHandle_ = kInvalidProcessHandle;
        IoHandle stdinWrite_ = kInvalidIoHandle;
        IoHandle stdoutRead_ = kInvalidIoHandle;
    };
}
