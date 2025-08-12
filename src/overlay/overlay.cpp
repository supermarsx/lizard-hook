#include "platform/window.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <thread>
#include <vector>

#include "glad/glad.h"
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

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
  bool init();
  void shutdown();
  void spawn_badge(int sprite, float x, float y);
  void run();

private:
  void update(float dt);
  void render();

  platform::Window m_window{};
  std::vector<Badge> m_badges;
  std::vector<Sprite> m_sprites;
  GLuint m_texture = 0;
  GLuint m_vao = 0;
  GLuint m_vbo = 0;
  GLuint m_instance = 0;
  GLuint m_program = 0;
  bool m_running = false;
};

// Embedded 1x1 PNG placeholder
static const unsigned char atlas_png[] = {
    0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d,
    0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01,
    0x08, 0x04, 0x00, 0x00, 0x00, 0xb5, 0x1c, 0x0c, 0x02, 0x00, 0x00, 0x00,
    0x0b, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9c, 0x63, 0x00, 0x01, 0x00, 0x00,
    0x05, 0x00, 0x01, 0x0d, 0x0a, 0x2d, 0xb4, 0x00, 0x00, 0x00, 0x00, 0x49,
    0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82};

bool Overlay::init() {
  platform::WindowDesc desc{800, 600};
  m_window = platform::create_overlay_window(desc);
  if (!m_window.native) {
    return false;
  }

  // Load atlas
  int w, h, channels;
  unsigned char *pixels =
      stbi_load_from_memory(atlas_png, sizeof(atlas_png), &w, &h, &channels, 4);
  if (!pixels) {
    return false;
  }
  glGenTextures(1, &m_texture);
  glBindTexture(GL_TEXTURE_2D, m_texture);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE,
               pixels);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  stbi_image_free(pixels);

  // single sprite covering full texture
  m_sprites.push_back({0.0f, 0.0f, 1.0f, 1.0f});

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
  glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                        (void *)(2 * sizeof(float)));

  glGenBuffers(1, &m_instance);
  glBindBuffer(GL_ARRAY_BUFFER, m_instance);
  glBufferData(GL_ARRAY_BUFFER, 1000 * sizeof(float) * 6, nullptr,
               GL_DYNAMIC_DRAW);
  glEnableVertexAttribArray(2);
  glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void *)0);
  glVertexAttribDivisor(2, 1);
  glEnableVertexAttribArray(3);
  glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, 6 * sizeof(float),
                        (void *)(2 * sizeof(float)));
  glVertexAttribDivisor(3, 1);
  glEnableVertexAttribArray(4);
  glVertexAttribPointer(4, 2, GL_FLOAT, GL_FALSE, 6 * sizeof(float),
                        (void *)(4 * sizeof(float)));
  glVertexAttribDivisor(4, 1);

  const char *vs = R"GLSL(
      #version 330 core
      layout(location=0) in vec2 inPos;
      layout(location=1) in vec2 inUV;
      layout(location=2) in vec2 iPos;
      layout(location=3) in vec2 iScale;
      layout(location=4) in vec2 iUV;
      out vec2 uv;
      void main(){
        vec2 pos = inPos * iScale + iPos;
        gl_Position = vec4(pos,0.0,1.0);
        uv = inUV * iUV;
      })GLSL";
  const char *fs = R"GLSL(
      #version 330 core
      in vec2 uv;
      out vec4 color;
      uniform sampler2D uTex;
      uniform float uAlpha;
      void main(){
        color = texture(uTex, uv) * uAlpha;
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
  if (m_texture) {
    glDeleteTextures(1, &m_texture);
    m_texture = 0;
  }
  platform::destroy_window(m_window);
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

void Overlay::update(float dt) {
  for (auto &b : m_badges) {
    b.time += dt;
    float t = b.time / b.lifetime;
    b.alpha = 1.0f - t;
    b.scale = 0.1f + 0.1f * t;
  }
  m_badges.erase(
      std::remove_if(m_badges.begin(), m_badges.end(),
                     [](const Badge &b) { return b.time >= b.lifetime; }),
      m_badges.end());
}

void Overlay::render() {
  glClearColor(0, 0, 0, 0);
  glClear(GL_COLOR_BUFFER_BIT);

  std::vector<float> data;
  for (const auto &b : m_badges) {
    const Sprite &s = m_sprites[b.sprite];
    data.push_back(b.x);
    data.push_back(b.y);
    data.push_back(b.scale);
    data.push_back(b.scale);
    data.push_back(s.u1 - s.u0);
    data.push_back(s.v1 - s.v0);
  }
  glBindBuffer(GL_ARRAY_BUFFER, m_instance);
  glBufferSubData(GL_ARRAY_BUFFER, 0, data.size() * sizeof(float), data.data());

  glUseProgram(m_program);
  glBindVertexArray(m_vao);
  glBindTexture(GL_TEXTURE_2D, m_texture);
  for (const auto &b : m_badges) {
    glUniform1f(glGetUniformLocation(m_program, "uAlpha"), b.alpha);
    glDrawArraysInstanced(GL_TRIANGLE_FAN, 0, 4, 1);
  }
  platform::poll_events(m_window);
}

void Overlay::run() {
  using clock = std::chrono::steady_clock;
  const auto frame = std::chrono::milliseconds(16);
  auto last = clock::now();
  while (m_running) {
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
}

} // namespace lizard::overlay
