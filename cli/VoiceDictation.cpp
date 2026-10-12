#include "VoiceDictation.hpp"
namespace aura::cli {
struct VoiceDictation::Impl {};
VoiceDictation::VoiceDictation() : impl_(std::make_unique<Impl>()) {}
VoiceDictation::~VoiceDictation() = default;
void VoiceDictation::start() {}
void VoiceDictation::poll() {}
void VoiceDictation::finish() {}
void VoiceDictation::stop() {}
VoiceSnapshot VoiceDictation::snapshot() const {
    return {VoicePhase::failed, "", "Native /voice dictation is currently available on macOS only.", 0};
}
}
