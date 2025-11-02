// glad must be included before any platform window headers to ensure modern
// OpenGL symbols like glBindBuffer are declared. Even in test builds we include
// it so GL types are available.
#ifndef LIZARD_TEST
#include "glad/glad.h"
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#else
extern "C" {
unsigned char *stbi_load(const char *, int *, int *, int *, int);
unsigned char *stbi_load_from_memory(const unsigned char *, int, int *, int *, int *, int);
const char *stbi_failure_reason(void);
void stbi_image_free(void *);
}
#endif

#include "platform/window.hpp"
#include "platform/tray.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <random>
#include <cmath>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>
#include <deque>
#include <mutex>
#include <atomic>
#include <queue>

#include <climits>
#ifdef _WIN32
#include <windows.h>
#elif defined(__linux__)
#include <X11/Xlib.h>
#include <X11/extensions/Xrandr.h>
#elif defined(__APPLE__)
#include <CoreGraphics/CoreGraphics.h>
#include <CoreFoundation/CoreFoundation.h>
#endif

#ifndef LIZARD_TEST
#include "embedded.h"
#endif
#include <nlohmann/json.hpp>

#include "app/config.h"
#include "overlay/gl_raii.h"
#include <spdlog/spdlog.h>

#ifdef LIZARD_TEST
extern bool g_overlay_log_called;
#endif

using json = nlohmann::json;

struct OverlayTestAccess;

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
  float vx;
  float vy;
  float phase;
  float scale;
  float alpha;
  float rotation;
  float time;
  float lifetime;
  float fade_in;
  float fade_out;
  int sprite;
};

struct MonitorBounds {
  float left;
  float top;
  float right;
  float bottom;
};

#ifdef LIZARD_TEST
namespace test {
  static std::optional<std::vector<MonitorBounds>> g_monitors_override;
  static std::optional<std::size_t> g_foreground_monitor_override;

  void set_monitors(std::vector<MonitorBounds> monitors) {
    g_monitors_override = std::move(monitors);
  }

  void clear_monitors() { g_monitors_override.reset(); }

  void set_foreground_monitor(std::optional<std::size_t> index) {
    g_foreground_monitor_override = index;
  }

  void clear_foreground_monitor() { g_foreground_monitor_override.reset(); }

  void reset_spawn_overrides() {
    g_monitors_override.reset();
    g_foreground_monitor_override.reset();
  }
} // namespace test
#endif

