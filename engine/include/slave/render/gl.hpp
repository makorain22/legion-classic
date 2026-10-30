#pragma once
// Slave Engine - thin OpenGL 3.3 helpers: shaders, textures, instance buffers.

#include <glad/gl.h>

#include <cstdint>
#include <string>

#include "slave/assets/image.hpp"

namespace slave::gl {

class Shader {
 public:
  Shader() = default;
  ~Shader();
  Shader(const Shader&) = delete;
  Shader& operator=(const Shader&) = delete;

  bool compile(const char* vertexSrc, const char* fragmentSrc, std::string& error);
  void use() const { glUseProgram(program_); }
  GLint uniform(const char* name) const { return glGetUniformLocation(program_, name); }

 private:
  GLuint program_ = 0;
};

class Texture {
 public:
  Texture() = default;
  ~Texture();
  Texture(const Texture&) = delete;
  Texture& operator=(const Texture&) = delete;

  // Uploads an RGBA image with nearest-neighbour filtering (crisp pixels).
  void create(const Image& image);
  void bind(int unit = 0) const;
  int width() const { return w_; }
  int height() const { return h_; }

 private:
  GLuint id_ = 0;
  int w_ = 0, h_ = 0;
};

// Enables GL debug output in debug builds when the driver supports it.
void installDebugOutput();

}  // namespace slave::gl
