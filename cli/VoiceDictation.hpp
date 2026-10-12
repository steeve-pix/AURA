#pragma once
#include <memory>
#include <string>

namespace aura::cli {
enum class VoicePhase { permissions, listening, finished, failed };
struct VoiceSnapshot {
    VoicePhase phase = VoicePhase::permissions;
    std::string text, error;
    float level = 0;
};
// Native audio and recognition live behind this small platform boundary.
class VoiceDictation {
    struct Impl;
    std::unique_ptr<Impl> impl_;
public:
    VoiceDictation();
    ~VoiceDictation();
    VoiceDictation(const VoiceDictation&) = delete;
    VoiceDictation& operator=(const VoiceDictation&) = delete;
    void start();
    void poll();
    void finish();
    void stop();
    VoiceSnapshot snapshot() const;
};
}
