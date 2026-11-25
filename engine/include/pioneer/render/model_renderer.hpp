#pragma once
// Pioneer Engine - draws one model (a glTF ModelAsset: meshes, materials,
// team paint, a skin) into a picture of its own, for the model viewer.
//
// This is the first code that draws glTF. It keeps to the engine's rules for
// old hardware: OpenGL 3.3 core / ES 3.0, no compute, skinning done on the
// CPU (tools::ModelView) and the posed vertices streamed to the GPU each time
// the pose changes. Lit by one sun and a sky term; team-painted materials take
// the team colour; the ground grid, the model's box and its bones are drawn
// as lines.
//
//   ModelRenderer r;  r.init(err);
//   r.setModel(view.shared());                    // when view.modelRevision() moves
//   r.setPose(view.posedMeshes());                // when view.poseRevision() moves
//   GLuint tex = r.render(w, h, view.camera, view.options, view.bones());
//   ImGui::Image(tex, ...);                       // the picture, upside down for ImGui (uv 0,1 -> 1,0)
//
// render() draws into its own target and puts back the framebuffer, viewport
// and the state it touched, so it can be called while ImGui builds a frame.

#include <memory>
#include <vector>

#include "pioneer/assets/gltf.hpp"
#include "pioneer/assets/image.hpp"
#include "pioneer/render/device.hpp"
#include "pioneer/render/gl.hpp"
#include "pioneer/tools/model_view.hpp"

namespace pioneer {

class ModelRenderer {
 public:
  ModelRenderer() = default;
  ~ModelRenderer();
  ModelRenderer(const ModelRenderer&) = delete;
  ModelRenderer& operator=(const ModelRenderer&) = delete;

  bool init(std::string& error);
  bool ready() const { return ready_; }
  void setModel(std::shared_ptr<const ModelAsset> model);
  void setPose(const std::vector<std::vector<MeshVertexFull>>& posed);
  // Draws the model; returns the colour texture (0 if it can't draw).
  GLuint render(int w, int h, const tools::OrbitCamera& camera, const tools::ModelView::Options& options,
                const std::vector<tools::Bone>& bones);
  // The last picture drawn, the right way up.
  bool readPixels(Image& out) const;
  size_t trianglesLastFrame() const { return triangles_; }

 private:
  struct GpuMesh {
    GLuint vao = 0, vbo = 0, ebo = 0;
    GLsizei indices = 0;
    int material = -1;
    bool skinned = false;
  };
  struct Vertex {
    float pos[3];
    float normal[3];
    uint8_t color[4];
  };
  void release();
  void upload(GpuMesh& g, const std::vector<MeshVertexFull>& verts, bool dynamic);
  void drawLines(const std::vector<float>& lines, const Mat4& viewProj, bool depthTest);

  gl::Shader mesh_, line_;
  gfx::RenderTarget target_;
  std::shared_ptr<const ModelAsset> model_;
  std::vector<GpuMesh> meshes_;
  GLuint lineVao_ = 0, lineVbo_ = 0;
  size_t lineCapacity_ = 0;
  size_t triangles_ = 0;
  bool ready_ = false;
};

}  // namespace pioneer
