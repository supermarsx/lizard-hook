#include "platform/window.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <random>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "glad/glad.h"
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include "embedded.h"
#include <nlohmann/json.hpp>

#include "app/config.h"

using json = nlohmann::json;

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
};

bool Overlay::init(const app::Config &cfg, std::optional<std::filesystem::path> emoji_path) {
  platform::WindowDesc desc{800, 600};
  m_window = platform::create_overlay_window(desc);
  if (!m_window.native) {
    return false;
  }

  // Load atlas
  int w, h, channels;
  unsigned char *pixels = nullptr;
  if (emoji_path && std::filesystem::exists(*emoji_path)) {
    pixels = stbi_load(emoji_path->string().c_str(), &w, &h, &channels, 4);
  } else {
    pixels = stbi_load_from_memory(lizard::assets::lizard_regular_png,
                                   lizard::assets::lizard_regular_png_len, &w, &h, &channels, 4);
  }
  if (!pixels) {
    return false;
  }

  // Pre-multiply RGB by alpha
  for (int i = 0; i < w * h; ++i) {
    unsigned char *p = pixels + i * 4;
    unsigned char a = p[3];
    p[0] = static_cast<unsigned char>(p[0] * a / 255);
    p[1] = static_cast<unsigned char>(p[1] * a / 255);
    p[2] = static_cast<unsigned char>(p[2] * a / 255);
  }
  glGenTextures(1, &m_texture);
  glBindTexture(GL_TEXTURE_2D, m_texture);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  stbi_image_free(pixels);

  // Load sprite UVs from atlas
  std::ifstream atlas_in(std::filesystem::path("assets") / "emoji_atlas.json");
  if (atlas_in.is_open()) {
    try {
      json j;
      atlas_in >> j;
      if (j.contains("sprites") && j["sprites"].is_object()) {
        for (const auto &[emoji, s] : j["sprites"].items()) {
          Sprite sp{};
          sp.u0 = s.value("u0", 0.0f);
          sp.v0 = s.value("v0", 0.0f);
          sp.u1 = s.value("u1", 1.0f);
          sp.v1 = s.value("v1", 1.0f);
          m_sprite_lookup[emoji] = static_cast<int>(m_sprites.size());
          m_sprites.push_back(sp);
        }
      }
    } catch (const std::exception &) {
      // fall back to default below
    }
  }
  if (m_sprites.empty()) {
    m_sprites.push_back({0.0f, 0.0f, 1.0f, 1.0f});
    m_sprite_lookup["🦎"] = 0;
  }

  // Build selector from config
  std::vector<double> weights;
  if (!cfg.emoji_weighted().empty()) {
    for (const auto &[emoji, weight] : cfg.emoji_weighted()) {
      auto it = m_sprite_lookup.find(emoji);
      if (it != m_sprite_lookup.end()) {
        m_selector_indices.push_back(it->second);
        weights.push_back(weight);
      }
    }
  } else {
    for (const auto &emoji : cfg.emoji()) {
      auto it = m_sprite_lookup.find(emoji);
      if (it != m_sprite_lookup.end()) {
        m_selector_indices.push_back(it->second);
        weights.push_back(1.0);
      }
    }
  }
  if (m_selector_indices.empty()) {
    for (int i = 0; i < static_cast<int>(m_sprites.size()); ++i) {
      m_selector_indices.push_back(i);
      weights.push_back(1.0);
    }
  }
  m_selector = std::discrete_distribution<>(weights.begin(), weights.end());

  if (!m_sprites.empty()) {
    spawn_badge(select_sprite(), 0.0f, 0.0f);
  }

  // Geometry
  const float verts[] = {-0.5f, -0.5f, 0.0f, 0.0f, 0.5f,  -0.5f, 1.0f, 0.0f,
                         0.5f,  0.5f,  1.0f, 1.0f, -0.5f, 0.5f,  0.0f, 1.0f};
  glGenVertexArrays(1, &m_vao);
  glBindVertexArray(m_vao);
  glGenBuffers(1, &m_vbo);
  glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
  glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);

  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void *)0);
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void *)(2 * sizeof(float)));

  glGenBuffers(1, &m_instance);
  glBindBuffer(GL_ARRAY_BUFFER, m_instance);
  glBufferData(GL_ARRAY_BUFFER, 1000 * sizeof(float) * 9, nullptr, GL_DYNAMIC_DRAW);
  glEnableVertexAttribArray(2);
  glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 9 * sizeof(float), (void *)0);
  glVertexAttribDivisor(2, 1);
  glEnableVertexAttribArray(3);
  glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, 9 * sizeof(float), (void *)(2 * sizeof(float)));
  glVertexAttribDivisor(3, 1);
  glEnableVertexAttribArray(4);
  glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, 9 * sizeof(float), (void *)(4 * sizeof(float)));
  glVertexAttribDivisor(4, 1);
  glEnableVertexAttribArray(5);
  glVertexAttribPointer(5, 2, GL_FLOAT, GL_FALSE, 9 * sizeof(float), (void *)(5 * sizeof(float)));
  glVertexAttribDivisor(5, 1);
  glEnableVertexAttribArray(6);
  glVertexAttribPointer(6, 2, GL_FLOAT, GL_FALSE, 9 * sizeof(float), (void *)(7 * sizeof(float)));
  glVertexAttribDivisor(6, 1);

  const char *vs = R"GLSL(
      #version 330 core
      layout(location=0) in vec2 inPos;
      layout(location=1) in vec2 inUV;
      layout(location=2) in vec2 iPos;
      layout(location=3) in vec2 iScale;
      layout(location=4) in float iAlpha;
      layout(location=5) in vec2 iUV0;
      layout(location=6) in vec2 iUV1;
      out vec2 uv;
      out float alpha;
      void main(){
        vec2 pos = inPos * iScale + iPos;
        gl_Position = vec4(pos,0.0,1.0);
        uv = mix(iUV0, iUV1, inUV);
        alpha = iAlpha;
      })GLSL";
  const char *fs = R"GLSL(
      #version 330 core
      in vec2 uv;
      in float alpha;
      out vec4 color;
      uniform sampler2D uTex;
      void main(){
        color = texture(uTex, uv) * alpha;
      })GLSL";
  GLuint vsId = glCreateShader(GL_VERTEX_SHADER);
  glShaderSource(vsId, 1, &vs, nullptr);
  glCompileShader(vsId);
  GLuint fsId = glCreateShader(GL_FRAGMENT_SHADER);
  glShaderSource(fsId, 1, &fs, nullptr);
  glCompileShader(fsId);
  m_program = glCreateProgram();
  glAttachShader(m_program, vsId);
  glAttachShader(m_program, fsId);
  glLinkProgram(m_program);
  glDeleteShader(vsId);
  glDeleteShader(fsId);

  m_running = true;
  return true;
}

