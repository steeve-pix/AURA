#pragma once

#include <string>
#include <string_view>

#include <windows.h>

namespace aura::bridge {
    class BrainProcess {
    public:
        BrainProcess(std::string pythonExecutable, std::string scriptPath,std::string workingDirectory);

        ~BrainProcess();

        [[nodiscard]] bool launch();

        [[nodiscard]]std::string exchange(std::string_view observationJson);

    private:
        std::string pythonExecutable_;
        std::string scriptPath_;
        std::string workingDirectory_;

        HANDLE processHandle_ = nullptr;
        HANDLE stdinWrite_ = nullptr;
        HANDLE stdoutRead_ = nullptr;
    };
}
