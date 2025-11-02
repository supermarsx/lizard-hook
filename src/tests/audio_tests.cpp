#include <catch2/catch_test_macros.hpp>
#include <chrono>

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

TEST_CASE("max_concurrent_playbacks respected", "[audio]") {
  lizard::audio::Engine eng;
  AudioTestAccess::voices(eng).resize(16);

  g_start_calls = 0;
  g_stop_calls = 0;

  for (int i = 0; i < 17; ++i) {
    eng.play();
  }

  REQUIRE(g_start_calls == 17);
  REQUIRE(g_stop_calls == 1);

  int playing = 0;
  for (auto &v : AudioTestAccess::voices(eng)) {
    if (ma_sound_is_playing(&v.sound)) {
      playing++;
    }
  }
  REQUIRE(playing == 16);
}

TEST_CASE("play retriggers immediately", "[audio]") {
  lizard::audio::Engine eng(1);
  AudioTestAccess::voices(eng).resize(1);

  g_start_calls = 0;
  g_stop_calls = 0;

  eng.play();
  eng.play();
  eng.play();

  REQUIRE(g_start_calls == 3);
  REQUIRE(g_stop_calls == 2);
}