void Overlay::shutdown() {
  stop();
  if (m_texture) {
    glDeleteTextures(1, &m_texture);
    m_texture = 0;
  }
  platform::destroy_window(m_window);
}

int Overlay::select_sprite() {
  if (m_selector_indices.empty()) {
    return 0;
  }
  auto idx = m_selector(m_rng);
  return m_selector_indices[idx];
}

void Overlay::spawn_badge(int sprite, float x, float y) {
  Badge b{};
  b.x = x;
  b.y = y;
  b.scale = 0.1f;
  b.alpha = 1.0f;
  b.time = 0.0f;
  b.lifetime = 1.0f;
  b.sprite = sprite;
  m_badges.push_back(b);
}

void Overlay::stop() { m_running = false; }

void Overlay::update(float dt) {
  for (auto &b : m_badges) {
    b.time += dt;
    float t = b.time / b.lifetime;
    b.alpha = 1.0f - t;
    b.scale = 0.1f + 0.1f * t;
  }
  m_badges.erase(std::remove_if(m_badges.begin(), m_badges.end(),
                                [](const Badge &b) { return b.time >= b.lifetime; }),
                 m_badges.end());
}

void Overlay::render() {
  glClearColor(0, 0, 0, 0);
  glClear(GL_COLOR_BUFFER_BIT);

  glEnable(GL_BLEND);
  glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

  std::vector<float> data;
  data.reserve(m_badges.size() * 9);
  for (const auto &b : m_badges) {
    const Sprite &s = m_sprites[b.sprite];
    data.push_back(b.x);
    data.push_back(b.y);
    data.push_back(b.scale);
    data.push_back(b.scale);
    data.push_back(b.alpha);
    data.push_back(s.u0);
    data.push_back(s.v0);
    data.push_back(s.u1);
    data.push_back(s.v1);
  }
  glBindBuffer(GL_ARRAY_BUFFER, m_instance);
  glBufferSubData(GL_ARRAY_BUFFER, 0, data.size() * sizeof(float), data.data());

  glUseProgram(m_program);
  glBindVertexArray(m_vao);
  glBindTexture(GL_TEXTURE_2D, m_texture);
  glDrawArraysInstanced(GL_TRIANGLE_FAN, 0, 4, static_cast<GLsizei>(m_badges.size()));
  platform::poll_events(m_window);
}

void Overlay::run(std::stop_token st) {
  using clock = std::chrono::steady_clock;
  const auto frame = std::chrono::milliseconds(16);
  auto last = clock::now();
  while (m_running && !st.stop_requested()) {
    auto now = clock::now();
    float dt = std::chrono::duration<float>(now - last).count();
    last = now;
    update(dt);
    render();
    auto end = clock::now();
    auto spend = end - now;
    if (spend < frame) {
      std::this_thread::sleep_for(frame - spend);
    }
  }
  stop();
}

} // namespace lizard::overlay
