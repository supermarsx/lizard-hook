#import <ApplicationServices/ApplicationServices.h>

#include "hook/keyboard_hook.h"

#include <future>
#include <thread>

#include <spdlog/spdlog.h>

namespace hook {

namespace {

using CGEventTapCreateFn = CFMachPortRef (*)(CGEventTapLocation, CGEventTapPlacement,
                                             CGEventTapOptions, CGEventMask, CGEventTapCallBack,
                                             void *);

class MacKeyboardHook : public KeyboardHook {
public:
  explicit MacKeyboardHook(KeyCallback cb) : callback_(std::move(cb)) {}
  ~MacKeyboardHook() override { stop(); }

  bool start() override {
    if (running_) {
      return false;
    }
    std::promise<bool> ready;
    auto fut = ready.get_future();
    thread_ = std::jthread(
        [this, p = std::move(ready)](std::stop_token st) mutable { run(st, std::move(p)); });
    bool ok = fut.get();
    running_ = ok;
    if (!ok && thread_.joinable()) {
      thread_.join();
    }
    return ok;
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
  static CGEventRef TapCallback(CGEventTapProxy proxy, CGEventType type, CGEventRef event,
                                void *refcon) {
    auto *self = static_cast<MacKeyboardHook *>(refcon);
    if (type == kCGEventKeyDown || type == kCGEventKeyUp) {
      int key = static_cast<int>(CGEventGetIntegerValueField(event, kCGKeyboardEventKeycode));
      bool pressed = type == kCGEventKeyDown;
      self->callback_(key, pressed);
    }
    return event;
  }

  void run(std::stop_token, std::promise<bool> started) {
    CGEventMask mask = CGEventMaskBit(kCGEventKeyDown) | CGEventMaskBit(kCGEventKeyUp);
    tap_ = cg_event_tap_create_(kCGSessionEventTap, kCGHeadInsertEventTap, 0, mask, &TapCallback,
                                this);
    if (!tap_) {
      spdlog::error("CGEventTapCreate failed");
      started.set_value(false);
      running_ = false;
      return;
    }
    run_loop_ = CFRunLoopGetCurrent();
    started.set_value(true);
    CFRunLoopSourceRef source = CFMachPortCreateRunLoopSource(kCFAllocatorDefault, tap_, 0);
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
  static inline CGEventTapCreateFn cg_event_tap_create_ = &CGEventTapCreate;
#ifdef LIZARD_TEST
  friend void testing::set_cg_event_tap_create(CGEventTapCreateFn);
#endif
};

#ifdef LIZARD_TEST
namespace testing {
inline void set_cg_event_tap_create(CGEventTapCreateFn fn) {
  MacKeyboardHook::cg_event_tap_create_ = fn;
}
} // namespace testing
#endif

} // namespace

std::unique_ptr<KeyboardHook> KeyboardHook::create(KeyCallback callback) {
  return std::make_unique<MacKeyboardHook>(std::move(callback));
}

} // namespace hook
