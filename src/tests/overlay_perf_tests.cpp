#include "overlay/overlay.h"
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <thread>

using namespace std::chrono_literals;

TEST_CASE("overlay sleeps when idle and resumes on badge", "[overlay][perf]") {
  lizard::overlay::Overlay ov;
  lizard::overlay::testing::set_running(ov, true);

  std::jthread th([&](std::stop_token st) { ov.run(st); });

  std::this_thread::sleep_for(50ms);
  REQUIRE(lizard::overlay::testing::frame_count(ov) == 0);

  ov.spawn_badge(0, 0.0f, 0.0f);

  std::this_thread::sleep_for(50ms);
  REQUIRE(lizard::overlay::testing::frame_count(ov) > 0);

  ov.stop();
}
