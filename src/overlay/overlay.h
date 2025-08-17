#pragma once

#include "platform/window.hpp"
#include "app/config.h"
#include "glad/glad.h"

#include <condition_variable>
#include <filesystem>
#include <mutex>
#include <optional>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

namespace lizard::overlay {

struct Sprite {
  float u0;
  float v0;
  float u1;
  float v1;
};

struct Badge {
  float x;
  float y;
  float scale;
  float alpha;
  float time;
  float lifetime;
  int sprite;
};

class Overlay {
public:
  bool init(const app::Config &cfg, std::optional<std::filesystem::path> emoji_path = std::nullopt);
  void shutdown();
  void spawn_badge(int sprite, float x, float y);
  void run(std::stop_token st);
  void stop();

private:
  int select_sprite();
  void update(float dt);
  void render();

  platform::Window m_window{};
  std::vector<Badge> m_badges;
  std::vector<Sprite> m_sprites;
  std::unordered_map<std::string, int> m_sprite_lookup;
  std::vector<int> m_selector_indices;
  std::discrete_distribution<> m_selector;
  std::mt19937 m_rng{std::random_device{}()};
  GLuint m_texture = 0;
  GLuint m_vao = 0;
  GLuint m_vbo = 0;
  GLuint m_instance = 0;
  GLuint m_program = 0;
  bool m_running = false;
  std::condition_variable_any m_cv;
  std::mutex m_mutex;
  std::size_t m_frames = 0;
};

namespace testing {
void set_running(Overlay &o, bool r);
std::size_t frame_count(const Overlay &o);
} // namespace testing

} // namespace lizard::overlay
