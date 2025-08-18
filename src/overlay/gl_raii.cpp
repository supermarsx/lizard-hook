#include "overlay/gl_raii.h"

#include <utility>

namespace lizard::overlay::gl {

Texture::~Texture() { reset(); }

Texture::Texture(Texture &&other) noexcept : id(other.id) { other.id = 0; }

Texture &Texture::operator=(Texture &&other) noexcept {
  if (this != &other) {
    reset();
    id = other.id;
    other.id = 0;
  }
  return *this;
}

void Texture::create() {
  reset();
  glGenTextures(1, &id);
}

void Texture::reset() {
  if (id) {
    glDeleteTextures(1, &id);
    id = 0;
  }
}

Buffer::~Buffer() { reset(); }

Buffer::Buffer(Buffer &&other) noexcept : id(other.id) { other.id = 0; }

Buffer &Buffer::operator=(Buffer &&other) noexcept {
  if (this != &other) {
    reset();
    id = other.id;
    other.id = 0;
  }
  return *this;
}

void Buffer::create() {
  reset();
  glGenBuffers(1, &id);
}

void Buffer::reset() {
  if (id) {
    glDeleteBuffers(1, &id);
    id = 0;
  }
}

Program::~Program() { reset(); }

Program::Program(Program &&other) noexcept : id(other.id) { other.id = 0; }

Program &Program::operator=(Program &&other) noexcept {
  if (this != &other) {
    reset();
    id = other.id;
    other.id = 0;
  }
  return *this;
}

void Program::create() {
  reset();
  id = glCreateProgram();
}

void Program::reset() {
  if (id) {
    glDeleteProgram(id);
    id = 0;
  }
}

VertexArray::~VertexArray() { reset(); }

VertexArray::VertexArray(VertexArray &&other) noexcept : id(other.id) { other.id = 0; }

VertexArray &VertexArray::operator=(VertexArray &&other) noexcept {
  if (this != &other) {
    reset();
    id = other.id;
    other.id = 0;
  }
  return *this;
}

void VertexArray::create() {
  reset();
  glGenVertexArrays(1, &id);
}

void VertexArray::reset() {
  if (id) {
    glDeleteVertexArrays(1, &id);
    id = 0;
  }
}

} // namespace lizard::overlay::gl
