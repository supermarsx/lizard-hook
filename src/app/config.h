#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace lizard::app {

class Config {
public:
  Config(std::filesystem::path executable_dir,
         std::optional<std::filesystem::path> cli_path = std::nullopt);
  ~Config();

  bool enabled() const;
  bool mute() const;
  const std::vector<std::string> &emoji() const;
  const std::unordered_map<std::string, double> &emoji_weighted() const;
  const std::optional<std::filesystem::path> &sound_path() const;
  const std::optional<std::filesystem::path> &emoji_path() const;

private:
  void load();
  static std::filesystem::path user_config_path();

  std::filesystem::path config_path_;
  std::filesystem::file_time_type last_write_{};
  std::jthread watcher_;

  // config values
  bool enabled_{true};
  bool mute_{false};
  int sound_cooldown_ms_{150};
  int max_concurrent_playbacks_{4};
  int badges_per_second_max_{12};
  int badge_min_px_{60};
  int badge_max_px_{108};
  std::vector<std::string> emoji_{"\U0001F98E"};
  std::unordered_map<std::string, double> emoji_weighted_{};
  std::optional<std::filesystem::path> sound_path_{};
  std::optional<std::filesystem::path> emoji_path_{};
  bool fullscreen_pause_{true};
  std::vector<std::string> exclude_processes_{};
  bool ignore_injected_{true};
  std::string audio_backend_{"miniaudio"};
  std::string badge_spawn_strategy_{"random_screen"};
  int volume_percent_{65};
  std::string dpi_scaling_mode_{"per_monitor_v2"};
  std::string logging_level_{"info"};
};

} // namespace lizard::app