namespace {

std::vector<MonitorBounds> query_system_monitors() {
  std::vector<MonitorBounds> monitors;
#ifdef _WIN32
  EnumDisplayMonitors(
      nullptr, nullptr,
      [](HMONITOR, HDC, LPRECT rect, LPARAM param) -> BOOL {
        auto *out = reinterpret_cast<std::vector<MonitorBounds> *>(param);
        if (rect) {
          out->push_back(MonitorBounds{static_cast<float>(rect->left), static_cast<float>(rect->top),
                                       static_cast<float>(rect->right),
                                       static_cast<float>(rect->bottom)});
        }
        return TRUE;
      },
      reinterpret_cast<LPARAM>(&monitors));
#elif defined(__APPLE__)
  uint32_t count = 0;
  if (CGGetActiveDisplayList(0, nullptr, &count) == kCGErrorSuccess && count > 0) {
    std::vector<CGDirectDisplayID> displays(count);
    if (CGGetActiveDisplayList(count, displays.data(), &count) == kCGErrorSuccess) {
      for (uint32_t i = 0; i < count; ++i) {
        CGRect bounds = CGDisplayBounds(displays[i]);
        monitors.push_back(MonitorBounds{static_cast<float>(bounds.origin.x),
                                         static_cast<float>(bounds.origin.y),
                                         static_cast<float>(bounds.origin.x + bounds.size.width),
                                         static_cast<float>(bounds.origin.y + bounds.size.height)});
      }
    }
  }
#elif defined(__linux__)
  platform::init_xlib_threads();
  if (Display *dpy = XOpenDisplay(nullptr)) {
    ::Window root = DefaultRootWindow(dpy);
    int nmon = 0;
    if (XRRMonitorInfo *info = XRRGetMonitors(dpy, root, True, &nmon)) {
      for (int i = 0; i < nmon; ++i) {
        monitors.push_back(MonitorBounds{static_cast<float>(info[i].x), static_cast<float>(info[i].y),
                                         static_cast<float>(info[i].x + info[i].width),
                                         static_cast<float>(info[i].y + info[i].height)});
      }
      XRRFreeMonitors(info);
    } else {
      int screen = DefaultScreen(dpy);
      monitors.push_back(MonitorBounds{0.0f, 0.0f, static_cast<float>(DisplayWidth(dpy, screen)),
                                       static_cast<float>(DisplayHeight(dpy, screen))});
    }
    XCloseDisplay(dpy);
  }
#endif
  return monitors;
}

std::optional<MonitorBounds> query_system_foreground_monitor() {
#ifdef _WIN32
  HWND foreground = GetForegroundWindow();
  if (!foreground) {
    return std::nullopt;
  }
  HMONITOR monitor = MonitorFromWindow(foreground, MONITOR_DEFAULTTONULL);
  if (!monitor) {
    monitor = MonitorFromWindow(foreground, MONITOR_DEFAULTTOPRIMARY);
  }
  if (!monitor) {
    return std::nullopt;
  }
  MONITORINFO info{};
  info.cbSize = sizeof(info);
  if (!GetMonitorInfo(monitor, &info)) {
    return std::nullopt;
  }
  RECT rc = info.rcMonitor;
  return MonitorBounds{static_cast<float>(rc.left), static_cast<float>(rc.top),
                       static_cast<float>(rc.right), static_cast<float>(rc.bottom)};
#elif defined(__APPLE__)
  CFArrayRef list = CGWindowListCopyWindowInfo(
      kCGWindowListOptionOnScreenOnly | kCGWindowListExcludeDesktopElements, kCGNullWindowID);
  if (!list) {
    return std::nullopt;
  }
  CGRect bounds{};
  bool found = false;
  CFIndex count = CFArrayGetCount(list);
  for (CFIndex i = 0; i < count && !found; ++i) {
    auto dict = static_cast<CFDictionaryRef>(CFArrayGetValueAtIndex(list, i));
    if (!dict) {
      continue;
    }
    CFNumberRef layer = static_cast<CFNumberRef>(CFDictionaryGetValue(dict, kCGWindowLayer));
    int layer_value = 0;
    if (!layer || !CFNumberGetValue(layer, kCFNumberIntType, &layer_value) || layer_value != 0) {
      continue;
    }
    auto bounds_dict = static_cast<CFDictionaryRef>(CFDictionaryGetValue(dict, kCGWindowBounds));
    if (bounds_dict && CGRectMakeWithDictionaryRepresentation(bounds_dict, &bounds)) {
      found = true;
    }
  }
  CFRelease(list);
  if (!found) {
    return std::nullopt;
  }
  auto monitors = query_system_monitors();
  double center_x = bounds.origin.x + bounds.size.width * 0.5;
  double center_y = bounds.origin.y + bounds.size.height * 0.5;
  for (const auto &m : monitors) {
    if (center_x >= m.left && center_x <= m.right && center_y >= m.top && center_y <= m.bottom) {
      return m;
    }
  }
  if (!monitors.empty()) {
    return monitors.front();
  }
  return std::nullopt;
#elif defined(__linux__)
  platform::init_xlib_threads();
  Display *dpy = XOpenDisplay(nullptr);
  if (!dpy) {
    return std::nullopt;
  }
  ::Window root = DefaultRootWindow(dpy);
  Atom active_atom = XInternAtom(dpy, "_NET_ACTIVE_WINDOW", False);
  if (active_atom == None) {
    XCloseDisplay(dpy);
    return std::nullopt;
  }
  Atom actual_type = None;
  int actual_format = 0;
  unsigned long nitems = 0;
  unsigned long bytes = 0;
  unsigned char *data = nullptr;
  if (XGetWindowProperty(dpy, root, active_atom, 0, ~0L, False, XA_WINDOW, &actual_type, &actual_format,
                         &nitems, &bytes, &data) != Success || !data || nitems == 0) {
    if (data) {
      XFree(data);
    }
    XCloseDisplay(dpy);
    return std::nullopt;
  }
  ::Window active = reinterpret_cast<::Window *>(data)[0];
  XFree(data);
  if (!active) {
    XCloseDisplay(dpy);
    return std::nullopt;
  }
  XWindowAttributes attrs{};
  if (!XGetWindowAttributes(dpy, active, &attrs)) {
    XCloseDisplay(dpy);
    return std::nullopt;
  }
  int abs_x = attrs.x;
  int abs_y = attrs.y;
  ::Window child;
  if (XTranslateCoordinates(dpy, active, root, 0, 0, &abs_x, &abs_y, &child) == False) {
    abs_x = attrs.x;
    abs_y = attrs.y;
  }
  double center_x = abs_x + attrs.width * 0.5;
  double center_y = abs_y + attrs.height * 0.5;
  XCloseDisplay(dpy);
  auto monitors = query_system_monitors();
  for (const auto &m : monitors) {
    if (center_x >= m.left && center_x <= m.right && center_y >= m.top && center_y <= m.bottom) {
      return m;
    }
  }
  if (!monitors.empty()) {
    return monitors.front();
  }
  return std::nullopt;
#else
  return std::nullopt;
#endif
}

} // namespace

static std::vector<MonitorBounds> active_monitors() {
#ifdef LIZARD_TEST
  if (test::g_monitors_override) {
    return *test::g_monitors_override;
  }
#endif
  return query_system_monitors();
}

