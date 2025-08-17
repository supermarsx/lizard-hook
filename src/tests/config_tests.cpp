#include "app/config.h"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <chrono>
#include <thread>
#include <spdlog/sinks/memory_sink.h>

using lizard::app::Config;

TEST_CASE("loads default when file missing", "[config]") {
  auto tempdir = std::filesystem::temp_directory_path() / "lizard_cfg_missing";
  std::filesystem::create_directories(tempdir);
  {
    Config cfg(tempdir);
    REQUIRE(cfg.enabled());
    REQUIRE_FALSE(cfg.mute());
  }
  std::filesystem::remove_all(tempdir);
}

TEST_CASE("parses provided values", "[config]") {
  auto tempdir = std::filesystem::temp_directory_path();
  auto cfg_file = tempdir / "lizard_cfg.json";
  std::ofstream out(cfg_file);
  out << R"({"enabled":false,"emoji":["A","B"],"emoji_weighted":{"X":1.0}})";
  out.close();

  Config cfg(tempdir, cfg_file);
  REQUIRE_FALSE(cfg.enabled());
  REQUIRE(cfg.emoji().empty());
  REQUIRE(cfg.emoji_weighted().at("X") == Catch::Approx(1.0));

  std::filesystem::remove(cfg_file);
}

TEST_CASE("parses asset paths", "[config]") {
  auto tempdir = std::filesystem::temp_directory_path();
  auto cfg_file = tempdir / "lizard_cfg_paths.json";
  std::ofstream out(cfg_file);
  out << R"({"sound_path":"custom.flac","emoji_path":"custom.png"})";
  out.close();

  Config cfg(tempdir, cfg_file);
  REQUIRE(cfg.sound_path().has_value());
  REQUIRE(cfg.emoji_path().has_value());

  std::filesystem::remove(cfg_file);
}

TEST_CASE("asset paths reset when removed or empty", "[config]") {
  using namespace std::chrono_literals;
  auto tempdir = std::filesystem::temp_directory_path();
  auto cfg_file = tempdir / "lizard_cfg_paths_reset.json";

  {
    std::ofstream out(cfg_file);
    out << R"({"sound_path":"custom.flac","emoji_path":"custom.png"})";
  }

  Config cfg(tempdir, cfg_file);
  REQUIRE(cfg.sound_path().has_value());
  REQUIRE(cfg.emoji_path().has_value());

  std::this_thread::sleep_for(1s);
  {
    std::ofstream out(cfg_file);
    out << R"({"sound_path":"","emoji_path":""})";
  }

  std::this_thread::sleep_for(2s);
  REQUIRE_FALSE(cfg.sound_path().has_value());
  REQUIRE_FALSE(cfg.emoji_path().has_value());

  std::this_thread::sleep_for(1s);
  {
    std::ofstream out(cfg_file);
    out << R"({})";
  }

  std::this_thread::sleep_for(2s);
  REQUIRE_FALSE(cfg.sound_path().has_value());
  REQUIRE_FALSE(cfg.emoji_path().has_value());

  std::filesystem::remove(cfg_file);
}

TEST_CASE("reloads on file change", "[config]") {
  using namespace std::chrono_literals;
  auto tempdir = std::filesystem::temp_directory_path();
  auto cfg_file = tempdir / "lizard_cfg_reload.json";
  {
    std::ofstream out(cfg_file);
    out << R"({"enabled":false})";
  }

  Config cfg(tempdir, cfg_file);
  REQUIRE_FALSE(cfg.enabled());

  std::this_thread::sleep_for(1s);
  {
    std::ofstream out(cfg_file);
    out << R"({"enabled":true})";
  }

  std::this_thread::sleep_for(2s);
  REQUIRE(cfg.enabled());

  std::filesystem::remove(cfg_file);
}

TEST_CASE("clamps out-of-range numeric values", "[config]") {
  auto tempdir = std::filesystem::temp_directory_path();
  auto cfg_file = tempdir / "lizard_cfg_invalid.json";
  std::ofstream out(cfg_file);
  out << R"({"sound_cooldown_ms":-5,"max_concurrent_playbacks":-1,
"badges_per_second_max":-3,"badge_min_px":-20,"badge_max_px":-10,
"volume_percent":-50})";
  out.close();

  Config cfg(tempdir, cfg_file);
  REQUIRE(cfg.sound_cooldown_ms() == 0);
  REQUIRE(cfg.max_concurrent_playbacks() == 0);
  REQUIRE(cfg.badges_per_second_max() == 0);
  REQUIRE(cfg.badge_min_px() == 0);
  REQUIRE(cfg.badge_max_px() == 0);
  REQUIRE(cfg.volume_percent() == 0);

  std::filesystem::remove(cfg_file);
}

TEST_CASE("enforces badge size ordering", "[config]") {
  auto tempdir = std::filesystem::temp_directory_path();
  auto cfg_file = tempdir / "lizard_cfg_badge_order.json";
  std::ofstream out(cfg_file);
  out << R"({"badge_min_px":120,"badge_max_px":100,"volume_percent":150})";
  out.close();

  Config cfg(tempdir, cfg_file);
  REQUIRE(cfg.badge_min_px() == 120);
  REQUIRE(cfg.badge_max_px() == 120);
  REQUIRE(cfg.volume_percent() == 100);

  std::filesystem::remove(cfg_file);
}

TEST_CASE("logs warnings for adjusted values", "[config]") {
  auto tempdir = std::filesystem::temp_directory_path();
  auto cfg_file = tempdir / "lizard_cfg_warn.json";
  std::ofstream out(cfg_file);
  out << R"({"sound_cooldown_ms":-5,"badge_min_px":-2,"badge_max_px":-1,"volume_percent":150})";
  out.close();

  auto sink = std::make_shared<spdlog::sinks::memory_sink_mt>();
  auto logger = std::make_shared<spdlog::logger>("test", sink);
  spdlog::set_default_logger(logger);

  Config cfg(tempdir, cfg_file);

  bool saw_sound = false;
  bool saw_badge_min = false;
  bool saw_badge_max = false;
  bool saw_volume = false;
  for (const auto &line : sink->lines()) {
    if (line.find("sound_cooldown_ms") != std::string::npos)
      saw_sound = true;
    if (line.find("badge_min_px") != std::string::npos)
      saw_badge_min = true;
    if (line.find("badge_max_px") != std::string::npos)
      saw_badge_max = true;
    if (line.find("volume_percent") != std::string::npos)
      saw_volume = true;
  }
  REQUIRE(saw_sound);
  REQUIRE(saw_badge_min);
  REQUIRE(saw_badge_max);
  REQUIRE(saw_volume);

  std::filesystem::remove(cfg_file);
}
