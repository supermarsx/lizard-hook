#include "config.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include "util/log.h"

using json = nlohmann::json;

namespace lizard::app {

Config::Config(std::filesystem::path executable_dir, std::optional<std::filesystem::path> cli_path,
               std::chrono::milliseconds interval)
    : interval_(interval) {
  if (cli_path && std::filesystem::exists(*cli_path)) {
    config_path_ = *cli_path;
  } else {
    auto user_path = user_config_path();
    if (std::filesystem::exists(user_path)) {
      config_path_ = user_path;
    } else {
      config_path_ = executable_dir / "lizard.json";
    }
  }

  {
    std::unique_lock lock(mutex_);
    load(lock);
    if (std::filesystem::exists(config_path_)) {
      last_write_ = std::filesystem::last_write_time(config_path_);
    }
  }

  watcher_ = std::jthread([this](std::stop_token st) {
    std::unique_lock lk(cv_mutex_);
    while (!st.stop_requested()) {
      cv_.wait_for(lk, interval_, [&] { return st.stop_requested(); });
      if (st.stop_requested()) {
        break;
      }
      lk.unlock();
      std::unique_lock lock(mutex_);
      if (!std::filesystem::exists(config_path_)) {
        lk.lock();
        continue;
      }
      auto current = std::filesystem::last_write_time(config_path_);
      if (current != last_write_) {
        last_write_ = current;
        load(lock);
        reload_cv_.notify_all();
      }
      lk.lock();
    }
  });
}

Config::~Config() {
  watcher_.request_stop();
  cv_.notify_all();
}

void Config::reload() {
  std::unique_lock lock(mutex_);
  load(lock);
  if (std::filesystem::exists(config_path_)) {
    last_write_ = std::filesystem::last_write_time(config_path_);
  }
}

std::filesystem::path Config::user_config_path() {
#ifdef _WIN32
  if (auto *local = std::getenv("LOCALAPPDATA")) {
    return std::filesystem::path(local) / "LizardHook" / "lizard.json";
  }
#elif __APPLE__
  if (auto *home = std::getenv("HOME")) {
    return std::filesystem::path(home) / "Library" / "Application Support" / "LizardHook" /
           "lizard.json";
  }
#else
  if (auto *xdg = std::getenv("XDG_CONFIG_HOME")) {
    return std::filesystem::path(xdg) / "lizard_hook" / "lizard.json";
  }
  if (auto *home = std::getenv("HOME")) {
    return std::filesystem::path(home) / ".config" / "lizard_hook" / "lizard.json";
  }
#endif
  return {};
}

void Config::load(std::unique_lock<std::shared_mutex> &lock) {
  (void)lock; // lock is held by caller
  logging_path_ = config_path_.parent_path() / "lizard.log";
  std::ifstream in(config_path_);
  if (!in.is_open()) {
    spdlog::warn("Could not open config file: {}", config_path_.string());
    lizard::util::init_logging(logging_level_, logging_queue_size_, logging_worker_count_,
                               logging_path_);
    return;
  }

  try {
    json j;
    in >> j;

    enabled_ = j.value("enabled", true);
    mute_ = j.value("mute", false);

    auto clamp_nonneg = [](int value, const char *name) {
      if (value < 0) {
        spdlog::warn("{} negative ({}); clamping to 0", name, value);
        return 0;
      }
      return value;
    };

    sound_cooldown_ms_ = clamp_nonneg(j.value("sound_cooldown_ms", 150), "sound_cooldown_ms");
    max_concurrent_playbacks_ =
        clamp_nonneg(j.value("max_concurrent_playbacks", 16), "max_concurrent_playbacks");
    badges_per_second_max_ =
        clamp_nonneg(j.value("badges_per_second_max", 12), "badges_per_second_max");
    badge_min_px_ = clamp_nonneg(j.value("badge_min_px", 60), "badge_min_px");
    badge_max_px_ = clamp_nonneg(j.value("badge_max_px", 108), "badge_max_px");
    if (badge_max_px_ < badge_min_px_) {
      spdlog::warn("badge_max_px ({}) less than badge_min_px ({}); clamping to {}", badge_max_px_,
                   badge_min_px_, badge_min_px_);
      badge_max_px_ = badge_min_px_;
    }
    fullscreen_pause_ = j.value("fullscreen_pause", true);
    exclude_processes_ = j.value("exclude_processes", std::vector<std::string>{});
    ignore_injected_ = j.value("ignore_injected", true);
    audio_backend_ = j.value("audio_backend", std::string("miniaudio"));
    auto strategy_in = j.value("badge_spawn_strategy", std::string("random_screen"));
    if (strategy_in != "random_screen" && strategy_in != "near_caret") {
      spdlog::warn("Unknown badge_spawn_strategy ({}); defaulting to random_screen", strategy_in);
      strategy_in = "random_screen";
    }
    badge_spawn_strategy_ = std::move(strategy_in);
    fps_mode_ = j.value("fps_mode", std::string("auto"));
    fps_fixed_ = clamp_nonneg(j.value("fps_fixed", 60), "fps_fixed");
    if (fps_fixed_ <= 0) {
      spdlog::warn("fps_fixed non-positive ({}); using 60", fps_fixed_);
      fps_fixed_ = 60;
    }

    int volume_in = j.value("volume_percent", 65);
    volume_percent_ = clamp_nonneg(volume_in, "volume_percent");
    if (volume_percent_ > 100) {
      spdlog::warn("volume_percent ({}) out of range; clamping to 100", volume_percent_);
      volume_percent_ = 100;
    }

    dpi_scaling_mode_ = j.value("dpi_scaling_mode", std::string("per_monitor_v2"));
    logging_level_ = j.value("logging_level", std::string("info"));
    logging_queue_size_ = clamp_nonneg(j.value("logging_queue_size", 8192), "logging_queue_size");
    logging_worker_count_ =
        clamp_nonneg(j.value("logging_worker_count", 1), "logging_worker_count");
    if (logging_worker_count_ == 0) {
      spdlog::warn("logging_worker_count zero; clamping to 1");
      logging_worker_count_ = 1;
    }
    logging_path_ = j.value("logging_path", logging_path_.string());

    if (j.contains("sound_path")) {
      auto path = std::filesystem::path(j.at("sound_path").get<std::string>());
      if (path.empty()) {
        sound_path_ = std::nullopt;
      } else {
        if (!path.is_absolute()) {
          path = config_path_.parent_path() / path;
        }
        sound_path_ = std::move(path);
      }
    } else {
      sound_path_ = std::nullopt;
    }

    if (j.contains("emoji_atlas")) {
      auto path = std::filesystem::path(j.at("emoji_atlas").get<std::string>());
      if (path.empty()) {
        emoji_atlas_ = std::nullopt;
      } else {
        if (!path.is_absolute()) {
          path = config_path_.parent_path() / path;
        }
        emoji_atlas_ = std::move(path);
      }
    } else {
      emoji_atlas_ = std::nullopt;
    }

    emoji_pngs_ = j.value("emoji_pngs", std::vector<std::string>{});

    if (!emoji_pngs_.empty()) {
      emoji_.clear();
      emoji_weighted_.clear();
    } else if (j.contains("emoji_weighted")) {
      emoji_weighted_.clear();
      for (auto &[k, v] : j.at("emoji_weighted").items()) {
        emoji_weighted_[k] = v.get<double>();
      }
      emoji_.clear();
    } else {
      emoji_ = j.value("emoji", std::vector<std::string>{"\U0001F98E"});
      emoji_weighted_.clear();
    }
  } catch (const std::exception &e) {
    spdlog::error("Failed to parse config {}: {}", config_path_.string(), e.what());
  }

  lizard::util::init_logging(logging_level_, logging_queue_size_, logging_worker_count_,
                             logging_path_);
}

bool Config::enabled() const {
  std::shared_lock lock(mutex_);
  return enabled_;
}

bool Config::mute() const {
  std::shared_lock lock(mutex_);
  return mute_;
}

std::vector<std::string> Config::emoji() const {
  std::shared_lock lock(mutex_);
  return emoji_;
}

std::unordered_map<std::string, double> Config::emoji_weighted() const {
  std::shared_lock lock(mutex_);
  return emoji_weighted_;
}

std::vector<std::string> Config::emoji_pngs() const {
  std::shared_lock lock(mutex_);
  return emoji_pngs_;
}

std::optional<std::filesystem::path> Config::sound_path() const {
  std::shared_lock lock(mutex_);
  return sound_path_;
}

std::optional<std::filesystem::path> Config::emoji_atlas() const {
  std::shared_lock lock(mutex_);
  return emoji_atlas_;
}

int Config::sound_cooldown_ms() const {
  std::shared_lock lock(mutex_);
  return sound_cooldown_ms_;
}

int Config::max_concurrent_playbacks() const {
  std::shared_lock lock(mutex_);
  return max_concurrent_playbacks_;
}

int Config::badges_per_second_max() const {
  std::shared_lock lock(mutex_);
  return badges_per_second_max_;
}

int Config::badge_min_px() const {
  std::shared_lock lock(mutex_);
  return badge_min_px_;
}

int Config::badge_max_px() const {
  std::shared_lock lock(mutex_);
  return badge_max_px_;
}

bool Config::fullscreen_pause() const {
  std::shared_lock lock(mutex_);
  return fullscreen_pause_;
}

std::vector<std::string> Config::exclude_processes() const {
  std::shared_lock lock(mutex_);
  return exclude_processes_;
}

bool Config::ignore_injected() const {
  std::shared_lock lock(mutex_);
  return ignore_injected_;
}

std::string Config::audio_backend() const {
  std::shared_lock lock(mutex_);
  return audio_backend_;
}

std::string Config::badge_spawn_strategy() const {
  std::shared_lock lock(mutex_);
  return badge_spawn_strategy_;
}

std::string Config::fps_mode() const {
  std::shared_lock lock(mutex_);
  return fps_mode_;
}

int Config::fps_fixed() const {
  std::shared_lock lock(mutex_);
  return fps_fixed_;
}

int Config::volume_percent() const {
  std::shared_lock lock(mutex_);
  return volume_percent_;
}

std::string Config::dpi_scaling_mode() const {
  std::shared_lock lock(mutex_);
  return dpi_scaling_mode_;
}

std::string Config::logging_level() const {
  std::shared_lock lock(mutex_);
  return logging_level_;
}

int Config::logging_queue_size() const {
  std::shared_lock lock(mutex_);
  return logging_queue_size_;
}

int Config::logging_worker_count() const {
  std::shared_lock lock(mutex_);
  return logging_worker_count_;
}

std::filesystem::path Config::logging_path() const {
  std::shared_lock lock(mutex_);
  return logging_path_;
}

} // namespace lizard::app
