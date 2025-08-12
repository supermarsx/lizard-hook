#include "hook/keyboard_hook.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <thread>

namespace hook {

namespace {

class WindowsKeyboardHook : public KeyboardHook {
public:
  explicit WindowsKeyboardHook(KeyCallback cb) : callback_(std::move(cb)) {}
  ~WindowsKeyboardHook() override { stop(); }

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

  void run(std::stop_token st) {
    thread_id_ = GetCurrentThreadId();
    instance_ = this;
    hook_ = SetWindowsHookExW(WH_KEYBOARD_LL, &HookProc, nullptr, 0);
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
};

} // namespace

std::unique_ptr<KeyboardHook> KeyboardHook::create(KeyCallback callback) {
  return std::make_unique<WindowsKeyboardHook>(std::move(callback));
}

} // namespace hook
