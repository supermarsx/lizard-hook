#pragma once
#include <cstddef>
#include <cstdint>

struct drflac {
  std::uint64_t totalPCMFrameCount;
  std::uint32_t channels;
  std::uint32_t sampleRate;
};

inline drflac *drflac_open_file(const char *, void *) {
  static drflac d{1, 1, 44100};
  return &d;
}

inline drflac *drflac_open_memory(const unsigned char *, size_t, void *) {
  static drflac d{1, 1, 44100};
  return &d;
}

inline void drflac_read_pcm_frames_f32(drflac *, std::uint64_t, float *) {}
inline void drflac_close(drflac *) {}
