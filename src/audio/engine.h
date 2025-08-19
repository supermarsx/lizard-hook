#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string_view>
#include <vector>

#include "miniaudio.h"

namespace lizard::audio {

class Engine {
public:
  Engine(std::uint32_t maxPlaybacks, std::chrono::milliseconds cooldown);
  ~Engine();

  bool init(std::optional<std::filesystem::path> sound_path = std::nullopt,
            int volume_percent = 100, std::string_view backend = "miniaudio");
  void shutdown();
  void play();
  void set_volume(float vol);
  bool restart();

private:
  struct Voice {
    ma_sound sound{};
    std::chrono::steady_clock::time_point start{};
  };

  ma_engine m_engine{};
  ma_context m_context{};
  bool m_contextInitialized{false};
  ma_audio_buffer_config m_bufferConfig{};
  ma_audio_buffer m_buffer{};
  std::vector<Voice> m_voices;
  std::uint32_t m_maxPlaybacks = 0;
  std::chrono::steady_clock::time_point m_lastPlay{};
  std::chrono::milliseconds m_cooldown{};
  float m_volume{1.0f};
  std::optional<std::filesystem::path> m_soundPath{};
  std::string m_backend;
  int m_volumePercent{100};
};

} // namespace lizard::audio
