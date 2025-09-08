#pragma once

#include <cstdint>
#include <utility>
#ifdef _WIN32
#include <windows.h>
#elif defined(__linux__)
#include <GL/glx.h>
#elif defined(__APPLE__)
class NSOpenGLContext;
#endif

namespace lizard::platform {

struct WindowDesc {
  std::int32_t x;
  std::int32_t y;
  std::uint32_t width;
  std::uint32_t height;
  // On Windows the overlay always spans the virtual screen, so these are ignored.
};

struct Window {
  void *native = nullptr;
  float dpiScale = 1.0f;
#ifdef _WIN32
  HGLRC glContext = nullptr;
  HDC device = nullptr;
#elif defined(__linux__)
  GLXContext glContext = nullptr;
#elif defined(__APPLE__)
  NSOpenGLContext *glContext = nullptr;
#endif
};

Window create_overlay_window(const WindowDesc &desc);
void destroy_window(Window &window);
void poll_events(Window &window);
bool fullscreen_window_present();
std::pair<float, float> cursor_pos();

} // namespace lizard::platform
