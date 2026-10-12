#include "VoiceDictation.hpp"
#import <AppKit/AppKit.h>
#include <csignal>
#include <chrono>
#include <cstdlib>
#include <memory>
#include <sys/stat.h>
#include <unistd.h>

int main(int argc, char** argv) {
    @autoreleasepool {
        if (argc < 3) return 2;
        struct stat info{};
        if (lstat(argv[1], &info) != 0 || !S_ISDIR(info.st_mode) || info.st_uid != getuid() || (info.st_mode & 0077)) return 2;
        NSString* directory = [NSString stringWithUTF8String:argv[1]];
        const pid_t parent = static_cast<pid_t>(std::strtol(argv[2], nullptr, 10));
        if (parent <= 1) return 2;
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
        const auto voice = std::make_shared<aura::cli::VoiceDictation>();
        const auto started = std::chrono::steady_clock::now();
        [NSTimer scheduledTimerWithTimeInterval:0.05 repeats:YES block:^(NSTimer*) {
            NSString* control = [NSString stringWithContentsOfFile:[directory stringByAppendingPathComponent:@"control"] encoding:NSUTF8StringEncoding error:nil];
            if ([control isEqualToString:@"stop"] || kill(parent, 0) != 0
                || ![NSFileManager.defaultManager fileExistsAtPath:directory]
                || std::chrono::steady_clock::now() - started > std::chrono::seconds(120)) {
                voice->stop();
                [NSApp terminate:nil];
                return;
            }
            if ([control isEqualToString:@"finish"]) {
                [NSFileManager.defaultManager removeItemAtPath:[directory stringByAppendingPathComponent:@"control"] error:nil];
                voice->finish();
            }
            const auto value = voice->snapshot();
            NSDictionary* state = @{@"phase": @(static_cast<int>(value.phase)),
                                    @"text": [NSString stringWithUTF8String:value.text.c_str()] ?: @"",
                                    @"error": [NSString stringWithUTF8String:value.error.c_str()] ?: @"",
                                    @"level": @(value.level)};
            NSData* data = [NSPropertyListSerialization dataWithPropertyList:state format:NSPropertyListBinaryFormat_v1_0 options:0 error:nil];
            [data writeToFile:[directory stringByAppendingPathComponent:@"state.plist"] atomically:YES];
            if (value.phase == aura::cli::VoicePhase::finished || value.phase == aura::cli::VoicePhase::failed) {
                voice->stop();
                [NSApp terminate:nil];
            }
        }];
        dispatch_async(dispatch_get_main_queue(), ^{ voice->start(); });
        [NSApp run];
    }
}