static std::optional<MonitorBounds> foreground_monitor(const std::vector<MonitorBounds> &monitors) {
#ifdef LIZARD_TEST
  if (test::g_foreground_monitor_override) {
    std::size_t idx = *test::g_foreground_monitor_override;
    if (idx < monitors.size()) {
      return monitors[idx];
    }
    return std::nullopt;
  }
#endif
  auto monitor = query_system_foreground_monitor();
  if (monitor) {
    return monitor;
  }
  return std::nullopt;
}

enum class BadgeSpawnStrategy {
  RandomScreen,
  NearCaret,
};

class Overlay {
public:
  bool init(const app::Config &cfg, std::optional<std::filesystem::path> emoji_path = std::nullopt);
  void shutdown();
  void spawn_badge(int sprite, float x, float y);
  void spawn_badge(float x, float y);
  void enqueue_spawn(int sprite, float x, float y);
  void enqueue_spawn(float x, float y);
  void run(std::stop_token st);
  void stop();
  void refresh_from_config(const app::Config &cfg);
  void set_paused(bool v) { m_paused = v; }
  void set_fps_mode(platform::FpsMode mode) {
    m_fps_mode = mode;
    update_frame_interval();
  }
  void set_fps_fixed(int fps) {
    m_fps_fixed = fps;
    update_frame_interval();
  }

private:
  friend struct ::OverlayTestAccess;
  int select_sprite();
  int select_sprite_locked();
  void update(float dt);
  void render();
  void update_frame_interval();
  void apply_pending_config();
  void process_spawn_queue();
  struct AtlasData {
    std::vector<Sprite> sprites;
    std::unordered_map<std::string, int> lookup;
    std::optional<std::filesystem::path> normalized_path;
  };
  std::optional<AtlasData> load_atlas_from_path(const std::optional<std::filesystem::path> &emoji_path);
  void build_selector(const std::vector<std::string> &emoji,
                      const std::unordered_map<std::string, double> &emoji_weighted);
  void spawn_badge_locked(int sprite, float x, float y);
  static std::optional<std::filesystem::path>
  normalize_path(const std::optional<std::filesystem::path> &path);

  struct SpawnRequest {
    std::optional<int> sprite;
    float x;
    float y;
  };

  struct PendingConfig {
    std::string spawn_strategy;
    int badge_min_px = 60;
    int badge_max_px = 108;
    int badges_per_second_max = 12;
    std::string fps_mode;
    int fps_fixed = 60;
    std::optional<std::filesystem::path> emoji_atlas;
    std::vector<std::string> emoji;
    std::unordered_map<std::string, double> emoji_weighted;
  };

  platform::Window m_window{};
  std::vector<Badge> m_badges;
  std::size_t m_badge_capacity = 0;
  bool m_badge_suppressed = false;
  int m_badge_min_px = 60;
  int m_badge_max_px = 108;
  float m_view_width = 1.0f;
  float m_view_height = 1.0f;
  float m_virtual_origin_x = 0.0f;
  float m_virtual_origin_y = 0.0f;
  std::vector<float> m_instanceData;
  std::vector<Sprite> m_sprites;
  std::unordered_map<std::string, int> m_sprite_lookup;
  std::vector<int> m_selector_indices;
  std::discrete_distribution<> m_selector;
  std::mt19937 m_rng{std::random_device{}()};
  std::deque<std::chrono::steady_clock::time_point> m_spawn_times;
  int m_badges_per_second_max = 12;
  gl::Texture m_texture;
  gl::VertexArray m_vao;
  gl::Buffer m_vbo;
  gl::Buffer m_instance;
  gl::Program m_program;
  bool m_running = false;
  BadgeSpawnStrategy m_spawn_strategy = BadgeSpawnStrategy::RandomScreen;
  std::atomic<bool> m_paused{false};
  platform::FpsMode m_fps_mode = platform::FpsMode::Auto;
  int m_fps_fixed = 60;
  std::atomic<std::int64_t> m_frame_interval_us{1000000 / 60};
  std::optional<std::filesystem::path> m_current_emoji_path;
  std::mutex m_pending_mutex;
  std::optional<PendingConfig> m_pending_config;
  std::atomic<bool> m_has_pending_config{false};
  std::mutex m_spawn_config_mutex;
  std::mutex m_spawn_queue_mutex;
  std::queue<SpawnRequest> m_spawn_queue;
};

void Overlay::update_frame_interval() {
  int refresh = 60;
  if (m_fps_mode == platform::FpsMode::Fixed && m_fps_fixed > 0) {
    refresh = m_fps_fixed;
  } else {
#ifdef _WIN32
    DEVMODE dm{};
    dm.dmSize = sizeof(dm);
    if (EnumDisplaySettingsEx(nullptr, ENUM_CURRENT_SETTINGS, &dm, 0) &&
        dm.dmDisplayFrequency > 0) {
      refresh = dm.dmDisplayFrequency;
    }
#elif defined(__APPLE__)
    auto mode = CGDisplayCopyDisplayMode(CGMainDisplayID());
    if (mode) {
      double rate = CGDisplayModeGetRefreshRate(mode);
      if (rate > 0.0) {
        refresh = static_cast<int>(rate + 0.5);
      }
      CGDisplayModeRelease(mode);
    }
#elif defined(__linux__)
    platform::init_xlib_threads();
    Display *dpy = XOpenDisplay(nullptr);
    if (dpy) {
      Window root = DefaultRootWindow(dpy);
      XRRScreenConfiguration *conf = XRRGetScreenInfo(dpy, root);
      if (conf) {
        short rate = XRRConfigCurrentRate(conf);
        if (rate > 0) {
          refresh = rate;
        }
        XRRFreeScreenConfigInfo(conf);
      }
      XCloseDisplay(dpy);
    }
#endif
  }
  if (refresh <= 0) {
    refresh = 60;
  }
  m_frame_interval_us = 1000000 / refresh;
}

