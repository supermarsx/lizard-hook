#import <ApplicationServices/ApplicationServices.h>

#include "hook/keyboard_hook.h"

#include <thread>

namespace hook {

namespace {

class MacKeyboardHook : public KeyboardHook {
public:
  explicit MacKeyboardHook(KeyCallback cb) : callback_(std::move(cb)) {}
  ~MacKeyboardHook() override { stop(); }

  bool start() override {
    if (running_) {
      return false;
    }
    running_ = true;
    thread_ = std::jthread([this](std::stop_token st) { run(st); });
    return true;
  }

  void stop() override {
    if (!running_) {
      return;
    }
    running_ = false;
    if (run_loop_) {
      CFRunLoopStop(run_loop_);
    }
    if (thread_.joinable()) {
      thread_.join();
    }
  }

private:
  static CGEventRef TapCallback(CGEventTapProxy proxy, CGEventType type,
                                CGEventRef event, void *refcon) {
    auto *self = static_cast<MacKeyboardHook *>(refcon);
    if (type == kCGEventKeyDown || type == kCGEventKeyUp) {
      int key = static_cast<int>(
          CGEventGetIntegerValueField(event, kCGKeyboardEventKeycode));
      bool pressed = type == kCGEventKeyDown;
      self->callback_(key, pressed);
    }
    return event;
  }

  void run(std::stop_token) {
    CGEventMask mask =
        CGEventMaskBit(kCGEventKeyDown) | CGEventMaskBit(kCGEventKeyUp);
    tap_ = CGEventTapCreate(kCGSessionEventTap, kCGHeadInsertEventTap, 0, mask,
                            &TapCallback, this);
    if (!tap_) {
      running_ = false;
      return;
    }
    run_loop_ = CFRunLoopGetCurrent();
    CFRunLoopSourceRef source =
        CFMachPortCreateRunLoopSource(kCFAllocatorDefault, tap_, 0);
    CFRunLoopAddSource(run_loop_, source, kCFRunLoopCommonModes);
    CGEventTapEnable(tap_, true);
    CFRunLoopRun();
    CFRunLoopRemoveSource(run_loop_, source, kCFRunLoopCommonModes);
    CFRelease(source);
    CFRelease(tap_);
    tap_ = nullptr;
    run_loop_ = nullptr;
  }

  KeyCallback callback_;
  std::jthread thread_;
  CFMachPortRef tap_{nullptr};
  CFRunLoopRef run_loop_{nullptr};
  bool running_{false};
};

} // namespace

std::unique_ptr<KeyboardHook> KeyboardHook::create(KeyCallback callback) {
  return std::make_unique<MacKeyboardHook>(std::move(callback));
}

} // namespace hook
