#include "app/config.h"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>

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
