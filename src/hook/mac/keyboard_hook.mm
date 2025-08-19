#import <ApplicationServices/ApplicationServices.h>

#include "hook/keyboard_hook.h"
#include "hook/filter.h"

#include "app/config.h"

#include <future>
#include <thread>
#include <filesystem>
#include <string>
#include <libproc.h>

#include <spdlog/spdlog.h>

namespace hook {

namespace {

using CGEventTapCreateFn = CFMachPortRef (*)(CGEventTapLocation, CGEventTapPlacement,
                                             CGEventTapOptions, CGEventMask, CGEventTapCallBack,
                                             void *);

class MacKeyboardHook : public KeyboardHook {
public:
  MacKeyboardHook(KeyCallback cb, const lizard::app::Config &cfg)
      : callback_(std::move(cb)), config_(cfg) {}
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
      bool injected = false;
      CGEventSourceRef source = CGEventCreateEventSourceFromEvent(event);
      if (source) {
        injected = CGEventSourceGetSourceState(source) != kCGEventSourceStateHIDSystemState;
        CFRelease(source);
      }
      pid_t pid =
          static_cast<pid_t>(CGEventGetIntegerValueField(event, kCGEventSourceUnixProcessID));
      std::string proc = process_name_fn_(pid);
      if (should_deliver_event(self->config_, injected, proc)) {
        self->callback_(key, pressed);
      }
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
  const lizard::app::Config &config_;
  std::jthread thread_;
  CFMachPortRef tap_{nullptr};
  CFRunLoopRef run_loop_{nullptr};
  bool running_{false};
  static inline CGEventTapCreateFn cg_event_tap_create_ = &CGEventTapCreate;
  using ProcessNameFn = std::string (*)(pid_t);
#ifdef LIZARD_TEST
  static inline ProcessNameFn process_name_fn_ = [](pid_t) { return std::string(); };
  friend void testing::set_cg_event_tap_create(CGEventTapCreateFn);
  friend void testing::set_process_name_resolver(ProcessNameFn);
#else
  static std::string default_process_name(pid_t pid) {
    char pathbuf[PROC_PIDPATHINFO_MAXSIZE];
    if (proc_pidpath(pid, pathbuf, sizeof(pathbuf)) > 0) {
      return std::filesystem::path(pathbuf).filename().string();
    }
    return {};
  }
  static inline ProcessNameFn process_name_fn_ = &default_process_name;
#endif
};

#ifdef LIZARD_TEST
namespace testing {
inline void set_cg_event_tap_create(CGEventTapCreateFn fn) {
  MacKeyboardHook::cg_event_tap_create_ = fn;
}
inline void set_process_name_resolver(MacKeyboardHook::ProcessNameFn fn) {
  MacKeyboardHook::process_name_fn_ = fn;
}
} // namespace testing
#endif

} // namespace

std::unique_ptr<KeyboardHook> KeyboardHook::create(KeyCallback callback,
                                                   const lizard::app::Config &cfg) {
  return std::make_unique<MacKeyboardHook>(std::move(callback), cfg);
}

} // namespace hook
