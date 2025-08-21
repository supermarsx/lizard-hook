#include "hook/keyboard_hook.h"
#include "hook/filter.h"

#include "app/config.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <future>
#include <thread>
#include <filesystem>
#include <string>

#include <spdlog/spdlog.h>

namespace hook {

namespace {

using SetHookFn = HHOOK(WINAPI *)(int, HOOKPROC, HINSTANCE, DWORD);

class WindowsKeyboardHook : public KeyboardHook {
public:
  WindowsKeyboardHook(KeyCallback cb, const lizard::app::Config &cfg)
      : callback_(std::move(cb)), config_(cfg) {}
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
      bool injected = (info->flags & (LLKHF_INJECTED | LLKHF_LOWER_IL_INJECTED)) != 0;
      std::string proc = process_name_();
      if (!should_deliver_event(instance_->config_, injected, proc)) {
        return CallNextHookEx(nullptr, code, wParam, lParam);
      }
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
  const lizard::app::Config &config_;
  std::jthread thread_;
  DWORD thread_id_{0};
  HHOOK hook_{nullptr};
  bool running_{false};
  static inline WindowsKeyboardHook *instance_{nullptr};
  static inline SetHookFn set_hook_ = &SetWindowsHookExW;
  using ProcessNameFn = std::string (*)();
#ifdef LIZARD_TEST
  static inline ProcessNameFn process_name_ = []() { return std::string(); };
#else
  static std::string default_process_name() {
    std::string name;
    HWND hwnd = GetForegroundWindow();
    if (!hwnd) {
      return name;
    }
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (!pid) {
      return name;
    }
    HANDLE proc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!proc) {
      return name;
    }
    wchar_t path[MAX_PATH];
    DWORD size = MAX_PATH;
    if (QueryFullProcessImageNameW(proc, 0, path, &size)) {
      name = std::filesystem::path(path).filename().string();
    }
    CloseHandle(proc);
    return name;
  }
  static inline ProcessNameFn process_name_ = &default_process_name;
#endif
#ifdef LIZARD_TEST
  friend void testing::set_setwindows_hook_ex(SetHookFn);
  friend void testing::set_process_name_resolver(ProcessNameFn);
#endif
};

#ifdef LIZARD_TEST
namespace testing {
inline void set_setwindows_hook_ex(SetHookFn hook_fn) { WindowsKeyboardHook::set_hook_ = hook_fn; }
inline void set_process_name_resolver(WindowsKeyboardHook::ProcessNameFn fn) {
  WindowsKeyboardHook::process_name_ = fn;
}
} // namespace testing
#endif

} // namespace

auto KeyboardHook::create(KeyCallback callback, const lizard::app::Config &cfg)
    -> std::unique_ptr<KeyboardHook> {
  return std::make_unique<WindowsKeyboardHook>(std::move(callback), cfg);
}

} // namespace hook
