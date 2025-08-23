#pragma once

#include <functional>

namespace lizard::platform {

enum class FpsMode {
  Auto,
  Fixed,
};

struct TrayState {
  bool enabled = true;
  bool muted = false;
  bool fullscreen_pause = false;
  FpsMode fps_mode = FpsMode::Auto;
  int fps_fixed = 60;
};

struct TrayCallbacks {
  std::function<void(bool)> toggle_enabled;
  std::function<void(bool)> toggle_mute;
  std::function<void(bool)> toggle_fullscreen_pause;
  std::function<void(FpsMode)> set_fps_mode;
  std::function<void(int)> set_fps_fixed;
  std::function<void()> open_config;
  std::function<void()> open_logs;
  std::function<void()> quit;
};

bool init_tray(const TrayState &state, const TrayCallbacks &callbacks);
void update_tray(const TrayState &state);
void shutdown_tray();

} // namespace lizard::platform
