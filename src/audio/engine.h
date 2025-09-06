#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string_view>
#include <string>
#include <vector>
#include <mutex>

struct ma_engine;
struct ma_context;
struct ma_audio_buffer_config;
struct ma_audio_buffer;
struct ma_sound;
enum ma_device_type;
typedef unsigned int ma_endpoint_notification_type; // forward declaration placeholder

namespace lizard::audio {

class Engine {
public:
  Engine(std::uint32_t maxPlaybacks = 16,
         std::chrono::milliseconds cooldown = std::chrono::milliseconds(150));
  ~Engine();

  bool init(std::optional<std::filesystem::path> sound_path = std::nullopt,
            int volume_percent = 100, std::string_view backend = "miniaudio",
            std::uint32_t maxPlaybacks = 0);
  void shutdown();
  void play();
  void set_volume(float vol);

private:
  void set_volume_locked(float vol);

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
  int m_volumePercent{100};
  std::string m_backend{"miniaudio"};
  std::mutex m_mutex;

  static void endpoint_callback(ma_context *pContext, ma_device_type deviceType,
                                ma_endpoint_notification_type notificationType, void *pUserData);
};

} // namespace lizard::audio
