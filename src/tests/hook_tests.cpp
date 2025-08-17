#include "hook/keyboard_hook.h"

#include <catch2/catch_test_macros.hpp>
#include <string>

#ifdef _WIN32
#include <windows.h>
namespace hook::testing {
using SetHookFn = HHOOK(WINAPI *)(int, HOOKPROC, HINSTANCE, DWORD);
void set_setwindows_hook_ex(SetHookFn fn);
} // namespace hook::testing
#elif defined(__APPLE__)
#import <ApplicationServices/ApplicationServices.h>
namespace hook::testing {
using CGEventTapCreateFn = CFMachPortRef (*)(CGEventTapLocation, CGEventTapPlacement,
                                             CGEventTapOptions, CGEventMask, CGEventTapCallBack,
                                             void *);
void set_cg_event_tap_create(CGEventTapCreateFn fn);
} // namespace hook::testing
#elif defined(__linux__)
#include <cstdlib>
#include <X11/Xlib.h>
#include <X11/extensions/record.h>
namespace hook::testing {
using AllocRangeFn = XRecordRange *(*)();
void set_xrecord_alloc_range(AllocRangeFn fn);
} // namespace hook::testing
#endif

TEST_CASE("start fails when platform API unavailable", "[hook]") {
#if defined(_WIN32)
  hook::testing::set_setwindows_hook_ex([](int, HOOKPROC, HINSTANCE, DWORD) -> HHOOK {
    SetLastError(ERROR_ACCESS_DENIED);
    return nullptr;
  });
#elif defined(__APPLE__)
  hook::testing::set_cg_event_tap_create([](CGEventTapLocation, CGEventTapPlacement,
                                            CGEventTapOptions, CGEventMask, CGEventTapCallBack,
                                            void *) { return (CFMachPortRef) nullptr; });
#elif defined(__linux__)
  const char *old_display = std::getenv("DISPLAY");
  if (old_display) {
    std::string saved = old_display;
    setenv("DISPLAY", "", 1);
    auto hk = hook::KeyboardHook::create([](int, bool) {});
    REQUIRE_FALSE(hk->start());
    setenv("DISPLAY", saved.c_str(), 1);
  } else {
    setenv("DISPLAY", "", 1);
    auto hk = hook::KeyboardHook::create([](int, bool) {});
    REQUIRE_FALSE(hk->start());
    unsetenv("DISPLAY");
  }
#else
  auto hk = hook::KeyboardHook::create([](int, bool) {});
  REQUIRE_FALSE(hk->start());
#endif
}

#if defined(__linux__)
TEST_CASE("start fails when XRecordAllocRange fails", "[hook]") {
  if (Display *d = XOpenDisplay(nullptr)) {
    XCloseDisplay(d);
    static bool called = false;
    auto failing_alloc = []() -> XRecordRange * {
      called = true;
      return nullptr;
    };
    hook::testing::set_xrecord_alloc_range(failing_alloc);
    auto hk = hook::KeyboardHook::create([](int, bool) {});
    REQUIRE_FALSE(hk->start());
    REQUIRE(called);
  } else {
    SUCCEED("X display unavailable; skipping test");
  }
}
#endif
