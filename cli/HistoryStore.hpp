#pragma once

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fcntl.h>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace aura::cli {
// Length-prefixed records preserve newlines without treating history as shell code.
class HistoryStore {
    static constexpr std::size_t max_bytes = 4 * 1024 * 1024;
    static constexpr std::string_view magic = "AURA_HISTORY_V1\n";
    struct File {
        int fd;
        explicit File(int value) : fd(value) {}
        ~File() { if (fd >= 0) close(fd); }
        File(const File&) = delete;
        File& operator=(const File&) = delete;
    };
    static std::filesystem::path location() {
        if (const auto* state = std::getenv("XDG_STATE_HOME"); state && std::filesystem::path(state).is_absolute())
            return std::filesystem::path(state) / "aura-cli/history";
        if (const auto* home = std::getenv("HOME"); home && *home)
            return std::filesystem::path(home) / ".local/state/aura-cli/history";
        throw std::runtime_error("No home directory for command history.");
    }
    static std::vector<std::string> read(const std::filesystem::path& path) {
        File file(open(path.c_str(), O_RDONLY | O_NOFOLLOW));
        if (file.fd < 0) {
            if (errno == ENOENT) return {};
            throw std::runtime_error("Cannot read saved command history.");
        }
        struct stat info{};
        if (fstat(file.fd, &info) || !S_ISREG(info.st_mode) || info.st_uid != getuid()
            || info.st_size < 0 || info.st_size > static_cast<off_t>(max_bytes))
            throw std::runtime_error("Saved command history has an invalid format or size.");
        std::string data(static_cast<std::size_t>(info.st_size), '\0');
        std::size_t at = 0;
        while (at < data.size()) {
            const auto count = ::read(file.fd, data.data() + at, data.size() - at);
            if (count < 0 && errno == EINTR) continue;
            if (count <= 0) throw std::runtime_error("Cannot finish reading command history.");
            at += static_cast<std::size_t>(count);
        }
        if (!data.starts_with(magic)) throw std::runtime_error("Saved command history has an invalid format.");
        std::vector<std::string> result;
        at = magic.size();
        while (at < data.size()) {
            if (data.size() - at < 4) throw std::runtime_error("Saved command history is incomplete.");
            std::uint32_t length = 0;
            for (int i = 0; i < 4; ++i) length = (length << 8) | static_cast<unsigned char>(data[at++]);
            if (!length || length > 65536 || length > data.size() - at)
                throw std::runtime_error("Saved command history contains an invalid entry.");
            auto entry = data.substr(at, length);
            for (const unsigned char c : entry)
                if ((c < 32 && c != '\n' && c != '\t') || c == 127)
                    throw std::runtime_error("Saved command history contains control characters.");
            result.push_back(std::move(entry));
            at += length;
        }
        return result;
    }
public:
    static bool enabled() {
        const auto* mode = std::getenv("AURA_HISTORY");
        return !mode || std::string_view(mode) != "off";
    }
    static void bound(std::vector<std::string>& entries) {
        std::size_t bytes = magic.size(), first = entries.size();
        while (first && entries.size() - first < 1000) {
            const auto size = entries[first - 1].size() + 4;
            if (size > max_bytes - bytes) break;
            bytes += size; --first;
        }
        entries.erase(entries.begin(), entries.begin() + static_cast<std::ptrdiff_t>(first));
    }
    static std::vector<std::string> load() {
        if (!enabled()) return {};
        auto entries = read(location());
        bound(entries);
        return entries;
    }
    static void clear() {
        const auto path = location();
        struct stat info{};
        if (lstat(path.parent_path().c_str(), &info) != 0) {
            if (errno == ENOENT) return;
            throw std::runtime_error("Cannot access the saved history folder.");
        }
        File lock(open((path.string() + ".lock").c_str(), O_CREAT | O_RDWR | O_NOFOLLOW, 0600));
        if (lock.fd < 0) throw std::runtime_error("Cannot lock saved history for clearing.");
        while (flock(lock.fd, LOCK_EX)) {
            if (errno != EINTR) throw std::runtime_error("Cannot lock saved history for clearing.");
        }
        // Share the append lock so another session cannot race the removal.
        if (unlink(path.c_str()) != 0 && errno != ENOENT)
            throw std::runtime_error("Cannot remove saved command history. Check the history folder's permissions.");
    }
    static std::vector<std::string> append(const std::string& command) {
        if (command.empty() || command.size() > 65536) throw std::runtime_error("Command is too long to save in history.");
        for (const unsigned char c : command)
            if ((c < 32 && c != '\n' && c != '\t') || c == 127)
                throw std::runtime_error("Command contains control characters and was not saved in history.");
        const auto path = location(), directory = path.parent_path();
        std::error_code error;
        std::filesystem::create_directories(directory, error);
        if (error || chmod(directory.c_str(), 0700)) throw std::runtime_error("Cannot create private command history folder.");
        File lock(open((path.string() + ".lock").c_str(), O_CREAT | O_RDWR | O_NOFOLLOW, 0600));
        if (lock.fd < 0) throw std::runtime_error("Cannot lock command history.");
        while (flock(lock.fd, LOCK_EX)) {
            if (errno != EINTR) throw std::runtime_error("Cannot lock command history.");
        }
        auto entries = read(path);
        if (entries.empty() || entries.back() != command) entries.push_back(command);
        bound(entries);
        std::string data(magic);
        for (const auto& entry : entries) {
            const auto length = static_cast<std::uint32_t>(entry.size());
            for (int shift = 24; shift >= 0; shift -= 8) data += static_cast<char>((length >> shift) & 255);
            data += entry;
        }
        auto temporary = path.string() + ".XXXXXX";
        File output(mkstemp(temporary.data()));
        if (output.fd < 0) throw std::runtime_error("Cannot create command history file.");
        try {
            std::size_t at = 0;
            while (at < data.size()) {
                const auto count = write(output.fd, data.data() + at, data.size() - at);
                if (count < 0 && errno == EINTR) continue;
                if (count <= 0) throw std::runtime_error("Cannot save command history.");
                at += static_cast<std::size_t>(count);
            }
            if (fsync(output.fd) || rename(temporary.c_str(), path.c_str()))
                throw std::runtime_error("Cannot finish saving command history.");
        } catch (...) { unlink(temporary.c_str()); throw; }
        return entries;
    }
};
}
