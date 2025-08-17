#include "config.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include "util/log.h"

using json = nlohmann::json;

namespace lizard::app {

Config::Config(std::filesystem::path executable_dir,
               std::optional<std::filesystem::path> cli_path) {
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
    using namespace std::chrono_literals;
    while (!st.stop_requested()) {
      std::this_thread::sleep_for(1s);
      std::unique_lock lock(mutex_);
      if (!std::filesystem::exists(config_path_)) {
        continue;
      }
      auto current = std::filesystem::last_write_time(config_path_);
      if (current != last_write_) {
        last_write_ = current;
        load(lock);
      }
    }
  });
}

Config::~Config() { watcher_.request_stop(); }

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
  std::ifstream in(config_path_);
  if (!in.is_open()) {
    spdlog::warn("Could not open config file: {}", config_path_.string());
    lizard::util::init_logging(logging_level_);
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
        clamp_nonneg(j.value("max_concurrent_playbacks", 4), "max_concurrent_playbacks");
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
    badge_spawn_strategy_ = j.value("badge_spawn_strategy", std::string("random_screen"));

    int volume_in = j.value("volume_percent", 65);
    volume_percent_ = clamp_nonneg(volume_in, "volume_percent");
    if (volume_percent_ > 100) {
      spdlog::warn("volume_percent ({}) out of range; clamping to 100", volume_percent_);
      volume_percent_ = 100;
    }

    dpi_scaling_mode_ = j.value("dpi_scaling_mode", std::string("per_monitor_v2"));
    logging_level_ = j.value("logging_level", std::string("info"));

    if (j.contains("sound_path")) {
      auto path = j.at("sound_path").get<std::string>();
      if (path.empty()) {
        sound_path_ = std::nullopt;
      } else {
        sound_path_ = path;
      }
    } else {
      sound_path_ = std::nullopt;
    }

    if (j.contains("emoji_path")) {
      auto path = j.at("emoji_path").get<std::string>();
      if (path.empty()) {
        emoji_path_ = std::nullopt;
      } else {
        emoji_path_ = path;
      }
    } else {
      emoji_path_ = std::nullopt;
    }

    if (j.contains("emoji_weighted")) {
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

  lizard::util::init_logging(logging_level_);
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

std::optional<std::filesystem::path> Config::sound_path() const {
  std::shared_lock lock(mutex_);
  return sound_path_;
}

std::optional<std::filesystem::path> Config::emoji_path() const {
  std::shared_lock lock(mutex_);
  return emoji_path_;
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

} // namespace lizard::app
