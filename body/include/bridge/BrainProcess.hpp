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

namespace aura::bridge {
    /// Owns the Windows child process and the pipes connecting the body to the brain.
    ///
    /// Observations travel to Python through standard input. Serialized actions return
    /// through standard output, keeping operating-system details inside the bridge.
    class BrainProcess {
    public:
        /// Stores the executable, script, and working-directory paths used at launch.
        BrainProcess(
            std::string pythonExecutable,
            std::string scriptPath,
            std::string workingDirectory
        );

        /// Releases the process and parent-side pipe handles owned by this object.
        ~BrainProcess();

        /// Launches Python and redirects its standard input and output to anonymous pipes.
        [[nodiscard]] bool launch();

        /// Sends one JSON observation and waits for one newline-terminated JSON action.
        [[nodiscard]] std::string exchange(std::string_view observationJson);

    private:
        std::string pythonExecutable_;
        std::string scriptPath_;
        std::string workingDirectory_;

        // These are the parent-side handles retained after the child starts.
        ChildProcessHandle processHandle_ = kInvalidProcessHandle;
        IoHandle stdinWrite_ = kInvalidIoHandle;
        IoHandle stdoutRead_ = kInvalidIoHandle;
    };
}
