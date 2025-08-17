#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <array>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <spdlog/sinks/ostream_sink.h>

#define private public
#include "app/config.h"
#undef private

struct OverlayTestAccess;
#define LIZARD_TEST
#include "overlay/overlay.cpp"

struct OverlayTestAccess {
  static std::vector<lizard::overlay::Sprite> &sprites(lizard::overlay::Overlay &o) {
    return o.m_sprites;
  }
  static std::unordered_map<std::string, int> &sprite_lookup(lizard::overlay::Overlay &o) {
    return o.m_sprite_lookup;
  }
  static std::mt19937 &rng(lizard::overlay::Overlay &o) { return o.m_rng; }
  static int select_sprite(lizard::overlay::Overlay &o) { return o.select_sprite(); }
};

using Catch::Approx;
using lizard::app::Config;
using lizard::overlay::Overlay;

TEST_CASE("select_sprite respects weights", "[overlay]") {
  Overlay ov;
  OverlayTestAccess::sprites(ov) = {lizard::overlay::Sprite{0, 0, 0, 0},
                                    lizard::overlay::Sprite{0, 0, 0, 0},
                                    lizard::overlay::Sprite{0, 0, 0, 0}};
  OverlayTestAccess::sprite_lookup(ov) = {{"A", 0}, {"B", 1}, {"C", 2}};

  Config cfg(std::filesystem::temp_directory_path());
  cfg.emoji_weighted_ = {{"A", 1.0}, {"B", 3.0}, {"C", 6.0}};

  ov.init(cfg);
  OverlayTestAccess::rng(ov).seed(42);

  const int samples = 10000;
  std::array<int, 3> counts{0, 0, 0};
  for (int i = 0; i < samples; ++i) {
    int idx = OverlayTestAccess::select_sprite(ov);
    counts[idx]++;
  }
  double total = 1.0 + 3.0 + 6.0;
  REQUIRE(counts[0] == Approx(samples * (1.0 / total)).margin(samples * 0.05));
  REQUIRE(counts[1] == Approx(samples * (3.0 / total)).margin(samples * 0.05));
  REQUIRE(counts[2] == Approx(samples * (6.0 / total)).margin(samples * 0.05));
}

TEST_CASE("invalid atlas logs error and falls back", "[overlay]") {
  std::filesystem::path tmp = std::filesystem::temp_directory_path() / "lizard_bad_atlas.json";
  {
    std::ofstream out(tmp);
    out << "{"; // invalid JSON
  }

  std::ostringstream oss;
  auto sink = std::make_shared<spdlog::sinks::ostream_sink_mt>(oss);
  auto logger = std::make_shared<spdlog::logger>("test", sink);
  auto old_logger = spdlog::default_logger();
  spdlog::set_default_logger(logger);

  Config cfg(std::filesystem::temp_directory_path());
  Overlay ov;
  ov.init(cfg, tmp);
  logger->flush();
  spdlog::set_default_logger(old_logger);

  REQUIRE(oss.str().find("Failed to parse emoji atlas") != std::string::npos);
  REQUIRE(OverlayTestAccess::sprites(ov).size() == 1);
  REQUIRE(OverlayTestAccess::sprite_lookup(ov).count("🦎") == 1);
}
