#include <algorithm>
#include <chrono>
#include <optional>
#include <string>
#include <vector>

#if defined(LIZARD_AUDIO_WASAPI)
#define MA_ENABLE_WASAPI
#elif defined(LIZARD_AUDIO_COREAUDIO)
#define MA_ENABLE_COREAUDIO
#elif defined(LIZARD_AUDIO_ALSA)
#define MA_ENABLE_ALSA
#endif

#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

#define DR_FLAC_IMPLEMENTATION
#include "dr_flac.h"

namespace lizard::audio {

namespace {
struct DecodedSample {
  std::vector<float> pcm;
  ma_uint64 frames = 0;
  ma_uint32 channels = 0;
  ma_uint32 sampleRate = 0;
};

std::optional<DecodedSample> load_flac(const std::string &path) {
  drflac *flac = drflac_open_file(path.c_str(), nullptr);
  if (flac == nullptr) {
    return std::nullopt;
  }

  DecodedSample sample;
  sample.frames = flac->totalPCMFrameCount;
  sample.channels = flac->channels;
  sample.sampleRate = flac->sampleRate;
  sample.pcm.resize(sample.frames * sample.channels);
  drflac_read_pcm_frames_f32(flac, sample.frames, sample.pcm.data());
  drflac_close(flac);
  return sample;
}
} // namespace

class Engine {
public:
  Engine(std::uint32_t maxPlaybacks, std::chrono::milliseconds cooldown)
      : m_maxPlaybacks(maxPlaybacks), m_cooldown(cooldown) {}

  ~Engine() { shutdown(); }

  bool init() {
    ma_result result = ma_engine_init(nullptr, &m_engine);
    if (result != MA_SUCCESS) {
      return false;
    }

    auto decoded = load_flac("assets/lizard.flac");
    if (!decoded) {
      ma_engine_uninit(&m_engine);
      return false;
    }

    m_bufferConfig = ma_audio_buffer_config_init(
        ma_format_f32, decoded->channels, decoded->frames, decoded->pcm.data(),
        nullptr);
    result = ma_audio_buffer_init(&m_bufferConfig, &m_buffer);
    if (result != MA_SUCCESS) {
      ma_engine_uninit(&m_engine);
      return false;
    }

    m_voices.resize(m_maxPlaybacks);
    for (auto &voice : m_voices) {
      ma_sound_init_from_data_source(&m_engine, &m_buffer, 0, nullptr,
                                     &voice.sound);
    }
    return true;
  }

  void shutdown() {
    for (auto &voice : m_voices) {
      ma_sound_uninit(&voice.sound);
    }
    m_voices.clear();
    ma_audio_buffer_uninit(&m_buffer);
    ma_engine_uninit(&m_engine);
  }

  void play() {
    auto now = std::chrono::steady_clock::now();
    if ((now - m_lastPlay) < m_cooldown) {
      return;
    }
    m_lastPlay = now;

    Voice *target = nullptr;
    for (auto &voice : m_voices) {
      if (!ma_sound_is_playing(&voice.sound)) {
        target = &voice;
        break;
      }
    }

    if (target == nullptr) {
      target = &*std::min_element(
          m_voices.begin(), m_voices.end(),
          [](const Voice &a, const Voice &b) { return a.start < b.start; });
      ma_sound_stop(&target->sound);
    }

    ma_sound_seek_to_pcm_frame(&target->sound, 0);
    ma_sound_start(&target->sound);
    target->start = now;
  }

private:
  struct Voice {
    ma_sound sound{};
    std::chrono::steady_clock::time_point start{};
  };

  ma_engine m_engine{};
  ma_audio_buffer_config m_bufferConfig{};
  ma_audio_buffer m_buffer{};
  std::vector<Voice> m_voices;
  std::uint32_t m_maxPlaybacks = 0;
  std::chrono::steady_clock::time_point m_lastPlay{};
  std::chrono::milliseconds m_cooldown{};
};

} // namespace lizard::audio
