#include "platform/window.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include <climits>
#ifdef _WIN32
#include <windows.h>
#elif defined(__linux__)
#include <X11/Xlib.h>
#elif defined(__APPLE__)
#include <CoreGraphics/CoreGraphics.h>
#endif

#include "glad/glad.h"
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#ifdef __APPLE__
#include <objc/message.h>
#endif

#include "embedded.h"
#include <nlohmann/json.hpp>

#include "app/config.h"
#include <spdlog/spdlog.h>

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
  platform::WindowDesc desc{};
#ifdef _WIN32
  desc.x = GetSystemMetrics(SM_XVIRTUALSCREEN);
  desc.y = GetSystemMetrics(SM_YVIRTUALSCREEN);
  desc.width = static_cast<std::uint32_t>(GetSystemMetrics(SM_CXVIRTUALSCREEN));
  desc.height = static_cast<std::uint32_t>(GetSystemMetrics(SM_CYVIRTUALSCREEN));
#elif defined(__APPLE__)
  uint32_t count = 0;
  CGGetActiveDisplayList(0, nullptr, &count);
  std::vector<CGDirectDisplayID> displays(count);
  CGGetActiveDisplayList(count, displays.data(), &count);
  int32_t minX = INT_MAX, minY = INT_MAX;
  int32_t maxX = INT_MIN, maxY = INT_MIN;
  for (uint32_t i = 0; i < count; ++i) {
    CGRect r = CGDisplayBounds(displays[i]);
    if (r.origin.x < minX)
      minX = static_cast<int32_t>(r.origin.x);
    if (r.origin.y < minY)
      minY = static_cast<int32_t>(r.origin.y);
    if (r.origin.x + r.size.width > maxX)
      maxX = static_cast<int32_t>(r.origin.x + r.size.width);
    if (r.origin.y + r.size.height > maxY)
      maxY = static_cast<int32_t>(r.origin.y + r.size.height);
  }
  if (count > 0) {
    desc.x = minX;
    desc.y = minY;
    desc.width = static_cast<std::uint32_t>(maxX - minX);
    desc.height = static_cast<std::uint32_t>(maxY - minY);
  } else {
    desc.x = 0;
    desc.y = 0;
    desc.width = 800;
    desc.height = 600;
  }
#elif defined(__linux__)
  if (Display *dpy = XOpenDisplay(nullptr)) {
    int screen = DefaultScreen(dpy);
    desc.x = 0;
    desc.y = 0;
    desc.width = static_cast<std::uint32_t>(DisplayWidth(dpy, screen));
    desc.height = static_cast<std::uint32_t>(DisplayHeight(dpy, screen));
    XCloseDisplay(dpy);
  } else {
    desc.x = 0;
    desc.y = 0;
    desc.width = 800;
    desc.height = 600;
  }
#else
  desc.x = 0;
  desc.y = 0;
  desc.width = 800;
  desc.height = 600;
#endif
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
  std::ifstream atlas_file;
  std::istringstream atlas_default(R"({
  "sprites": {
    "🦎": { "u0": 0.0, "v0": 0.0, "u1": 0.5, "v1": 0.5 },
    "🐍": { "u0": 0.5, "v0": 0.0, "u1": 1.0, "v1": 0.5 },
    "🐢": { "u0": 0.0, "v0": 0.5, "u1": 0.5, "v1": 1.0 }
  }
})");
  std::istream *atlas = nullptr;
  if (emoji_path) {
    std::filesystem::path json_path = *emoji_path;
    json_path += ".json";
    if (std::filesystem::exists(json_path)) {
      atlas_file.open(json_path);
      if (atlas_file.is_open()) {
        atlas = &atlas_file;
      }
    }
    if (!atlas) {
      json_path = emoji_path->parent_path() / "emoji_atlas.json";
      if (std::filesystem::exists(json_path)) {
        atlas_file.open(json_path);
        if (atlas_file.is_open()) {
          atlas = &atlas_file;
        }
      }
    }
  }
  if (!atlas) {
    atlas = &atlas_default;
  }
  try {
    json j;
    *atlas >> j;
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
    // fall back to single sprite below
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

  auto compile_shader = [](GLuint id, const char *type) -> bool {
    glCompileShader(id);
    GLint status = GL_FALSE;
    glGetShaderiv(id, GL_COMPILE_STATUS, &status);
    if (status != GL_TRUE) {
      GLint logLen = 0;
      glGetShaderiv(id, GL_INFO_LOG_LENGTH, &logLen);
      std::string log(logLen, '\0');
      glGetShaderInfoLog(id, logLen, nullptr, log.data());
      spdlog::error("{} shader compilation failed: {}", type, log);
      glDeleteShader(id);
      return false;
    }
    return true;
  };

  if (!compile_shader(vsId, "Vertex")) {
    return false;
  }

  GLuint fsId = glCreateShader(GL_FRAGMENT_SHADER);
  glShaderSource(fsId, 1, &fs, nullptr);
  if (!compile_shader(fsId, "Fragment")) {
    glDeleteShader(vsId);
    return false;
  }

  m_program = glCreateProgram();
  glAttachShader(m_program, vsId);
  glAttachShader(m_program, fsId);

  auto link_program = [](GLuint prog) -> bool {
    glLinkProgram(prog);
    GLint status = GL_FALSE;
    glGetProgramiv(prog, GL_LINK_STATUS, &status);
    if (status != GL_TRUE) {
      GLint logLen = 0;
      glGetProgramiv(prog, GL_INFO_LOG_LENGTH, &logLen);
      std::string log(logLen, '\0');
      glGetProgramInfoLog(prog, logLen, nullptr, log.data());
      spdlog::error("Program link failed: {}", log);
      glDeleteProgram(prog);
      return false;
    }
    return true;
  };

  if (!link_program(m_program)) {
    glDeleteShader(vsId);
    glDeleteShader(fsId);
    m_program = 0;
    return false;
  }

  glDeleteShader(vsId);
  glDeleteShader(fsId);

  m_running = true;
  return true;
}

void Overlay::shutdown() {
  stop();
#ifdef _WIN32
  if (m_window.device && m_window.glContext) {
    wglMakeCurrent((HDC)m_window.device, (HGLRC)m_window.glContext);
  }
#elif defined(__linux__)
  if (m_window.native && m_window.glContext) {
    Display *dpy = glXGetCurrentDisplay();
    if (dpy) {
      glXMakeCurrent(dpy, (GLXDrawable)m_window.native, (GLXContext)m_window.glContext);
    }
  }
#elif defined(__APPLE__)
  if (m_window.glContext) {
    auto makeCurrent = reinterpret_cast<void (*)(id, SEL)>(objc_msgSend);
    makeCurrent((id)m_window.glContext, sel_getUid("makeCurrentContext"));
  }
#endif
  if (m_texture) {
    glDeleteTextures(1, &m_texture);
    m_texture = 0;
  }
  if (m_vbo) {
    glDeleteBuffers(1, &m_vbo);
    m_vbo = 0;
  }
  if (m_instance) {
    glDeleteBuffers(1, &m_instance);
    m_instance = 0;
  }
  if (m_vao) {
    glDeleteVertexArrays(1, &m_vao);
    m_vao = 0;
  }
  if (m_program) {
    glDeleteProgram(m_program);
    m_program = 0;
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
