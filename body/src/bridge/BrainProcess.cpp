#include "bridge/BrainProcess.hpp"

#include <string>
#include <utility>
#include <vector>

#include <windows.h>

namespace aura::bridge {
    BrainProcess::BrainProcess(std::string pythonExecutable, std::string scriptPath, std::string workingDirectory)
        : pythonExecutable_(std::move(pythonExecutable)),
          scriptPath_(std::move(scriptPath)),
          workingDirectory_(std::move(workingDirectory)) {
    }

    BrainProcess::~BrainProcess() {
        // Each non-null handle belongs to this object after a successful launch.
        if (stdinWrite_) {
            CloseHandle(stdinWrite_);
        }

        if (stdoutRead_) {
            CloseHandle(stdoutRead_);
        }

        if (processHandle_) {
            CloseHandle(processHandle_);
        }
    }

    bool BrainProcess::launch() {
        SECURITY_ATTRIBUTES security{};
        security.nLength = sizeof(SECURITY_ATTRIBUTES);
        // The child must inherit the pipe ends assigned to its stdin and stdout.
        security.bInheritHandle = TRUE;
        security.lpSecurityDescriptor = nullptr;

        HANDLE childStdinRead = nullptr;
        HANDLE childStdoutWrite = nullptr;

        // Python's input() reads observations through childStdinRead.
        if (!CreatePipe(&childStdinRead, &stdinWrite_, &security, 0)) {
            return false;
        }

        // Python's print() writes actions through childStdoutWrite.
        if (!CreatePipe(&stdoutRead_, &childStdoutWrite, &security, 0)) {
            CloseHandle(childStdinRead);
            CloseHandle(stdinWrite_);
            stdinWrite_ = nullptr;
            return false;
        }

        // Python should inherit only its ends. Inheriting these parent ends could keep a
        // pipe open unexpectedly and prevent reads from observing end-of-file.
        SetHandleInformation(stdinWrite_, HANDLE_FLAG_INHERIT, 0);
        SetHandleInformation(stdoutRead_, HANDLE_FLAG_INHERIT, 0);

        STARTUPINFOA startupInfo{};
        PROCESS_INFORMATION processInfo{};

        startupInfo.cb = sizeof(startupInfo);
        startupInfo.dwFlags |= STARTF_USESTDHANDLES;
        startupInfo.hStdInput = childStdinRead;
        startupInfo.hStdOutput = childStdoutWrite;
        startupInfo.hStdError = GetStdHandle(STD_ERROR_HANDLE);

        std::string command =
                pythonExecutable_ + " " + scriptPath_;

        // CreateProcessA may modify its command-line buffer, so it needs writable storage
        // rather than the read-only pointer returned by std::string::c_str().
        std::vector<char> commandBuffer(
            command.begin(),
            command.end()
        );

        commandBuffer.push_back('\0');

        const BOOL success = CreateProcessA(
            nullptr,
            commandBuffer.data(),
            nullptr,
            nullptr,
            TRUE,
            0,
            nullptr,
            workingDirectory_.c_str(),
            &startupInfo,
            &processInfo
        );

        // The parent keeps stdinWrite_ and stdoutRead_, not these child-facing copies.
        CloseHandle(childStdinRead);
        CloseHandle(childStdoutWrite);

        if (!success) {
            CloseHandle(stdinWrite_);
            CloseHandle(stdoutRead_);
            stdinWrite_ = nullptr;
            stdoutRead_ = nullptr;
            return false;
        }

        CloseHandle(processInfo.hThread);
        processHandle_ = processInfo.hProcess;

        return true;
    }

    std::string BrainProcess::exchange(std::string_view observationJson) {
        if (!stdinWrite_ || !stdoutRead_) {
            return {};
        }

        std::string message{observationJson};
        // One line is one protocol message; Python's input() removes this delimiter.
        message.push_back('\n');

        DWORD bytesWritten = 0;

        const BOOL writeSuccess = WriteFile(
            stdinWrite_,
            message.data(),
            static_cast<DWORD>(message.size()),
            &bytesWritten,
            nullptr
        );

        if (!writeSuccess) {
            return {};
        }

        std::string response;

        // Read one byte at a time until Python's print() supplies the line delimiter.
        while (true) {
            char character = '\0';
            DWORD bytesRead = 0;

            const BOOL readSuccess = ReadFile(
                stdoutRead_,
                &character,
                1,
                &bytesRead,
                nullptr
            );

            if (!readSuccess || bytesRead == 0) {
                return {};
            }

            if (character == '\n') {
                break;
            }

            response.push_back(character);
        }

        return response;
    }
}