bool Overlay::init(const app::Config &cfg, std::optional<std::filesystem::path> emoji_path) {
  auto strategy = cfg.badge_spawn_strategy();
  if (strategy == "near_caret") {
    m_spawn_strategy = BadgeSpawnStrategy::NearCaret;
  } else {
    m_spawn_strategy = BadgeSpawnStrategy::RandomScreen;
  }
  if (cfg.fps_mode() == "fixed") {
    m_fps_mode = platform::FpsMode::Fixed;
    m_fps_fixed = cfg.fps_fixed();
  } else {
    m_fps_mode = platform::FpsMode::Auto;
  }
  m_badge_min_px = cfg.badge_min_px();
  m_badge_max_px = cfg.badge_max_px();
  m_badges_per_second_max = cfg.badges_per_second_max();
  update_frame_interval();

  auto emoji = cfg.emoji();
  auto emoji_weighted = cfg.emoji_weighted();
  auto normalized_path = normalize_path(emoji_path);
  std::optional<AtlasData> atlas;
#ifdef LIZARD_TEST
  atlas = load_atlas_from_path(normalized_path);
  if (!atlas) {
    return false;
  }
#else
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
  platform::init_xlib_threads();
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
  m_view_width = static_cast<float>(desc.width);
  m_view_height = static_cast<float>(desc.height);
  m_virtual_origin_x = static_cast<float>(desc.x);
  m_virtual_origin_y = static_cast<float>(desc.y);
  m_window = platform::create_overlay_window(desc);
  if (!m_window.native) {
    return false;
  }

  atlas = load_atlas_from_path(normalized_path);
  if (!atlas) {
    return false;
  }
#endif

  {
    std::lock_guard<std::mutex> lock(m_spawn_config_mutex);
    m_sprite_lookup = std::move(atlas->lookup);
    m_sprites = std::move(atlas->sprites);
    m_current_emoji_path = std::move(atlas->normalized_path);
    build_selector(emoji, emoji_weighted);
  }

  m_badge_capacity = 150;
  m_badges.reserve(m_badge_capacity);
  m_instanceData.reserve(m_badge_capacity * 10);

  if (!m_sprites.empty()) {
    spawn_badge(0.0f, 0.0f);
  }

#ifndef LIZARD_TEST
  // Geometry
  const float verts[] = {-0.5f, -0.5f, 0.0f, 0.0f, 0.5f,  -0.5f, 1.0f, 0.0f,
                         0.5f,  0.5f,  1.0f, 1.0f, -0.5f, 0.5f,  0.0f, 1.0f};
  m_vao.create();
  glBindVertexArray(m_vao.id);
  m_vbo.create();
  glBindBuffer(GL_ARRAY_BUFFER, m_vbo.id);
  glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);

  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void *)0);
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void *)(2 * sizeof(float)));

  m_instance.create();
  glBindBuffer(GL_ARRAY_BUFFER, m_instance.id);
  glBufferData(GL_ARRAY_BUFFER, 1000 * sizeof(float) * 10, nullptr, GL_DYNAMIC_DRAW);
  glEnableVertexAttribArray(2);
  glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 10 * sizeof(float), (void *)0);
  glVertexAttribDivisor(2, 1);
  glEnableVertexAttribArray(3);
  glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, 10 * sizeof(float), (void *)(2 * sizeof(float)));
  glVertexAttribDivisor(3, 1);
  glEnableVertexAttribArray(4);
  glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, 10 * sizeof(float), (void *)(4 * sizeof(float)));
  glVertexAttribDivisor(4, 1);
  glEnableVertexAttribArray(5);
  glVertexAttribPointer(5, 1, GL_FLOAT, GL_FALSE, 10 * sizeof(float), (void *)(5 * sizeof(float)));
  glVertexAttribDivisor(5, 1);
  glEnableVertexAttribArray(6);
  glVertexAttribPointer(6, 2, GL_FLOAT, GL_FALSE, 10 * sizeof(float), (void *)(6 * sizeof(float)));
  glVertexAttribDivisor(6, 1);
  glEnableVertexAttribArray(7);
  glVertexAttribPointer(7, 2, GL_FLOAT, GL_FALSE, 10 * sizeof(float), (void *)(8 * sizeof(float)));
  glVertexAttribDivisor(7, 1);

  const char *vs = R"GLSL(
      #version 330 core
      layout(location=0) in vec2 inPos;
      layout(location=1) in vec2 inUV;
      layout(location=2) in vec2 iPos;
      layout(location=3) in vec2 iScale;
      layout(location=4) in float iRot;
      layout(location=5) in float iAlpha;
      layout(location=6) in vec2 iUV0;
      layout(location=7) in vec2 iUV1;
      out vec2 uv;
      out float alpha;
      void main(){
        vec2 pos = inPos * iScale;
        float c = cos(iRot);
        float s = sin(iRot);
        pos = vec2(pos.x * c - pos.y * s, pos.x * s + pos.y * c) + iPos;
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

  m_program.create();
  glAttachShader(m_program.id, vsId);
  glAttachShader(m_program.id, fsId);

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

  if (!link_program(m_program.id)) {
    glDeleteShader(vsId);
    glDeleteShader(fsId);
    m_program.reset();
    return false;
  }

  glDeleteShader(vsId);
  glDeleteShader(fsId);

  platform::clear_current_context(m_window);

#endif
  m_running = true;
  return true;
}

