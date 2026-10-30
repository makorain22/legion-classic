#pragma once
// Slave Engine - thin OpenGL 3.3 helpers: shaders, textures, instance buffers.

#include <glad/gl.h>

#include <cstdint>
#include <string>
#include <vector>

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

struct TextureParams {
  bool linear = false;   // false = nearest-neighbour (crisp pixels)
  bool mipmaps = false;
  bool repeat = false;   // false = clamp to edge
};

class Texture {
 public:
  Texture() = default;
  ~Texture();
  Texture(const Texture&) = delete;
  Texture& operator=(const Texture&) = delete;

  void create(const Image& image, TextureParams params = {});
  // Allocates an empty RGBA8 texture.
  void allocate(int w, int h, TextureParams params = {});
  // Replaces a rectangle with tightly packed RGBA8 pixels.
  void update(int x, int y, int w, int h, const void* rgba);
  void bind(int unit = 0) const;
  int width() const { return w_; }
  int height() const { return h_; }
  GLuint id() const { return id_; }

 private:
  GLuint id_ = 0;
  int w_ = 0, h_ = 0;
};

// A stack of same-sized textures sampled as one (terrain layers).
class TextureArray {
 public:
  TextureArray() = default;
  ~TextureArray();
  TextureArray(const TextureArray&) = delete;
  TextureArray& operator=(const TextureArray&) = delete;

  void create(const std::vector<Image>& layers);  // linear, mipmapped, repeating
  void bind(int unit = 0) const;

 private:
  GLuint id_ = 0;
};

// Enables GL debug output in debug builds when the driver supports it.
void installDebugOutput();

}  // namespace slave::gl
