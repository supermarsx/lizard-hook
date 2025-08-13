#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string_view>
#include <vector>

struct ma_engine;
struct ma_context;
struct ma_audio_buffer_config;
struct ma_audio_buffer;
struct ma_sound;

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
};

} // namespace lizard::audio
