#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <thread>

int g_start_calls = 0;
int g_stop_calls = 0;

#include "stubs/miniaudio.h"
#include "stubs/dr_flac.h"

#define private public
#include "audio/engine.cpp"
#undef private

struct AudioTestAccess {
  static std::vector<lizard::audio::Engine::Voice> &voices(lizard::audio::Engine &e) {
    return e.m_voices;
  }
};

using namespace std::chrono_literals;

TEST_CASE("max_concurrent_playbacks respected", "[audio]") {
  lizard::audio::Engine eng(2, 0ms);
  AudioTestAccess::voices(eng).resize(2);

  g_start_calls = 0;
  g_stop_calls = 0;

  eng.play();
  eng.play();
  eng.play();

  REQUIRE(g_start_calls == 3);
  REQUIRE(g_stop_calls == 1);

  int playing = 0;
  for (auto &v : AudioTestAccess::voices(eng)) {
    if (ma_sound_is_playing(&v.sound)) {
      playing++;
    }
  }
  REQUIRE(playing == 2);
}

TEST_CASE("cooldown prevents rapid retriggers", "[audio]") {
  lizard::audio::Engine eng(1, 50ms);
  AudioTestAccess::voices(eng).resize(1);

  g_start_calls = 0;
  g_stop_calls = 0;

  eng.play();
  eng.play();
  REQUIRE(g_start_calls == 1);

  std::this_thread::sleep_for(60ms);
  eng.play();
  REQUIRE(g_start_calls == 2);
}
