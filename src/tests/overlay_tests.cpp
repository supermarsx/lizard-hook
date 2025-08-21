#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <array>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <spdlog/sinks/ostream_sink.h>

#define private public
#include "app/config.h"
#undef private

struct OverlayTestAccess;
#define LIZARD_TEST
using GLuint = unsigned int;
using GLsizei = int;

extern "C" {
void glGenTextures(GLsizei, GLuint *);
void glDeleteTextures(GLsizei, const GLuint *);
void glGenBuffers(GLsizei, GLuint *);
void glDeleteBuffers(GLsizei, const GLuint *);
void glGenVertexArrays(GLsizei, GLuint *);
void glDeleteVertexArrays(GLsizei, const GLuint *);
GLuint glCreateProgram();
void glDeleteProgram(GLuint);
}

#include "overlay/gl_raii.cpp"
#include "overlay/overlay.cpp"

struct OverlayTestAccess {
  static std::vector<lizard::overlay::Sprite> &sprites(lizard::overlay::Overlay &o) {
    return o.m_sprites;
  }
  static std::unordered_map<std::string, int> &sprite_lookup(lizard::overlay::Overlay &o) {
    return o.m_sprite_lookup;
  }
  static std::mt19937 &rng(lizard::overlay::Overlay &o) { return o.m_rng; }
  static int select_sprite(lizard::overlay::Overlay &o) { return o.select_sprite(); }
  static lizard::overlay::gl::Texture &texture(lizard::overlay::Overlay &o) { return o.m_texture; }
  static lizard::overlay::gl::Buffer &vbo(lizard::overlay::Overlay &o) { return o.m_vbo; }
  static lizard::overlay::gl::Buffer &instance(lizard::overlay::Overlay &o) { return o.m_instance; }
  static lizard::overlay::gl::VertexArray &vao(lizard::overlay::Overlay &o) { return o.m_vao; }
  static lizard::overlay::gl::Program &program(lizard::overlay::Overlay &o) { return o.m_program; }
  static std::vector<lizard::overlay::Badge> &badges(lizard::overlay::Overlay &o) {
    return o.m_badges;
  }
};

bool g_overlay_log_called = false;
namespace {
int g_textures_deleted = 0;
int g_buffers_deleted = 0;
int g_vertex_arrays_deleted = 0;
int g_programs_deleted = 0;
GLuint g_next_id = 1;
} // namespace

extern "C" {
void glGenTextures(GLsizei n, GLuint *textures) {
  for (int i = 0; i < n; ++i) {
    textures[i] = g_next_id++;
  }
}
void glDeleteTextures(GLsizei n, const GLuint *) { g_textures_deleted += n; }
void glGenBuffers(GLsizei n, GLuint *buffers) {
  for (int i = 0; i < n; ++i) {
    buffers[i] = g_next_id++;
  }
}
void glDeleteBuffers(GLsizei n, const GLuint *) { g_buffers_deleted += n; }
void glGenVertexArrays(GLsizei n, GLuint *arrays) {
  for (int i = 0; i < n; ++i) {
    arrays[i] = g_next_id++;
  }
}
void glDeleteVertexArrays(GLsizei n, const GLuint *) { g_vertex_arrays_deleted += n; }
GLuint glCreateProgram() { return g_next_id++; }
void glDeleteProgram(GLuint) { g_programs_deleted++; }

unsigned char *stbi_load(const char *, int *, int *, int *, int) { return nullptr; }
unsigned char *stbi_load_from_memory(const unsigned char *, int, int *, int *, int *, int) {
  return nullptr;
}
const char *stbi_failure_reason(void) { return "stub failure"; }
void stbi_image_free(void *) {}
}

using Catch::Approx;
using lizard::app::Config;
using lizard::overlay::Overlay;

