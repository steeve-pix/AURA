#include "VoiceDictation.hpp"
#import <AVFoundation/AVFoundation.h>
#import <Speech/Speech.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <mutex>

namespace aura::cli {
namespace {
struct Capture {
    std::mutex mutex;
    VoiceSnapshot value;
    std::atomic<bool> closed{false};
    std::atomic<float> level{0};
    AVAudioEngine* engine = nil;
    SFSpeechRecognizer* recognizer = nil;
    SFSpeechAudioBufferRecognitionRequest* request = nil;
    SFSpeechRecognitionTask* task = nil;
    bool tapped = false;
};
void fail(std::shared_ptr<Capture> capture, const std::string& message) {
    if (capture->closed) return;
    std::lock_guard lock(capture->mutex);
    capture->value.phase = VoicePhase::failed;
    capture->value.error = message;
}
void begin_capture(std::shared_ptr<Capture> capture) {
    if (capture->closed) return;
    @try {
        NSString* language = NSLocale.preferredLanguages.firstObject ?: @"en-US";
        capture->recognizer = [[SFSpeechRecognizer alloc] initWithLocale:[[NSLocale alloc] initWithLocaleIdentifier:language]];
        if (!capture->recognizer || !capture->recognizer.supportsOnDeviceRecognition) {
            fail(capture, "On-device dictation is unavailable for your macOS language. Enable/download that language in System Settings > Keyboard > Dictation, then retry /voice.");
            return;
        }
        if (!capture->recognizer.available) {
            fail(capture, "macOS speech recognition is currently unavailable. Try /voice again after checking Dictation settings.");
            return;
        }
        capture->recognizer.queue = NSOperationQueue.mainQueue;
        capture->request = [[SFSpeechAudioBufferRecognitionRequest alloc] init];
        capture->request.shouldReportPartialResults = YES;
        capture->request.requiresOnDeviceRecognition = YES;
        capture->request.taskHint = SFSpeechRecognitionTaskHintDictation;
        capture->engine = [[AVAudioEngine alloc] init];
        AVAudioInputNode* input = capture->engine.inputNode;
        AVAudioFormat* format = [input outputFormatForBus:0];
        if (format.sampleRate <= 0 || format.channelCount == 0) {
            fail(capture, "No microphone input is available. Choose an input device in System Settings > Sound.");
            return;
        }
        SFSpeechAudioBufferRecognitionRequest* request = capture->request;
        AVAudioNodeTapBlock tap = ^(AVAudioPCMBuffer* buffer, AVAudioTime*) {
            if (capture->closed) return;
            [request appendAudioPCMBuffer:buffer];
            double sum = 0;
            if (const auto channels = buffer.floatChannelData) {
                for (AVAudioFrameCount i = 0; i < buffer.frameLength; ++i) sum += channels[0][i] * channels[0][i];
            }
            const auto rms = std::sqrt(sum / std::max<AVAudioFrameCount>(1, buffer.frameLength));
            const auto level = std::clamp((20.0 * std::log10(std::max(rms, 0.00001)) + 55.0) / 45.0, 0.0, 1.0);
            capture->level = static_cast<float>(level);
        };
#if __MAC_OS_X_VERSION_MAX_ALLOWED >= 270000
        if (@available(macOS 27.0, *)) {
            NSError* tap_error = nil;
            if (![input installTapOnBus:0 bufferSize:static_cast<AVAudioFrameCount>(format.sampleRate * 0.1)
                                format:format error:&tap_error block:tap]) {
                fail(capture, tap_error.localizedDescription.UTF8String ?: "Could not attach the microphone input.");
                return;
            }
        } else
#endif
        {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
            [input installTapOnBus:0 bufferSize:1024 format:format block:tap];
#pragma clang diagnostic pop
        }
        capture->tapped = true;
        capture->task = [capture->recognizer recognitionTaskWithRequest:request resultHandler:^(SFSpeechRecognitionResult* result, NSError* error) {
            if (capture->closed) return;
            std::lock_guard lock(capture->mutex);
            if (result) {
                capture->value.text = result.bestTranscription.formattedString.UTF8String ?: "";
                if (result.final) capture->value.phase = VoicePhase::finished;
            }
            if (error && capture->value.phase != VoicePhase::finished) {
                capture->value.phase = VoicePhase::failed;
                capture->value.error = error.localizedDescription.UTF8String ?: "Speech recognition failed.";
            }
        }];
        NSError* error = nil;
        [capture->engine prepare];
        if (![capture->engine startAndReturnError:&error]) {
            fail(capture, error.localizedDescription.UTF8String ?: "Could not start the microphone.");
            return;
        }
        std::lock_guard lock(capture->mutex);
        if (capture->value.phase == VoicePhase::permissions) capture->value.phase = VoicePhase::listening;
    } @catch (NSException* exception) {
        fail(capture, exception.reason.UTF8String ?: "Could not initialize macOS dictation.");
    }
}
void request_speech(std::shared_ptr<Capture> capture) {
    if (capture->closed) return;
    [SFSpeechRecognizer requestAuthorization:^(SFSpeechRecognizerAuthorizationStatus status) {
        dispatch_async(dispatch_get_main_queue(), ^{
            if (capture->closed) return;
            if (status != SFSpeechRecognizerAuthorizationStatusAuthorized) {
                fail(capture, "Speech permission is unavailable. Allow Speech Recognition for Aura CLI or your terminal in System Settings > Privacy & Security.");
                return;
            }
            begin_capture(capture);
        });
    }];
}
}
struct VoiceDictation::Impl { std::shared_ptr<Capture> capture = std::make_shared<Capture>(); };
VoiceDictation::VoiceDictation() : impl_(std::make_unique<Impl>()) {}
VoiceDictation::~VoiceDictation() { stop(); }
void VoiceDictation::start() {
    @autoreleasepool {
        const auto capture = impl_->capture;
        if (![NSBundle.mainBundle objectForInfoDictionaryKey:@"NSMicrophoneUsageDescription"]
            || ![NSBundle.mainBundle objectForInfoDictionaryKey:@"NSSpeechRecognitionUsageDescription"]) {
            fail(capture, "Dictation privacy metadata is missing. Reinstall aura-cli with cli/install.sh.");
            return;
        }
        [AVCaptureDevice requestAccessForMediaType:AVMediaTypeAudio completionHandler:^(BOOL granted) {
            dispatch_async(dispatch_get_main_queue(), ^{
                if (!granted) fail(capture, "Microphone permission is unavailable. Allow your terminal or Aura CLI in System Settings > Privacy & Security > Microphone.");
                else request_speech(capture);
            });
        }];
    }
}
void VoiceDictation::poll() {
    @autoreleasepool { CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.01, true); }
}
VoiceSnapshot VoiceDictation::snapshot() const {
    const auto capture = impl_->capture;
    std::lock_guard lock(capture->mutex);
    auto result = capture->value;
    result.level = capture->level.load();
    return result;
}
void VoiceDictation::finish() {
    @autoreleasepool {
        const auto capture = impl_->capture;
        if (capture->closed) return;
        [capture->engine stop];
        if (capture->tapped) {
            [capture->engine.inputNode removeTapOnBus:0];
            capture->tapped = false;
        }
        capture->level = 0;
        [capture->request endAudio];
        [capture->task finish];
    }
}
void VoiceDictation::stop() {
    @autoreleasepool {
        const auto capture = impl_->capture;
        if (capture->closed.exchange(true)) return;
        [capture->engine stop];
        if (capture->tapped) [capture->engine.inputNode removeTapOnBus:0];
        [capture->request endAudio];
        [capture->task cancel];
        capture->task = nil;
        capture->request = nil;
        capture->engine = nil;
        capture->recognizer = nil;
        capture->level = 0;
    }
}
}
