#pragma once

#include <functional>

namespace lizard::platform {

struct TrayState {
  bool enabled = true;
  bool muted = false;
  bool fullscreen_pause = false;
  bool show_fps = false;
};

struct TrayCallbacks {
  std::function<void(bool)> toggle_enabled;
  std::function<void(bool)> toggle_mute;
  std::function<void(bool)> toggle_fullscreen_pause;
  std::function<void(bool)> toggle_fps;
  std::function<void()> open_config;
  std::function<void()> open_logs;
  std::function<void()> quit;
};

bool init_tray(const TrayState &state, const TrayCallbacks &callbacks);
void update_tray(const TrayState &state);
void shutdown_tray();

} // namespace lizard::platform