TEST_CASE("select_sprite respects weights", "[overlay]") {
  Overlay ov;
  OverlayTestAccess::sprites(ov) = {lizard::overlay::Sprite{0, 0, 0, 0},
                                    lizard::overlay::Sprite{0, 0, 0, 0},
                                    lizard::overlay::Sprite{0, 0, 0, 0}};
  OverlayTestAccess::sprite_lookup(ov) = {{"A", 0}, {"B", 1}, {"C", 2}};

  Config cfg(std::filesystem::temp_directory_path());
  cfg.emoji_weighted_ = {{"A", 1.0}, {"B", 3.0}, {"C", 6.0}};

  ov.init(cfg);
  OverlayTestAccess::rng(ov).seed(42);

  const int samples = 10000;
  std::array<int, 3> counts{0, 0, 0};
  for (int i = 0; i < samples; ++i) {
    int idx = OverlayTestAccess::select_sprite(ov);
    counts[idx]++;
  }
  double total = 1.0 + 3.0 + 6.0;
  REQUIRE(counts[0] == Approx(samples * (1.0 / total)).margin(samples * 0.05));
  REQUIRE(counts[1] == Approx(samples * (3.0 / total)).margin(samples * 0.05));
  REQUIRE(counts[2] == Approx(samples * (6.0 / total)).margin(samples * 0.05));
}

TEST_CASE("invalid atlas logs error and falls back", "[overlay]") {
  std::filesystem::path tmp = std::filesystem::temp_directory_path() / "lizard_bad_atlas.json";
  {
    std::ofstream out(tmp);
    out << "{"; // invalid JSON
  }

  std::ostringstream oss;
  auto sink = std::make_shared<spdlog::sinks::ostream_sink_mt>(oss);
  auto logger = std::make_shared<spdlog::logger>("test", sink);
  auto old_logger = spdlog::default_logger();
  spdlog::set_default_logger(logger);

  Config cfg(std::filesystem::temp_directory_path());
  Overlay ov;
  ov.init(cfg, tmp);
  logger->flush();
  spdlog::set_default_logger(old_logger);

  REQUIRE(OverlayTestAccess::sprites(ov).size() == 1);
  REQUIRE(OverlayTestAccess::sprite_lookup(ov).count("🦎") == 1);
}

TEST_CASE("invalid image logs error", "[overlay]") {
  std::filesystem::path tmp = std::filesystem::temp_directory_path() / "lizard_bad.png";
  {
    std::ofstream out(tmp, std::ios::binary);
    out << "not a png";
  }
  Config cfg(std::filesystem::temp_directory_path());
  Overlay ov;
  g_overlay_log_called = false;
  bool ok = ov.init(cfg, tmp);
  std::filesystem::remove(tmp);

  REQUIRE_FALSE(ok);
  REQUIRE(g_overlay_log_called);
}

TEST_CASE("GL resources released when Overlay is destroyed", "[overlay]") {
  g_textures_deleted = 0;
  g_buffers_deleted = 0;
  g_vertex_arrays_deleted = 0;
  g_programs_deleted = 0;
  {
    Overlay ov;
    OverlayTestAccess::texture(ov).create();
    OverlayTestAccess::vbo(ov).create();
    OverlayTestAccess::instance(ov).create();
    OverlayTestAccess::vao(ov).create();
    OverlayTestAccess::program(ov).create();
  }
  REQUIRE(g_textures_deleted == 1);
  REQUIRE(g_buffers_deleted == 2);
  REQUIRE(g_vertex_arrays_deleted == 1);
  REQUIRE(g_programs_deleted == 1);
}

TEST_CASE("random_screen strategy randomizes badge position", "[overlay]") {
  Config cfg(std::filesystem::temp_directory_path());
  cfg.badge_spawn_strategy_ = "random_screen";
  Overlay ov;
  ov.init(cfg);
  OverlayTestAccess::rng(ov).seed(1337);
  ov.spawn_badge(0, 0.0f, 0.0f);
  auto &b = OverlayTestAccess::badges(ov).back();
  REQUIRE(b.x == Approx(0.262025f));
  REQUIRE(b.y == Approx(0.56053f));
}

TEST_CASE("cursor_follow strategy uses provided coordinates", "[overlay]") {
  Config cfg(std::filesystem::temp_directory_path());
  cfg.badge_spawn_strategy_ = "cursor_follow";
  Overlay ov;
  ov.init(cfg);
  ov.spawn_badge(0, 0.25f, 0.75f);
  auto &b = OverlayTestAccess::badges(ov).back();
  REQUIRE(b.x == Approx(0.25f));
  REQUIRE(b.y == Approx(0.75f));
}
