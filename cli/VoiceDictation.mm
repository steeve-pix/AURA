#include "VoiceDictation.hpp"
#import <Foundation/Foundation.h>
#include <filesystem>
#include <mach-o/dyld.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;

namespace aura::cli {
struct VoiceDictation::Impl {
    std::string directory;
    VoiceSnapshot value;
    pid_t launcher = 0;
    bool stopped = false;
    void control(NSString* command) {
        if (directory.empty()) return;
        [command writeToFile:[NSString stringWithUTF8String:(directory + "/control").c_str()]
                  atomically:YES encoding:NSUTF8StringEncoding error:nil];
    }
};
VoiceDictation::VoiceDictation() : impl_(std::make_unique<Impl>()) {}
VoiceDictation::~VoiceDictation() { stop(); }
void VoiceDictation::start() {
    @autoreleasepool {
        uint32_t length = 0;
        _NSGetExecutablePath(nullptr, &length);
        std::string executable(length, '\0');
        _NSGetExecutablePath(executable.data(), &length);
        const auto bin = std::filesystem::weakly_canonical(executable.c_str()).parent_path();
        auto helper = bin / "AuraDictation.app";
        if (!std::filesystem::exists(helper)) helper = bin.parent_path() / "lib/aura-cli/AuraDictation.app";
        if (!std::filesystem::exists(helper)) {
            impl_->value = {VoicePhase::failed, "", "Dictation helper is missing. Reinstall using cli/install.sh.", 0};
            return;
        }
        std::string temporary = std::string(NSTemporaryDirectory().UTF8String) + "aura-voice.XXXXXX";
        if (!mkdtemp(temporary.data())) {
            impl_->value = {VoicePhase::failed, "", "Could not create a private dictation session.", 0};
            return;
        }
        impl_->directory = temporary;
        const auto helper_path = helper.string();
        const auto parent = std::to_string(getpid());
        // Launch Services assigns requests to the helper's own privacy identity.
        char* arguments[] = {const_cast<char*>("/usr/bin/open"), const_cast<char*>("-n"), const_cast<char*>("-g"),
                             const_cast<char*>("-W"), const_cast<char*>(helper_path.c_str()),
                             const_cast<char*>("--args"), temporary.data(), const_cast<char*>(parent.c_str()), nullptr};
        if (posix_spawn(&impl_->launcher, "/usr/bin/open", nullptr, nullptr, arguments, environ) != 0) {
            impl_->launcher = 0;
            impl_->value = {VoicePhase::failed, "", "macOS could not launch Aura Dictation.", 0};
        }
    }
}
void VoiceDictation::poll() {
    @autoreleasepool {
        if (impl_->directory.empty()) return;
        const auto path = impl_->directory + "/state.plist";
        NSData* data = [NSData dataWithContentsOfFile:[NSString stringWithUTF8String:path.c_str()]];
        if (data) {
            NSDictionary* state = [NSPropertyListSerialization propertyListWithData:data options:NSPropertyListImmutable format:nil error:nil];
            if ([state isKindOfClass:NSDictionary.class]) {
                impl_->value.phase = static_cast<VoicePhase>([state[@"phase"] intValue]);
                impl_->value.text = [state[@"text"] UTF8String] ?: "";
                impl_->value.error = [state[@"error"] UTF8String] ?: "";
                impl_->value.level = [state[@"level"] floatValue];
            }
        }
        if (impl_->launcher > 0) {
            int status = 0;
            if (waitpid(impl_->launcher, &status, WNOHANG) == impl_->launcher) {
                impl_->launcher = 0;
                if (impl_->value.phase == VoicePhase::permissions || impl_->value.phase == VoicePhase::listening)
                    impl_->value = {VoicePhase::failed, impl_->value.text, "Aura Dictation closed before recognition finished. Check microphone and speech permissions.", 0};
            }
        }
    }
}
void VoiceDictation::finish() { @autoreleasepool { impl_->control(@"finish"); } }
VoiceSnapshot VoiceDictation::snapshot() const { return impl_->value; }
void VoiceDictation::stop() {
    @autoreleasepool {
        if (impl_->stopped) return;
        impl_->stopped = true;
        impl_->control(@"stop");
        for (int i = 0; i < 20 && impl_->launcher > 0; ++i) {
            int status;
            if (waitpid(impl_->launcher, &status, WNOHANG) == impl_->launcher) impl_->launcher = 0;
            else usleep(50000);
        }
        if (!impl_->directory.empty()) {
            std::error_code error;
            std::filesystem::remove_all(impl_->directory, error);
        }
    }
}
}
