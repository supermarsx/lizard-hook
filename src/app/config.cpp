#include "config.h"

#include <cstdlib>
#include <fstream>

#include <nlohmann/json.hpp>

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

  load();
  if (std::filesystem::exists(config_path_)) {
    last_write_ = std::filesystem::last_write_time(config_path_);
  }

  watcher_ = std::jthread([this](std::stop_token st) {
    using namespace std::chrono_literals;
    while (!st.stop_requested()) {
      std::this_thread::sleep_for(1s);
      if (!std::filesystem::exists(config_path_)) {
        continue;
      }
      auto current = std::filesystem::last_write_time(config_path_);
      if (current != last_write_) {
        last_write_ = current;
        load();
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
      return std::filesystem::path(home) / "Library" / "Application Support" /
             "LizardHook" / "lizard.json";
    }
#else
    if (auto *xdg = std::getenv("XDG_CONFIG_HOME")) {
      return std::filesystem::path(xdg) / "lizard_hook" / "lizard.json";
    }
    if (auto *home = std::getenv("HOME")) {
      return std::filesystem::path(home) / ".config" / "lizard_hook" /
             "lizard.json";
    }
#endif
    return {};
  }

void Config::load() {
  std::ifstream in(config_path_);
  if (!in.is_open()) {
    return; // keep defaults
  }

  try {
    json j;
    in >> j;

    enabled_ = j.value("enabled", true);
    mute_ = j.value("mute", false);
    sound_cooldown_ms_ = j.value("sound_cooldown_ms", 150);
    max_concurrent_playbacks_ = j.value("max_concurrent_playbacks", 4);
    badges_per_second_max_ = j.value("badges_per_second_max", 12);
    badge_min_px_ = j.value("badge_min_px", 60);
    badge_max_px_ = j.value("badge_max_px", 108);
    fullscreen_pause_ = j.value("fullscreen_pause", true);
    exclude_processes_ =
        j.value("exclude_processes", std::vector<std::string>{});
    ignore_injected_ = j.value("ignore_injected", true);
    audio_backend_ = j.value("audio_backend", std::string("miniaudio"));
    badge_spawn_strategy_ =
        j.value("badge_spawn_strategy", std::string("random_screen"));
    volume_percent_ = j.value("volume_percent", 65);
    dpi_scaling_mode_ =
        j.value("dpi_scaling_mode", std::string("per_monitor_v2"));
    logging_level_ = j.value("logging_level", std::string("info"));

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
  } catch (const std::exception &) {
    // keep defaults on parse errors
  }
}

bool Config::enabled() const { return enabled_; }

bool Config::mute() const { return mute_; }

const std::vector<std::string> &Config::emoji() const { return emoji_; }

const std::unordered_map<std::string, double> &Config::emoji_weighted() const {
  return emoji_weighted_;
}

} // namespace lizard::app
