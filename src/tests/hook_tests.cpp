#include "hook/keyboard_hook.h"

#include <catch2/catch_test_macros.hpp>

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
  setenv("DISPLAY", "", 1);
#endif

  auto hk = hook::KeyboardHook::create([](int, bool) {});
  REQUIRE_FALSE(hk->start());
}
