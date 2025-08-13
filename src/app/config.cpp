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

void Config::load() {
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
      sound_path_ = j.at("sound_path").get<std::string>();
    }
    if (j.contains("emoji_path")) {
      emoji_path_ = j.at("emoji_path").get<std::string>();
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

bool Config::enabled() const { return enabled_; }

bool Config::mute() const { return mute_; }

const std::vector<std::string> &Config::emoji() const { return emoji_; }

const std::unordered_map<std::string, double> &Config::emoji_weighted() const {
  return emoji_weighted_;
}

const std::optional<std::filesystem::path> &Config::sound_path() const { return sound_path_; }

const std::optional<std::filesystem::path> &Config::emoji_path() const { return emoji_path_; }

int Config::sound_cooldown_ms() const { return sound_cooldown_ms_; }

int Config::max_concurrent_playbacks() const { return max_concurrent_playbacks_; }

int Config::badges_per_second_max() const { return badges_per_second_max_; }

int Config::badge_min_px() const { return badge_min_px_; }

int Config::badge_max_px() const { return badge_max_px_; }

bool Config::fullscreen_pause() const { return fullscreen_pause_; }

const std::vector<std::string> &Config::exclude_processes() const { return exclude_processes_; }

bool Config::ignore_injected() const { return ignore_injected_; }

const std::string &Config::audio_backend() const { return audio_backend_; }

const std::string &Config::badge_spawn_strategy() const { return badge_spawn_strategy_; }

int Config::volume_percent() const { return volume_percent_; }

const std::string &Config::dpi_scaling_mode() const { return dpi_scaling_mode_; }

const std::string &Config::logging_level() const { return logging_level_; }

} // namespace lizard::app
