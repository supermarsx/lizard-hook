#pragma once

#include <cstdint>

namespace lizard::platform {

struct WindowDesc {
  std::uint32_t width;
  std::uint32_t height;
};

struct Window {
  void *native = nullptr;
  float dpiScale = 1.0f;
};

Window create_overlay_window(const WindowDesc &desc);
void destroy_window(Window &window);
void poll_events(Window &window);

} // namespace lizard::platform
