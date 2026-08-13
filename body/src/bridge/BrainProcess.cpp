#include "bridge/BrainProcess.hpp"

#include <string>
#include <utility>
#include <vector>
#include <unistd.h>

#if defined(_WIN32)
#include <windows.h>
#else
#include <csignal>
#include <sys/wait.h>
#include <errno.h>
#endif

namespace aura::bridge {
    BrainProcess::BrainProcess(std::string pythonExecutable, std::string scriptPath, std::string workingDirectory)
        : pythonExecutable_(std::move(pythonExecutable)),
          scriptPath_(std::move(scriptPath)),
          workingDirectory_(std::move(workingDirectory)) {
    }

    BrainProcess::~BrainProcess() {
#if defined(_WIN32)
        // The parent owns only these handles; child-side pipe handles were closed at launch.
        if (stdinWrite_) {
            CloseHandle(stdinWrite_);
            stdinWrite_ = kInvalidIoHandle;
        }

        if (stdoutRead_) {
            CloseHandle(stdoutRead_);
            stdoutRead_ = kInvalidIoHandle;
        }

        if (processHandle_) {
            CloseHandle(processHandle_);
            processHandle_ = kInvalidProcessHandle;
        }
#else
        // Closing stdin first lets a cooperative brain observe EOF before termination.
        if (stdinWrite_ != kInvalidIoHandle) {
            close(stdinWrite_);
            stdinWrite_ = kInvalidIoHandle;
        }

        if (stdoutRead_ != kInvalidIoHandle) {
            close(stdoutRead_);
            stdoutRead_ = kInvalidIoHandle;
        }

        if (processHandle_ != kInvalidProcessHandle) {
            kill(processHandle_, SIGTERM);
            waitpid(processHandle_, nullptr, 0);
            processHandle_ = kInvalidProcessHandle;
        }
#endif
    }

    bool BrainProcess::launch() {
#if defined(_WIN32)
        // Child endpoints remain inheritable while parent endpoints are explicitly private.
        SECURITY_ATTRIBUTES security{};
        security.nLength = sizeof(SECURITY_ATTRIBUTES);
        security.bInheritHandle = TRUE;
        security.lpSecurityDescriptor = nullptr;

        HANDLE childStdinRead = nullptr;
        HANDLE childStdoutWrite = nullptr;

        if (!CreatePipe(&childStdinRead, &stdinWrite_, &security, 0)) {
            return false;
        }

        if (!CreatePipe(&stdoutRead_, &childStdoutWrite, &security, 0)) {
            CloseHandle(childStdinRead);
            CloseHandle(stdinWrite_);
            stdinWrite_ = kInvalidIoHandle;
            return false;
        }

        SetHandleInformation(stdinWrite_, HANDLE_FLAG_INHERIT, 0);
        SetHandleInformation(stdoutRead_, HANDLE_FLAG_INHERIT, 0);

        STARTUPINFOA startupInfo{};
        PROCESS_INFORMATION processInfo{};

        startupInfo.cb = sizeof(startupInfo);
        startupInfo.dwFlags |= STARTF_USESTDHANDLES;
        startupInfo.hStdInput = childStdinRead;
        startupInfo.hStdOutput = childStdoutWrite;
        startupInfo.hStdError = GetStdHandle(STD_ERROR_HANDLE);

        std::string command = pythonExecutable_ + " " + scriptPath_;
        // CreateProcess may modify its command-line buffer, so it cannot receive c_str().
        std::vector<char> commandBuffer(command.begin(), command.end());
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

        CloseHandle(childStdinRead);
        CloseHandle(childStdoutWrite);

        if (!success) {
            CloseHandle(stdinWrite_);
            CloseHandle(stdoutRead_);
            stdinWrite_ = kInvalidIoHandle;
            stdoutRead_ = kInvalidIoHandle;
            return false;
        }

        CloseHandle(processInfo.hThread);
        processHandle_ = processInfo.hProcess;
        return true;
#else
        // Two unidirectional pipes form the request and response channels.
        int stdinPipe[2] = {-1, -1};
        int stdoutPipe[2] = {-1, -1};

        if (pipe(stdinPipe) == -1 || pipe(stdoutPipe) == -1) {
            if (stdinPipe[0] != -1) close(stdinPipe[0]);
            if (stdinPipe[1] != -1) close(stdinPipe[1]);
            if (stdoutPipe[0] != -1) close(stdoutPipe[0]);
            if (stdoutPipe[1] != -1) close(stdoutPipe[1]);
            return false;
        }

        pid_t pid = fork();
        if (pid == -1) {
            close(stdinPipe[0]);
            close(stdinPipe[1]);
            close(stdoutPipe[0]);
            close(stdoutPipe[1]);
            return false;
        }

        if (pid == 0) {
            // The child replaces its standard streams before replacing the process image.
            dup2(stdinPipe[0], STDIN_FILENO);
            dup2(stdoutPipe[1], STDOUT_FILENO);

            close(stdinPipe[1]);
            close(stdoutPipe[0]);

            if (!workingDirectory_.empty()) {
                if (chdir(workingDirectory_.c_str()) != 0) {
                    _exit(1);
                }
            }

            execlp(
                pythonExecutable_.c_str(),
                pythonExecutable_.c_str(),
                "-u",
                scriptPath_.c_str(),
                static_cast<char *>(nullptr)
            );
            _exit(1);
        }

        close(stdinPipe[0]);
        close(stdoutPipe[1]);

        // The parent retains only the endpoints used to write requests and read replies.
        processHandle_ = pid;
        stdinWrite_ = stdinPipe[1];
        stdoutRead_ = stdoutPipe[0];
        return true;
#endif
    }

    std::string BrainProcess::exchange(std::string_view observationJson) {
#if defined(_WIN32)
        if (!stdinWrite_ || !stdoutRead_) {
            return {};
        }

        std::string message{observationJson};
        // Newlines frame messages because each JSON document itself occupies one line.
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
        // Reading through the delimiter avoids returning a partial JSON document.
        while (true) {
            char character = '\0';
            DWORD bytesRead = 0;
            const BOOL readSuccess = ReadFile(stdoutRead_, &character, 1, &bytesRead, nullptr);
            if (!readSuccess || bytesRead == 0) {
                return {};
            }

            if (character == '\n') {
                break;
            }

            response.push_back(character);
        }

        return response;
#else
        if (stdinWrite_ == kInvalidIoHandle || stdoutRead_ == kInvalidIoHandle) {
            return {};
        }

        std::string message{observationJson};
        // Newlines frame messages because each JSON document itself occupies one line.
        message.push_back('\n');

        const ssize_t written = write(stdinWrite_, message.data(), message.size());
        if (written != static_cast<ssize_t>(message.size())) {
            return {};
        }

        std::string response;
        // Reading through the delimiter avoids returning a partial JSON document.
        while (true) {
            char character = '\0';
            const ssize_t readCount = read(stdoutRead_, &character, 1);
            if (readCount != 1) {
                return {};
            }

            if (character == '\n') {
                break;
            }

            response.push_back(character);
        }

        return response;
#endif
    }
}
