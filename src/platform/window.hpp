#pragma once

#include <cstdint>
#ifdef _WIN32
#include <windows.h>
#elif defined(__linux__)
#include <GL/glx.h>
#endif

namespace lizard::platform {

struct WindowDesc {
  std::uint32_t width;
  std::uint32_t height;
};

struct Window {
  void *native = nullptr;
  float dpiScale = 1.0f;
#ifdef _WIN32
  HGLRC glContext = nullptr;
  HDC device = nullptr;
#elif defined(__linux__)
  GLXContext glContext = nullptr;
#endif
};

Window create_overlay_window(const WindowDesc &desc);
void destroy_window(Window &window);
void poll_events(Window &window);

} // namespace lizard::platform
