#pragma once
#include <cstdint>

using ma_uint64 = std::uint64_t;
using ma_uint32 = std::uint32_t;
using ma_result = int;
using ma_backend = int;
using ma_format = int;

inline constexpr ma_result MA_SUCCESS = 0;
inline constexpr ma_format ma_format_f32 = 1;
inline constexpr ma_backend ma_backend_wasapi = 1;
inline constexpr ma_backend ma_backend_coreaudio = 2;
inline constexpr ma_backend ma_backend_alsa = 3;

struct ma_engine {};
struct ma_context {};
struct ma_engine_config {
  ma_context *pContext = nullptr;
};
struct ma_context_config {};
struct ma_audio_buffer_config {};
struct ma_audio_buffer {};
struct ma_sound {
  bool playing = false;
};

inline ma_engine_config ma_engine_config_init() { return {}; }
inline ma_context_config ma_context_config_init() { return {}; }
inline ma_result ma_context_init(const ma_backend *, ma_uint32, const ma_context_config *,
                                 ma_context *) {
  return MA_SUCCESS;
}
inline void ma_context_uninit(ma_context *) {}
inline ma_result ma_engine_init(const ma_engine_config *, ma_engine *) { return MA_SUCCESS; }
inline void ma_engine_uninit(ma_engine *) {}
inline ma_audio_buffer_config ma_audio_buffer_config_init(ma_format, ma_uint32, ma_uint64,
                                                          const void *, void *) {
  return {};
}
inline ma_result ma_audio_buffer_init(const ma_audio_buffer_config *, ma_audio_buffer *) {
  return MA_SUCCESS;
}
inline void ma_audio_buffer_uninit(ma_audio_buffer *) {}
inline ma_result ma_sound_init_from_data_source(ma_engine *, ma_audio_buffer *, int, void *,
                                                ma_sound *s) {
  s->playing = false;
  return MA_SUCCESS;
}
inline void ma_sound_uninit(ma_sound *) {}
inline bool ma_sound_is_playing(const ma_sound *s) { return s->playing; }
extern int g_stop_calls;
extern int g_start_calls;
inline void ma_sound_stop(ma_sound *s) {
  s->playing = false;
  ++g_stop_calls;
}
inline void ma_sound_seek_to_pcm_frame(ma_sound *, ma_uint64) {}
inline void ma_sound_start(ma_sound *s) {
  s->playing = true;
  ++g_start_calls;
}
inline void ma_sound_set_volume(ma_sound *, float) {}
