#pragma once

#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace lizard::app {

class Config {
public:
  Config(std::filesystem::path executable_dir,
         std::optional<std::filesystem::path> cli_path = std::nullopt,
         std::chrono::milliseconds interval = std::chrono::seconds(1));
  ~Config();

  bool enabled() const;
  bool mute() const;
  std::vector<std::string> emoji() const;
  std::unordered_map<std::string, double> emoji_weighted() const;
  std::optional<std::filesystem::path> sound_path() const;
  std::optional<std::filesystem::path> emoji_path() const;
  int sound_cooldown_ms() const;
  int max_concurrent_playbacks() const;
  int badges_per_second_max() const;
  int badge_min_px() const;
  int badge_max_px() const;
  bool fullscreen_pause() const;
  std::vector<std::string> exclude_processes() const;
  bool ignore_injected() const;
  std::string audio_backend() const;
  std::string badge_spawn_strategy() const;
  int volume_percent() const;
  std::string dpi_scaling_mode() const;
  std::string logging_level() const;
  int logging_queue_size() const;
  int logging_worker_count() const;
  std::filesystem::path logging_path() const;

  std::condition_variable &reload_cv() { return reload_cv_; }

private:
  void load(std::unique_lock<std::shared_mutex> &lock);
  static std::filesystem::path user_config_path();

  mutable std::shared_mutex mutex_;
  std::filesystem::path config_path_;
  std::filesystem::file_time_type last_write_{};
  std::jthread watcher_;
  std::chrono::milliseconds interval_;
  std::condition_variable cv_;
  std::mutex cv_mutex_;
  std::condition_variable reload_cv_;

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
  int logging_queue_size_{8192};
  int logging_worker_count_{1};
  std::filesystem::path logging_path_{};
};

} // namespace lizard::app