std::optional<std::filesystem::path>
Overlay::normalize_path(const std::optional<std::filesystem::path> &path) {
  if (!path || path->empty()) {
    return std::nullopt;
  }
  return path->lexically_normal();
}

std::optional<Overlay::AtlasData>
Overlay::load_atlas_from_path(const std::optional<std::filesystem::path> &emoji_path) {
  auto normalized = normalize_path(emoji_path);
  std::unordered_map<std::string, int> lookup;
  std::vector<Sprite> sprites;

#ifdef LIZARD_TEST
  if (normalized && normalized->extension() == ".png") {
    int w = 0;
    int h = 0;
    int channels = 0;
    unsigned char *pixels = stbi_load(normalized->string().c_str(), &w, &h, &channels, 4);
    if (!pixels) {
      g_overlay_log_called = true;
      spdlog::error("Failed to load emoji atlas {}: {}", normalized->string(),
                    stbi_failure_reason());
      return std::nullopt;
    }
    stbi_image_free(pixels);
  }
#else
  int w = 0;
  int h = 0;
  int channels = 0;
  unsigned char *pixels = nullptr;
  if (normalized) {
    pixels = stbi_load(normalized->string().c_str(), &w, &h, &channels, 4);
  } else {
    pixels = stbi_load_from_memory(lizard::assets::lizard_regular_png,
                                   lizard::assets::lizard_regular_png_len, &w, &h, &channels, 4);
  }
  if (!pixels) {
    if (normalized) {
      spdlog::error("Failed to load emoji atlas {}: {}", normalized->string(),
                    stbi_failure_reason());
    } else {
      spdlog::error("Failed to load embedded emoji atlas: {}", stbi_failure_reason());
    }
    return std::nullopt;
  }

  for (int i = 0; i < w * h; ++i) {
    unsigned char *p = pixels + i * 4;
    unsigned char a = p[3];
    p[0] = static_cast<unsigned char>(p[0] * a / 255);
    p[1] = static_cast<unsigned char>(p[1] * a / 255);
    p[2] = static_cast<unsigned char>(p[2] * a / 255);
  }
  if (!m_texture.id) {
    m_texture.create();
  }
  glBindTexture(GL_TEXTURE_2D, m_texture.id);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  stbi_image_free(pixels);
#endif

  std::ifstream atlas_file;
  std::istringstream atlas_default(R"({
  "sprites": {
    "🦎": { "u0": 0.0, "v0": 0.0, "u1": 0.5, "v1": 0.5 },
    "🐍": { "u0": 0.5, "v0": 0.0, "u1": 1.0, "v1": 0.5 },
    "🐢": { "u0": 0.0, "v0": 0.5, "u1": 0.5, "v1": 1.0 }
  }
})");
  std::istream *atlas = nullptr;
  if (normalized) {
    if (normalized->extension() == ".json") {
      atlas_file.open(*normalized);
      if (atlas_file.is_open()) {
        atlas = &atlas_file;
      }
    } else {
      std::filesystem::path json_path = *normalized;
      json_path += ".json";
      if (std::filesystem::exists(json_path)) {
        atlas_file.open(json_path);
        if (atlas_file.is_open()) {
          atlas = &atlas_file;
        }
      }
      if (!atlas) {
        json_path = normalized->parent_path() / "emoji_atlas.json";
        if (std::filesystem::exists(json_path)) {
          atlas_file.open(json_path);
          if (atlas_file.is_open()) {
            atlas = &atlas_file;
          }
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
        lookup[emoji] = static_cast<int>(sprites.size());
        sprites.push_back(sp);
      }
    }
  } catch (const std::exception &e) {
    spdlog::error("Failed to parse emoji atlas: {}", e.what());
  }
  if (sprites.empty()) {
    sprites.push_back({0.0f, 0.0f, 1.0f, 1.0f});
    lookup["🦎"] = 0;
  }

  AtlasData data;
  data.sprites = std::move(sprites);
  data.lookup = std::move(lookup);
  data.normalized_path = normalized;
  return data;
}

