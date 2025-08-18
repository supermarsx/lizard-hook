#pragma once

#ifndef LIZARD_TEST
#include "glad/glad.h"
#else
using GLuint = unsigned int;
using GLsizei = int;
#endif

namespace lizard::overlay::gl {

struct Texture {
  Texture() = default;
  ~Texture();
  Texture(const Texture &) = delete;
  Texture &operator=(const Texture &) = delete;
  Texture(Texture &&other) noexcept;
  Texture &operator=(Texture &&other) noexcept;
  void create();
  void reset();
  GLuint id = 0;
};

struct Buffer {
  Buffer() = default;
  ~Buffer();
  Buffer(const Buffer &) = delete;
  Buffer &operator=(const Buffer &) = delete;
  Buffer(Buffer &&other) noexcept;
  Buffer &operator=(Buffer &&other) noexcept;
  void create();
  void reset();
  GLuint id = 0;
};

struct Program {
  Program() = default;
  ~Program();
  Program(const Program &) = delete;
  Program &operator=(const Program &) = delete;
  Program(Program &&other) noexcept;
  Program &operator=(Program &&other) noexcept;
  void create();
  void reset();
  GLuint id = 0;
};

struct VertexArray {
  VertexArray() = default;
  ~VertexArray();
  VertexArray(const VertexArray &) = delete;
  VertexArray &operator=(const VertexArray &) = delete;
  VertexArray(VertexArray &&other) noexcept;
  VertexArray &operator=(VertexArray &&other) noexcept;
  void create();
  void reset();
  GLuint id = 0;
};

} // namespace lizard::overlay::gl
