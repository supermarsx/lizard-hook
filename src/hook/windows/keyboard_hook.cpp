#include "hook/keyboard_hook.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <future>
#include <thread>

#include <spdlog/spdlog.h>

namespace hook {

namespace {

using SetHookFn = HHOOK(WINAPI *)(int, HOOKPROC, HINSTANCE, DWORD);

class WindowsKeyboardHook : public KeyboardHook {
public:
  explicit WindowsKeyboardHook(KeyCallback cb) : callback_(std::move(cb)) {}
  ~WindowsKeyboardHook() override { stop(); }

  bool start() override {
    if (running_) {
      return false;
    }
    std::promise<bool> ready;
    auto future = ready.get_future();
    thread_ = std::jthread(
        [this, p = std::move(ready)](std::stop_token st) mutable { run(st, std::move(p)); });
    bool ok = future.get();
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
    if (thread_.joinable()) {
      PostThreadMessage(thread_id_, WM_QUIT, 0, 0);
      thread_.join();
    }
  }

private:
  static LRESULT CALLBACK HookProc(int code, WPARAM wParam, LPARAM lParam) {
    if (code == HC_ACTION && instance_) {
      const auto *info = reinterpret_cast<KBDLLHOOKSTRUCT *>(lParam);
      bool pressed = wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN;
      instance_->callback_(static_cast<int>(info->vkCode), pressed);
    }
    return CallNextHookEx(nullptr, code, wParam, lParam);
  }

  void run(std::stop_token st, std::promise<bool> started) {
    thread_id_ = GetCurrentThreadId();
    hook_ = set_hook_(WH_KEYBOARD_LL, &HookProc, nullptr, 0);
    if (!hook_) {
      spdlog::error("SetWindowsHookExW failed: {}", GetLastError());
      started.set_value(false);
      return;
    }
    instance_ = this;
    started.set_value(true);
    MSG msg;
    while (!st.stop_requested() && GetMessageW(&msg, nullptr, 0, 0)) {
      // Message loop to keep hook alive.
    }
    UnhookWindowsHookEx(hook_);
    instance_ = nullptr;
  }

  KeyCallback callback_;
  std::jthread thread_;
  DWORD thread_id_{0};
  HHOOK hook_{nullptr};
  bool running_{false};
  static inline WindowsKeyboardHook *instance_{nullptr};
  static inline SetHookFn set_hook_ = &SetWindowsHookExW;
#ifdef LIZARD_TEST
  friend void testing::set_setwindows_hook_ex(SetHookFn);
#endif
};

#ifdef LIZARD_TEST
namespace testing {
inline void set_setwindows_hook_ex(SetHookFn fn) { WindowsKeyboardHook::set_hook_ = fn; }
} // namespace testing
#endif

} // namespace

std::unique_ptr<KeyboardHook> KeyboardHook::create(KeyCallback callback) {
  return std::make_unique<WindowsKeyboardHook>(std::move(callback));
}

} // namespace hook