void Overlay::build_selector(const std::vector<std::string> &emoji,
                             const std::unordered_map<std::string, double> &emoji_weighted) {
  m_selector_indices.clear();
  std::vector<double> weights;

  if (!emoji_weighted.empty()) {
    for (const auto &[symbol, weight] : emoji_weighted) {
      auto it = m_sprite_lookup.find(symbol);
      if (it != m_sprite_lookup.end()) {
        m_selector_indices.push_back(it->second);
        weights.push_back(weight);
      }
    }
  } else if (!emoji.empty()) {
    for (const auto &symbol : emoji) {
      auto it = m_sprite_lookup.find(symbol);
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

  if (weights.empty()) {
    weights.push_back(1.0);
    if (m_selector_indices.empty()) {
      m_selector_indices.push_back(0);
    }
  }

  m_selector = std::discrete_distribution<>(weights.begin(), weights.end());
}

void Overlay::refresh_from_config(const app::Config &cfg) {
  PendingConfig pending;
  pending.spawn_strategy = cfg.badge_spawn_strategy();
  pending.badge_min_px = cfg.badge_min_px();
  pending.badge_max_px = cfg.badge_max_px();
  pending.badges_per_second_max = cfg.badges_per_second_max();
  pending.fps_mode = cfg.fps_mode();
  pending.fps_fixed = cfg.fps_fixed();
  pending.emoji_atlas = normalize_path(cfg.emoji_atlas());
  pending.emoji = cfg.emoji();
  pending.emoji_weighted = cfg.emoji_weighted();

  {
    std::lock_guard<std::mutex> lock(m_pending_mutex);
    m_pending_config = std::move(pending);
  }
  m_has_pending_config.store(true, std::memory_order_release);
}

void Overlay::apply_pending_config() {
  PendingConfig pending;
  {
    std::lock_guard<std::mutex> lock(m_pending_mutex);
    if (!m_pending_config) {
      return;
    }
    pending = *m_pending_config;
    m_pending_config.reset();
  }

  auto normalized = normalize_path(pending.emoji_atlas);
  bool atlas_changed = normalized != m_current_emoji_path;
  std::optional<AtlasData> atlas;
  if (atlas_changed) {
    atlas = load_atlas_from_path(normalized);
    if (!atlas) {
      normalized = m_current_emoji_path;
      atlas.reset();
    }
  }

  {
    std::lock_guard<std::mutex> lock(m_spawn_config_mutex);
    if (atlas) {
      m_badges.clear();
      m_spawn_times.clear();
      m_sprite_lookup = std::move(atlas->lookup);
      m_sprites = std::move(atlas->sprites);
      m_current_emoji_path = std::move(atlas->normalized_path);
    }
    if (pending.spawn_strategy == "near_caret") {
      m_spawn_strategy = BadgeSpawnStrategy::NearCaret;
    } else {
      m_spawn_strategy = BadgeSpawnStrategy::RandomScreen;
    }

    m_badge_min_px = pending.badge_min_px;
    m_badge_max_px = pending.badge_max_px;
    m_badges_per_second_max = pending.badges_per_second_max;

    build_selector(pending.emoji, pending.emoji_weighted);
  }

  if (pending.fps_mode == "fixed") {
    set_fps_fixed(pending.fps_fixed);
    set_fps_mode(platform::FpsMode::Fixed);
  } else {
    set_fps_mode(platform::FpsMode::Auto);
  }
}

void Overlay::shutdown() {
#ifndef LIZARD_TEST
  stop();
  platform::make_context_current(m_window);
  m_texture.reset();
  m_vbo.reset();
  m_instance.reset();
  m_vao.reset();
  m_program.reset();
  platform::clear_current_context(m_window);
  platform::destroy_window(m_window);
#endif
}

int Overlay::select_sprite() {
  std::lock_guard<std::mutex> lock(m_spawn_config_mutex);
  return select_sprite_locked();
}

void Overlay::spawn_badge(float x, float y) {
  std::lock_guard<std::mutex> lock(m_spawn_config_mutex);
  int sprite = select_sprite_locked();
  spawn_badge_locked(sprite, x, y);
}

void Overlay::spawn_badge(int sprite, float x, float y) {
  std::lock_guard<std::mutex> lock(m_spawn_config_mutex);
  spawn_badge_locked(sprite, x, y);
}

void Overlay::enqueue_spawn(float x, float y) {
  std::lock_guard<std::mutex> lock(m_spawn_queue_mutex);
  m_spawn_queue.push(SpawnRequest{std::nullopt, x, y});
}

void Overlay::enqueue_spawn(int sprite, float x, float y) {
  std::lock_guard<std::mutex> lock(m_spawn_queue_mutex);
  m_spawn_queue.push(SpawnRequest{std::make_optional(sprite), x, y});
}

int Overlay::select_sprite_locked() {
  if (m_selector_indices.empty()) {
    return 0;
  }
  auto idx = m_selector(m_rng);
  return m_selector_indices[idx];
}

void Overlay::spawn_badge_locked(int sprite, float x, float y) {
  if (m_badge_suppressed) {
    if (m_badges.size() < static_cast<std::size_t>(m_badge_capacity * 0.8f)) {
      m_badge_suppressed = false;
    } else {
      return;
    }
  }
  if (m_badges.size() >= m_badge_capacity) {
    m_badge_suppressed = true;
    return;
  }

  auto now = std::chrono::steady_clock::now();
  while (!m_spawn_times.empty() && now - m_spawn_times.front() > std::chrono::seconds(1)) {
    m_spawn_times.pop_front();
  }
  if (m_badges_per_second_max > 0 &&
      static_cast<int>(m_spawn_times.size()) >= m_badges_per_second_max) {
    return;
  }

  auto monitors = active_monitors();
  if (monitors.empty()) {
    monitors.push_back(MonitorBounds{m_virtual_origin_x, m_virtual_origin_y,
                                     m_virtual_origin_x + std::max(m_view_width, 1.0f),
                                     m_virtual_origin_y + std::max(m_view_height, 1.0f)});
  }

  auto normalized_from_absolute = [&](float abs_x, float abs_y) {
    float width = m_view_width > 0.0f ? m_view_width : 1.0f;
    float height = m_view_height > 0.0f ? m_view_height : 1.0f;
    float nx = (abs_x - m_virtual_origin_x) / width;
    float ny = (abs_y - m_virtual_origin_y) / height;
    nx = std::clamp(nx, 0.0f, 1.0f);
    ny = std::clamp(ny, 0.0f, 1.0f);
    return std::pair<float, float>{nx, ny};
  };

  auto sample_point_in_monitor = [&](const MonitorBounds &bounds) {
    constexpr float inset = 24.0f;
    float left = bounds.left + inset;
    float right = bounds.right - inset;
    float top = bounds.top + inset;
    float bottom = bounds.bottom - inset;
    if (right <= left) {
      float mid = (bounds.left + bounds.right) * 0.5f;
      left = right = mid;
    }
    if (bottom <= top) {
      float mid = (bounds.top + bounds.bottom) * 0.5f;
      top = bottom = mid;
    }
    float abs_x = left;
    float abs_y = top;
    if (right > left) {
      std::uniform_real_distribution<float> dist_x(left, right);
      abs_x = dist_x(m_rng);
    }
    if (bottom > top) {
      std::uniform_real_distribution<float> dist_y(top, bottom);
      abs_y = dist_y(m_rng);
    }
    return normalized_from_absolute(abs_x, abs_y);
  };

  float px = std::clamp(x, 0.0f, 1.0f);
  float py = std::clamp(y, 0.0f, 1.0f);
  if (m_spawn_strategy == BadgeSpawnStrategy::RandomScreen) {
    std::vector<double> weights;
    weights.reserve(monitors.size());
    constexpr float inset = 24.0f;
    for (const auto &m : monitors) {
      float width = std::max(0.0f, m.right - m.left);
      float height = std::max(0.0f, m.bottom - m.top);
      float usable_width = std::max(0.0f, width - inset * 2.0f);
      float usable_height = std::max(0.0f, height - inset * 2.0f);
      double area = static_cast<double>(usable_width) * static_cast<double>(usable_height);
      if (area <= 0.0 && width > 0.0f && height > 0.0f) {
        area = static_cast<double>(width) * static_cast<double>(height);
      }
      if (area <= 0.0) {
        area = 1.0;
      }
      weights.push_back(area);
    }
    std::discrete_distribution<std::size_t> monitor_dist(weights.begin(), weights.end());
    std::size_t idx = monitor_dist(m_rng);
    auto sampled = sample_point_in_monitor(monitors[idx]);
    px = sampled.first;
    py = sampled.second;
  } else if (m_spawn_strategy == BadgeSpawnStrategy::NearCaret) {
    if (auto caret = lizard::platform::caret_pos()) {
      auto norm = normalized_from_absolute(caret->first, caret->second);
      px = norm.first;
      py = norm.second;
    } else {
      auto fg = foreground_monitor(monitors);
      if (!fg && !monitors.empty()) {
        fg = monitors.front();
      }
      if (fg) {
        auto sampled = sample_point_in_monitor(*fg);
        px = sampled.first;
        py = sampled.second;
      }
    }
  }

  std::uniform_real_distribution<float> angleDist(-0.3f, 0.3f);
  std::uniform_real_distribution<float> speedDist(0.15f, 0.3f);
  float angle = angleDist(m_rng);
  float speed = speedDist(m_rng);
  float vx = std::sin(angle) * speed;
  float vy = std::cos(angle) * speed;

  std::uniform_real_distribution<float> phaseDist(0.0f, 6.2831853f);
  float phase = phaseDist(m_rng);

  std::uniform_real_distribution<float> diaDist(static_cast<float>(m_badge_min_px),
                                                static_cast<float>(m_badge_max_px));
  float diameter = diaDist(m_rng);
  float scale = (diameter * 2.0f) / m_view_height;

  std::uniform_real_distribution<float> rotDist(-5.0f, 5.0f);
  float rotation = rotDist(m_rng) * 3.14159265f / 180.0f;

  std::uniform_real_distribution<float> lifeDist(0.7f, 1.2f);
  std::uniform_real_distribution<float> fadeInDist(0.06f, 0.12f);
  std::uniform_real_distribution<float> fadeOutDist(0.2f, 0.6f);
  float lifetime = lifeDist(m_rng);
  float fade_in = fadeInDist(m_rng);
  float fade_out = fadeOutDist(m_rng);

  m_badges.emplace_back(Badge{px, py, vx, vy, phase, scale, 0.0f, rotation, 0.0f, lifetime, fade_in,
                              fade_out, sprite});
  m_spawn_times.push_back(now);
}

void Overlay::process_spawn_queue() {
  std::queue<SpawnRequest> local;
  {
    std::lock_guard<std::mutex> lock(m_spawn_queue_mutex);
    std::swap(local, m_spawn_queue);
  }
  while (!local.empty()) {
    auto &request = local.front();
    if (request.sprite.has_value()) {
      spawn_badge(request.sprite.value(), request.x, request.y);
    } else {
      spawn_badge(request.x, request.y);
    }
    local.pop();
  }
}

void Overlay::stop() { m_running = false; }

void Overlay::update(float dt) {
  auto cubicOut = [](float t) { return 1.0f - std::pow(1.0f - t, 3.0f); };
  for (auto &b : m_badges) {
    b.time += dt;
    float nx = std::sin(b.time * 6.2831853f + b.phase) * 0.02f;
    float ny = std::cos(b.time * 6.2831853f + b.phase) * 0.02f;
    b.x += (b.vx + nx) * dt;
    b.y += (b.vy + ny) * dt;
    if (b.time < b.fade_in) {
      float p = b.time / b.fade_in;
      b.alpha = cubicOut(p);
    } else if (b.time > b.lifetime - b.fade_out) {
      float p = (b.lifetime - b.time) / b.fade_out;
      p = std::clamp(p, 0.0f, 1.0f);
      b.alpha = cubicOut(p);
    } else {
      b.alpha = 1.0f;
    }
  }
  m_badges.erase(std::remove_if(m_badges.begin(), m_badges.end(),
                                [](const Badge &b) { return b.time >= b.lifetime; }),
                 m_badges.end());
  if (m_badge_suppressed && m_badges.size() < static_cast<std::size_t>(m_badge_capacity * 0.8f)) {
    m_badge_suppressed = false;
  }
}

void Overlay::render() {
#ifndef LIZARD_TEST
  glClearColor(0, 0, 0, 0);
  glClear(GL_COLOR_BUFFER_BIT);

  glEnable(GL_BLEND);
  glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

  m_instanceData.clear();
  if (m_instanceData.capacity() < m_badges.size() * 10) {
    m_instanceData.reserve(m_badges.size() * 10);
  }
  for (const auto &b : m_badges) {
    const Sprite &s = m_sprites[b.sprite];
    m_instanceData.push_back(b.x);
    m_instanceData.push_back(b.y);
    m_instanceData.push_back(b.scale);
    m_instanceData.push_back(b.scale);
    m_instanceData.push_back(b.rotation);
    m_instanceData.push_back(b.alpha);
    m_instanceData.push_back(s.u0);
    m_instanceData.push_back(s.v0);
    m_instanceData.push_back(s.u1);
    m_instanceData.push_back(s.v1);
  }
  glBindBuffer(GL_ARRAY_BUFFER, m_instance.id);
  glBufferSubData(GL_ARRAY_BUFFER, 0, m_instanceData.size() * sizeof(float), m_instanceData.data());

  glUseProgram(m_program.id);
  glBindVertexArray(m_vao.id);
  glBindTexture(GL_TEXTURE_2D, m_texture.id);
  glDrawArraysInstanced(GL_TRIANGLE_FAN, 0, 4, static_cast<GLsizei>(m_badges.size()));
  platform::swap_buffers(m_window);
  platform::poll_events(m_window);
#endif
}

void Overlay::run(std::stop_token st) {
#ifndef LIZARD_TEST
  platform::make_context_current(m_window);
  using clock = std::chrono::steady_clock;
  auto last = clock::now();
  while (m_running && !st.stop_requested()) {
    if (m_has_pending_config.exchange(false, std::memory_order_acq_rel)) {
      apply_pending_config();
    }
    process_spawn_queue();
    auto frame = std::chrono::microseconds(m_frame_interval_us.load());
    if (m_paused.load()) {
      std::this_thread::sleep_for(frame);
      last = clock::now();
      platform::poll_events(m_window);
      continue;
    }
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
  platform::clear_current_context(m_window);
  stop();
#else
  (void)st;
#endif
}

} // namespace lizard::overlay
