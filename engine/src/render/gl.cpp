#include "slave/render/gl.hpp"

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

void Texture::create(const Image& image) {
  if (!id_) glGenTextures(1, &id_);
  w_ = image.w;
  h_ = image.h;
  glBindTexture(GL_TEXTURE_2D, id_);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w_, h_, 0, GL_RGBA, GL_UNSIGNED_BYTE, image.px.data());
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

void Texture::bind(int unit) const {
  glActiveTexture(GL_TEXTURE0 + unit);
  glBindTexture(GL_TEXTURE_2D, id_);
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
