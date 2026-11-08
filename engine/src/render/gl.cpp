#include "slave/render/gl.hpp"

#include <algorithm>
#include <cstring>

#include "slave/core/log.hpp"

namespace slave::gl {

namespace {
GLuint compileStage(GLenum type, const char* src, std::string& error) {
  const GLuint s = glCreateShader(type);
  glShaderSource(s, 1, &src, nullptr);
  glCompileShader(s);
  GLint ok = 0;
  glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    char log[2048];
    glGetShaderInfoLog(s, sizeof log, nullptr, log);
    error = std::string(type == GL_VERTEX_SHADER ? "vertex" : "fragment") + " shader: " + log;
    glDeleteShader(s);
    return 0;
  }
  return s;
}
}  // namespace

Shader::~Shader() {
  if (program_) glDeleteProgram(program_);
}

bool Shader::compile(const char* vertexSrc, const char* fragmentSrc, std::string& error) {
  const GLuint vs = compileStage(GL_VERTEX_SHADER, vertexSrc, error);
  if (!vs) return false;
  const GLuint fs = compileStage(GL_FRAGMENT_SHADER, fragmentSrc, error);
  if (!fs) {
    glDeleteShader(vs);
    return false;
  }
  program_ = glCreateProgram();
  glAttachShader(program_, vs);
  glAttachShader(program_, fs);
  glLinkProgram(program_);
  glDeleteShader(vs);
  glDeleteShader(fs);
  GLint ok = 0;
  glGetProgramiv(program_, GL_LINK_STATUS, &ok);
  if (!ok) {
    char log[2048];
    glGetProgramInfoLog(program_, sizeof log, nullptr, log);
    error = std::string("shader link: ") + log;
    return false;
  }
  return true;
}

Texture::~Texture() {
  if (id_) glDeleteTextures(1, &id_);
}

namespace {
void applyParams(GLenum target, TextureParams p) {
  glTexParameteri(target, GL_TEXTURE_MIN_FILTER,
                  p.linear ? (p.mipmaps ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR) : GL_NEAREST);
  glTexParameteri(target, GL_TEXTURE_MAG_FILTER, p.linear ? GL_LINEAR : GL_NEAREST);
  const GLint wrap = p.repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE;
  glTexParameteri(target, GL_TEXTURE_WRAP_S, wrap);
  glTexParameteri(target, GL_TEXTURE_WRAP_T, wrap);
}
}  // namespace

void Texture::create(const Image& image, TextureParams params) {
  if (!id_) glGenTextures(1, &id_);
  w_ = image.w;
  h_ = image.h;
  glBindTexture(GL_TEXTURE_2D, id_);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w_, h_, 0, GL_RGBA, GL_UNSIGNED_BYTE, image.px.data());
  applyParams(GL_TEXTURE_2D, params);
  if (params.mipmaps) glGenerateMipmap(GL_TEXTURE_2D);
}

void Texture::allocate(int w, int h, TextureParams params) {
  if (!id_) glGenTextures(1, &id_);
  w_ = w;
  h_ = h;
  glBindTexture(GL_TEXTURE_2D, id_);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
  applyParams(GL_TEXTURE_2D, params);
}

void Texture::update(int x, int y, int w, int h, const void* rgba) {
  glBindTexture(GL_TEXTURE_2D, id_);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
  glTexSubImage2D(GL_TEXTURE_2D, 0, x, y, w, h, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
}

void Texture::bind(int unit) const {
  glActiveTexture(GL_TEXTURE0 + unit);
  glBindTexture(GL_TEXTURE_2D, id_);
}

TextureArray::~TextureArray() {
  if (id_) glDeleteTextures(1, &id_);
}

void TextureArray::create(const std::vector<Image>& layers) {
  if (layers.empty()) return;
  if (!id_) glGenTextures(1, &id_);
  glBindTexture(GL_TEXTURE_2D_ARRAY, id_);
  const int w = layers[0].w, h = layers[0].h;
  glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
  glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA8, w, h, static_cast<GLsizei>(layers.size()), 0, GL_RGBA,
               GL_UNSIGNED_BYTE, nullptr);
  for (size_t i = 0; i < layers.size(); ++i)
    glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, static_cast<GLint>(i), w, h, 1, GL_RGBA, GL_UNSIGNED_BYTE,
                    layers[i].px.data());
  applyParams(GL_TEXTURE_2D_ARRAY, {true, true, true});
  glGenerateMipmap(GL_TEXTURE_2D_ARRAY);
  // The ground seen at a low angle in 3D stays sharp with anisotropic filtering.
  static const bool aniso = [] {
    GLint n = 0;
    glGetIntegerv(GL_NUM_EXTENSIONS, &n);
    for (GLint i = 0; i < n; ++i) {
      const char* e = reinterpret_cast<const char*>(glGetStringi(GL_EXTENSIONS, static_cast<GLuint>(i)));
      if (e && (std::strcmp(e, "GL_EXT_texture_filter_anisotropic") == 0 || std::strcmp(e, "GL_ARB_texture_filter_anisotropic") == 0))
        return true;
    }
    return false;
  }();
  if (aniso) {
    constexpr GLenum kMaxAniso = 0x84FE, kMaxAnisoLimit = 0x84FF;  // GL_TEXTURE_MAX_ANISOTROPY(_EXT)
    GLfloat limit = 1;
    glGetFloatv(kMaxAnisoLimit, &limit);
    glTexParameterf(GL_TEXTURE_2D_ARRAY, kMaxAniso, std::min(8.0f, limit));
  }
}

void TextureArray::bind(int unit) const {
  glActiveTexture(GL_TEXTURE0 + unit);
  glBindTexture(GL_TEXTURE_2D_ARRAY, id_);
}

#ifndef NDEBUG
namespace {
void GLAD_API_PTR debugCallback(GLenum, GLenum type, GLuint, GLenum severity, GLsizei, const GLchar* msg,
                            const void*) {
  if (severity == GL_DEBUG_SEVERITY_NOTIFICATION) return;
  if (type == GL_DEBUG_TYPE_ERROR) SLV_ERROR("GL: %s", msg);
  else SLV_DEBUG("GL: %s", msg);
}
}  // namespace
#endif

void installDebugOutput() {
#ifndef NDEBUG
  if (GLAD_GL_KHR_debug) {
    glEnable(GL_DEBUG_OUTPUT);
    glDebugMessageCallback(debugCallback, nullptr);
  }
#endif
}

}  // namespace slave::gl
