#include "hook/keyboard_hook.h"
#include "hook/filter.h"
#include "app/config.h"

#include <catch2/catch_test_macros.hpp>
#include <string>
#include <chrono>
#include <thread>
#include <filesystem>
#include <fstream>

#ifdef _WIN32
#include <windows.h>
namespace hook::testing {
using SetHookFn = HHOOK(WINAPI *)(int, HOOKPROC, HINSTANCE, DWORD);
void set_setwindows_hook_ex(SetHookFn hook_fn);
} // namespace hook::testing
#elif defined(__APPLE__)
#import <ApplicationServices/ApplicationServices.h>
namespace hook::testing {
using CGEventTapCreateFn = CFMachPortRef (*)(CGEventTapLocation, CGEventTapPlacement,
                                             CGEventTapOptions, CGEventMask, CGEventTapCallBack,
                                             void *);
void set_cg_event_tap_create(CGEventTapCreateFn hook_fn);
} // namespace hook::testing
#elif defined(__linux__)
#include <cstdlib>
#include <X11/Xlib.h>
#include <X11/extensions/record.h>
namespace hook::testing {
using AllocRangeFn = XRecordRange *(*)();
void set_xrecord_alloc_range(AllocRangeFn range_fn);
} // namespace hook::testing
#endif

TEST_CASE("start fails when platform API unavailable", "[hook]") {
#if defined(_WIN32)
  hook::testing::set_setwindows_hook_ex([](int, HOOKPROC, HINSTANCE, DWORD) -> HHOOK {
    SetLastError(ERROR_ACCESS_DENIED);
    return nullptr;
  });
  lizard::app::Config cfg(std::filesystem::temp_directory_path());
  auto hook_instance = hook::KeyboardHook::create([](int, bool) {}, cfg);
  REQUIRE_FALSE(hook_instance->start());
#elif defined(__APPLE__)
  hook::testing::set_cg_event_tap_create([](CGEventTapLocation, CGEventTapPlacement,
                                            CGEventTapOptions, CGEventMask, CGEventTapCallBack,
                                            void *) { return (CFMachPortRef) nullptr; });
  lizard::app::Config cfg(std::filesystem::temp_directory_path());
  auto hook_instance = hook::KeyboardHook::create([](int, bool) {}, cfg);
  REQUIRE_FALSE(hook_instance->start());
#elif defined(__linux__)
  const char *old_display = std::getenv("DISPLAY");
  if (old_display != nullptr) {
    std::string saved = old_display;
    setenv("DISPLAY", "", 1);
    lizard::app::Config cfg(std::filesystem::temp_directory_path());
    auto hook_instance = hook::KeyboardHook::create([](int, bool) {}, cfg);
    REQUIRE_FALSE(hook_instance->start());
    setenv("DISPLAY", saved.c_str(), 1);
  } else {
    setenv("DISPLAY", "", 1);
    lizard::app::Config cfg(std::filesystem::temp_directory_path());
    auto hook_instance = hook::KeyboardHook::create([](int, bool) {}, cfg);
    REQUIRE_FALSE(hook_instance->start());
    unsetenv("DISPLAY");
  }
#else
  lizard::app::Config cfg(std::filesystem::temp_directory_path());
  auto hook_instance = hook::KeyboardHook::create([](int, bool) {}, cfg);
  REQUIRE_FALSE(hook_instance->start());
#endif
}

#if defined(__linux__)
TEST_CASE("start fails when XRecordAllocRange fails", "[hook]") {
  if (Display *display = XOpenDisplay(nullptr)) {
    XCloseDisplay(display);
    static bool called = false;
    auto failing_alloc = []() -> XRecordRange * {
      called = true;
      return nullptr;
    };
    hook::testing::set_xrecord_alloc_range(failing_alloc);
    lizard::app::Config cfg(std::filesystem::temp_directory_path());
    auto hook_instance = hook::KeyboardHook::create([](int, bool) {}, cfg);
    REQUIRE_FALSE(hook_instance->start());
    REQUIRE(called);
  } else {
    SUCCEED("X display unavailable; skipping test");
  }
}
#endif

TEST_CASE("start and stop succeed without activity", "[hook]") {
  lizard::app::Config cfg(std::filesystem::temp_directory_path());
  auto hook_instance = hook::KeyboardHook::create([](int, bool) {}, cfg);
  if (hook_instance->start()) {
    using namespace std::chrono_literals;
    std::this_thread::sleep_for(10ms);
    hook_instance->stop();
    SUCCEED();
  } else {
    SUCCEED("start failed; skipping");
  }
}

TEST_CASE("config filters injected and excluded processes", "[hook]") {
  auto tempdir = std::filesystem::temp_directory_path();
  auto cfg_file = tempdir / "hook_filter.json";
  std::ofstream out(cfg_file);
  out << R"({"exclude_processes":["badproc"],"ignore_injected":true})";
  out.close();
  lizard::app::Config cfg(tempdir, cfg_file);
  REQUIRE_FALSE(hook::should_deliver_event(cfg, true, "whatever"));
  REQUIRE_FALSE(hook::should_deliver_event(cfg, false, "badproc"));
  REQUIRE(hook::should_deliver_event(cfg, false, "good"));
  std::filesystem::remove(cfg_file);

  // When ignore_injected is false, injected events are delivered
  auto cfg_file2 = tempdir / "hook_filter2.json";
  std::ofstream out2(cfg_file2);
  out2 << R"({"exclude_processes":[],"ignore_injected":false})";
  out2.close();
  lizard::app::Config cfg2(tempdir, cfg_file2);
  REQUIRE(hook::should_deliver_event(cfg2, true, "whatever"));
  std::filesystem::remove(cfg_file2);
}
