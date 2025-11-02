#include "engine.h"

#include <algorithm>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>
#include <mutex>

#include "embedded.h"
#include <spdlog/spdlog.h>

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

std::optional<DecodedSample> load_flac_file(const std::string &path) {
  drflac *flac = drflac_open_file(path.c_str(), nullptr);
  if (flac == nullptr) {
    spdlog::error("Failed to open FLAC file {}", path);
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

std::optional<DecodedSample> load_flac_memory(const unsigned char *data, size_t size) {
  drflac *flac = drflac_open_memory(data, size, nullptr);
  if (flac == nullptr) {
    spdlog::error("Failed to open embedded FLAC data");
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

void Engine::endpoint_callback(ma_context *, ma_device_type deviceType,
                               ma_endpoint_notification_type notificationType, void *pUserData) {
  if (deviceType != ma_device_type_playback ||
      notificationType != ma_endpoint_notification_type_default_changed) {
    return;
  }

  auto *self = static_cast<Engine *>(pUserData);
  if (self == nullptr) {
    return;
  }

  std::lock_guard<std::mutex> lock(self->m_mutex);
  float currentVol = self->m_volume;
  self->shutdown();
  self->init(self->m_soundPath, self->m_volumePercent, self->m_backend, self->m_maxPlaybacks);
  self->set_volume_locked(currentVol);
}

Engine::Engine(std::uint32_t maxPlaybacks) : m_maxPlaybacks(maxPlaybacks) {}

Engine::~Engine() { shutdown(); }

bool Engine::init(std::optional<std::filesystem::path> sound_path, int volume_percent,
                  std::string_view backend, std::uint32_t maxPlaybacks) {
  if (maxPlaybacks > 0) {
    m_maxPlaybacks = maxPlaybacks;
  }
  m_soundPath = sound_path;
  m_volumePercent = volume_percent;
  m_backend = std::string(backend);

  ma_engine_config engineConfig = ma_engine_config_init();

  ma_backend maBackend{};
  bool useBackend = true;
  if (backend == "wasapi") {
    maBackend = ma_backend_wasapi;
  } else if (backend == "coreaudio") {
    maBackend = ma_backend_coreaudio;
  } else if (backend == "alsa") {
    maBackend = ma_backend_alsa;
  } else {
    useBackend = false;
  }

  ma_result result = MA_SUCCESS;
  if (useBackend) {
    ma_context_config contextConfig = ma_context_config_init();
    const ma_backend backends[] = {maBackend};
    result = ma_context_init(backends, 1, &contextConfig, &m_context);
    if (result != MA_SUCCESS) {
      spdlog::error("ma_context_init failed: {}", result);
      return false;
    }
    m_contextInitialized = true;
    engineConfig.pContext = &m_context;
    ma_context_set_endpoint_notification_callback(&m_context, Engine::endpoint_callback, this);
  }

  result = ma_engine_init(&engineConfig, &m_engine);
  if (result != MA_SUCCESS) {
    spdlog::error("ma_engine_init failed: {}", result);
    if (m_contextInitialized) {
      ma_context_uninit(&m_context);
      m_contextInitialized = false;
    }
    return false;
  }

  std::optional<DecodedSample> decoded;
  if (sound_path && std::filesystem::exists(*sound_path)) {
    decoded = load_flac_file(sound_path->string());
  } else {
    decoded = load_flac_memory(lizard::assets::lizard_processed_clean_no_meta_flac,
                               lizard::assets::lizard_processed_clean_no_meta_flac_len);
  }
  if (!decoded) {
    spdlog::error("Failed to decode audio sample");
    ma_engine_uninit(&m_engine);
    if (m_contextInitialized) {
      ma_context_uninit(&m_context);
      m_contextInitialized = false;
    }
    return false;
  }

  m_bufferConfig = ma_audio_buffer_config_init(ma_format_f32, decoded->channels, decoded->frames,
                                               decoded->pcm.data(), nullptr);
  result = ma_audio_buffer_init(&m_bufferConfig, &m_buffer);
  if (result != MA_SUCCESS) {
    spdlog::error("ma_audio_buffer_init failed: {}", result);
    ma_engine_uninit(&m_engine);
    if (m_contextInitialized) {
      ma_context_uninit(&m_context);
      m_contextInitialized = false;
    }
    return false;
  }

  m_voices.resize(m_maxPlaybacks);
  for (auto &voice : m_voices) {
    ma_sound_init_from_data_source(&m_engine, &m_buffer, 0, nullptr, &voice.sound);
  }
  int clampedPercent = std::clamp(volume_percent, 0, 100);
  set_volume_locked(static_cast<float>(clampedPercent) / 100.0f);
  return true;
}

void Engine::shutdown() {
  for (auto &voice : m_voices) {
    ma_sound_uninit(&voice.sound);
  }
  m_voices.clear();
  ma_audio_buffer_uninit(&m_buffer);
  ma_engine_uninit(&m_engine);
  if (m_contextInitialized) {
    ma_context_uninit(&m_context);
    m_contextInitialized = false;
  }
}

void Engine::play() {
  std::lock_guard<std::mutex> lock(m_mutex);
  auto now = std::chrono::steady_clock::now();

  Voice *target = nullptr;
  for (auto &voice : m_voices) {
    if (!ma_sound_is_playing(&voice.sound)) {
      target = &voice;
      break;
    }
  }

  if (target == nullptr) {
    target = &*std::min_element(m_voices.begin(), m_voices.end(),
                                [](const Voice &a, const Voice &b) { return a.start < b.start; });
    ma_sound_stop(&target->sound);
  }

  ma_sound_seek_to_pcm_frame(&target->sound, 0);
  ma_sound_start(&target->sound);
  target->start = now;
}

void Engine::set_volume(float vol) {
  std::lock_guard<std::mutex> lock(m_mutex);
  set_volume_locked(vol);
}

void Engine::set_volume_locked(float vol) {
  m_volume = std::clamp(vol, 0.0f, 1.0f);
  m_volumePercent = static_cast<int>(m_volume * 100.0f);
  for (auto &voice : m_voices) {
    ma_sound_set_volume(&voice.sound, m_volume);
  }
}

} // namespace lizard::audio
