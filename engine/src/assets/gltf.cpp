// Pioneer Engine - glTF 2.0 models. See gltf.hpp.
#include "pioneer/assets/gltf.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <map>
#include <tuple>
#include <unordered_map>

#include <nlohmann/json.hpp>

#include "pioneer/assets/voxel_mesh.hpp"

namespace pioneer {

namespace {
using json = nlohmann::json;

constexpr uint32_t kGlbMagic = 0x46546C67;  // "glTF"
constexpr uint32_t kChunkJson = 0x4E4F534A, kChunkBin = 0x004E4942;

// glTF is y-up, the engine z-up: engine (x, y, z) = (gx, -gz, gy), a quarter turn about x.
Transform yUpToEngine() {
  Transform t;
  t.r = quatAxisAngle(1, 0, 0, 1.5707963f);
  return t;
}

bool decodeBase64(const std::string& in, std::vector<uint8_t>& out) {
  static const std::string kChars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  int val = 0, bits = -8;
  out.clear();
  for (char c : in) {
    if (c == '=') break;
    const size_t p = kChars.find(c);
    if (p == std::string::npos) {
      if (c == '\n' || c == '\r' || c == ' ') continue;
      return false;
    }
    val = (val << 6) + static_cast<int>(p);
    bits += 6;
    if (bits >= 0) {
      out.push_back(static_cast<uint8_t>((val >> bits) & 0xFF));
      bits -= 8;
    }
  }
  return true;
}

int componentCount(const std::string& type) {
  if (type == "SCALAR") return 1;
  if (type == "VEC2") return 2;
  if (type == "VEC3") return 3;
  if (type == "VEC4") return 4;
  if (type == "MAT4") return 16;
  if (type == "MAT3") return 9;
  if (type == "MAT2") return 4;
  return 0;
}
int componentSize(int ct) {
  switch (ct) {
    case 5120: case 5121: return 1;
    case 5122: case 5123: return 2;
    case 5125: case 5126: return 4;
    default: return 0;
  }
}

struct Reader {
  const json& doc;
  std::vector<std::vector<uint8_t>> buffers;
  std::string error;

  // Every element of an accessor as floats (normalized integers to 0..1 / -1..1).
  bool floats(int index, std::vector<float>& out, int& comps) {
    out.clear();
    if (!doc.contains("accessors") || index < 0 || index >= static_cast<int>(doc["accessors"].size())) {
      error = "accessor " + std::to_string(index) + " missing";
      return false;
    }
    const json& a = doc["accessors"][static_cast<size_t>(index)];
    if (a.contains("sparse")) {
      error = "sparse accessors are not read";
      return false;
    }
    comps = componentCount(a.value("type", std::string()));
    const int ct = a.value("componentType", 0), cs = componentSize(ct);
    const size_t count = a.value("count", size_t{0});
    const bool norm = a.value("normalized", false);
    if (!comps || !cs) {
      error = "accessor " + std::to_string(index) + ": unknown type";
      return false;
    }
    out.assign(count * static_cast<size_t>(comps), 0.0f);
    if (!a.contains("bufferView")) return true;  // all zeros
    const json& bv = doc["bufferViews"][a["bufferView"].get<size_t>()];
    const size_t buf = bv.value("buffer", size_t{0});
    if (buf >= buffers.size()) {
      error = "buffer " + std::to_string(buf) + " missing";
      return false;
    }
    const std::vector<uint8_t>& b = buffers[buf];
    const size_t elem = static_cast<size_t>(comps * cs);
    const size_t stride = bv.value("byteStride", size_t{0}) ? bv["byteStride"].get<size_t>() : elem;
    const size_t base = bv.value("byteOffset", size_t{0}) + a.value("byteOffset", size_t{0});
    if (count && base + (count - 1) * stride + elem > b.size()) {
      error = "accessor " + std::to_string(index) + " runs past its buffer";
      return false;
    }
    for (size_t i = 0; i < count; ++i)
      for (int c = 0; c < comps; ++c) {
        const uint8_t* p = b.data() + base + i * stride + static_cast<size_t>(c * cs);
        float v = 0;
        switch (ct) {
          case 5126: std::memcpy(&v, p, 4); break;
          case 5121: v = norm ? p[0] / 255.0f : p[0]; break;
          case 5120: v = norm ? std::max(static_cast<int8_t>(p[0]) / 127.0f, -1.0f) : static_cast<int8_t>(p[0]); break;
          case 5123: { uint16_t x; std::memcpy(&x, p, 2); v = norm ? x / 65535.0f : x; break; }
          case 5122: { int16_t x; std::memcpy(&x, p, 2); v = norm ? std::max(x / 32767.0f, -1.0f) : x; break; }
          case 5125: { uint32_t x; std::memcpy(&x, p, 4); v = static_cast<float>(x); break; }
        }
        out[i * static_cast<size_t>(comps) + static_cast<size_t>(c)] = v;
      }
    return true;
  }
};

Transform nodeLocal(const json& n) {
  if (n.contains("matrix") && n["matrix"].size() == 16) {
    Mat4 m;
    for (size_t i = 0; i < 16; ++i) m.m[i] = n["matrix"][i].get<float>();
    return decompose(m);
  }
  Transform t;
  if (n.contains("translation"))
    for (size_t i = 0; i < 3; ++i) t.t[i] = n["translation"][i].get<float>();
  if (n.contains("rotation"))
    t.r = {n["rotation"][0].get<float>(), n["rotation"][1].get<float>(), n["rotation"][2].get<float>(), n["rotation"][3].get<float>()};
  if (n.contains("scale"))
    for (size_t i = 0; i < 3; ++i) t.s[i] = n["scale"][i].get<float>();
  return t;
}

// The 3x3 inverse transpose of a matrix's upper left, as a Mat4 (for normals).
Mat4 normalMatrix(const Mat4& m) {
  Mat4 inv = inverse(m), r;
  for (int i = 0; i < 3; ++i)
    for (int j = 0; j < 3; ++j) r.at(i, j) = inv.at(j, i);
  r.at(0, 3) = r.at(1, 3) = r.at(2, 3) = 0;
  return r;
}
}  // namespace

int ModelAsset::findClip(const std::string& name) const {
  for (size_t i = 0; i < clips.size(); ++i)
    if (clips[i].name == name) return static_cast<int>(i);
  return -1;
}

void ModelAsset::bounds(float lo[3], float hi[3]) const {
  for (int i = 0; i < 3; ++i) {
    lo[i] = 1e30f;
    hi[i] = -1e30f;
  }
  for (const auto& m : meshes)
    for (const auto& v : m.vertices)
      for (int i = 0; i < 3; ++i) {
        lo[i] = std::min(lo[i], v.pos[i]);
        hi[i] = std::max(hi[i], v.pos[i]);
      }
}

size_t ModelAsset::triangleCount() const {
  size_t n = 0;
  for (const auto& m : meshes) n += m.indices.size() / 3;
  return n;
}

bool readGltfBytes(const std::vector<uint8_t>& bytes, const std::string& baseDir, ModelAsset& out, std::string& error) {
  out = ModelAsset{};
  json doc;
  std::vector<uint8_t> glbBin;
  bool glb = false;
  if (bytes.size() >= 12) {
    uint32_t magic;
    std::memcpy(&magic, bytes.data(), 4);
    glb = magic == kGlbMagic;
  }
  if (glb) {
    size_t pos = 12;
    bool haveJson = false;
    while (pos + 8 <= bytes.size()) {
      uint32_t len, type;
      std::memcpy(&len, bytes.data() + pos, 4);
      std::memcpy(&type, bytes.data() + pos + 4, 4);
      if (pos + 8 + len > bytes.size()) {
        error = "the .glb is cut short";
        return false;
      }
      const uint8_t* c = bytes.data() + pos + 8;
      if (type == kChunkJson) {
        doc = json::parse(c, c + len, nullptr, false);
        haveJson = true;
      } else if (type == kChunkBin && glbBin.empty()) {
        glbBin.assign(c, c + len);
      }
      pos += 8 + len;
    }
    if (!haveJson) {
      error = "no JSON in the .glb";
      return false;
    }
  } else {
    doc = json::parse(bytes.begin(), bytes.end(), nullptr, false);
  }
  if (doc.is_discarded() || !doc.is_object()) {
    error = "not valid glTF JSON";
    return false;
  }
  if (!doc.contains("asset") || doc["asset"].value("version", std::string()).rfind("2", 0) != 0) {
    error = "not glTF 2.0";
    return false;
  }
  Reader rd{doc, {}, {}};
  for (const auto& b : doc.value("buffers", json::array())) {
    std::vector<uint8_t> data;
    if (!b.contains("uri")) {
      data = glbBin;
    } else {
      const std::string uri = b["uri"].get<std::string>();
      const size_t comma = uri.find(',');
      if (uri.rfind("data:", 0) == 0 && comma != std::string::npos) {
        if (!decodeBase64(uri.substr(comma + 1), data)) {
          error = "a buffer's base64 is damaged";
          return false;
        }
      } else {
        std::ifstream in(std::filesystem::path(baseDir) / uri, std::ios::binary);
        if (!in) {
          error = "buffer file " + uri + " missing";
          return false;
        }
        data.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
      }
    }
    rd.buffers.push_back(std::move(data));
  }

  // Materials.
  for (const auto& m : doc.value("materials", json::array())) {
    Material mat;
    mat.name = m.value("name", std::string());
    if (m.contains("pbrMetallicRoughness")) {
      const json& p = m["pbrMetallicRoughness"];
      if (p.contains("baseColorFactor"))
        for (size_t i = 0; i < 4; ++i) mat.baseColor[i] = p["baseColorFactor"][i].get<float>();
      mat.metallic = p.value("metallicFactor", 1.0f);
      mat.roughness = p.value("roughnessFactor", 1.0f);
      if (p.contains("baseColorTexture")) {
        const int ti = p["baseColorTexture"].value("index", -1);
        if (ti >= 0 && doc.contains("textures") && ti < static_cast<int>(doc["textures"].size())) {
          const int src = doc["textures"][static_cast<size_t>(ti)].value("source", -1);
          if (src >= 0 && doc.contains("images") && src < static_cast<int>(doc["images"].size()))
            mat.baseColorImage = doc["images"][static_cast<size_t>(src)].value("uri", std::string());
          if (mat.baseColorImage.rfind("data:", 0) == 0) mat.baseColorImage.clear();
        }
      }
    }
    if (m.contains("emissiveFactor"))
      for (size_t i = 0; i < 3; ++i) mat.emissive[i] = m["emissiveFactor"][i].get<float>();
    const std::string am = m.value("alphaMode", std::string("OPAQUE"));
    mat.alpha = am == "MASK" ? Material::Alpha::Mask : am == "BLEND" ? Material::Alpha::Blend : Material::Alpha::Opaque;
    mat.alphaCutoff = m.value("alphaCutoff", 0.5f);
    mat.doubleSided = m.value("doubleSided", false);
    mat.team = m.contains("extras") && m["extras"].value("pioneer_team", false);
    out.materials.push_back(mat);
  }

  const json& nodes = doc.contains("nodes") ? doc["nodes"] : json::array();
  const size_t nodeCount = nodes.size();
  std::vector<int> parentOf(nodeCount, -1);
  for (size_t i = 0; i < nodeCount; ++i)
    for (const auto& c : nodes[i].value("children", json::array()))
      if (c.get<size_t>() < nodeCount) parentOf[c.get<size_t>()] = static_cast<int>(i);
  std::vector<Mat4> world(nodeCount);
  std::vector<char> worldDone(nodeCount, 0);
  std::function<Mat4(int)> worldOf = [&](int i) -> Mat4 {
    if (worldDone[static_cast<size_t>(i)]) return world[static_cast<size_t>(i)];
    worldDone[static_cast<size_t>(i)] = 1;  // a loop in a damaged file ends here
    const Mat4 local = nodeLocal(nodes[static_cast<size_t>(i)]).matrix();
    const int p = parentOf[static_cast<size_t>(i)];
    world[static_cast<size_t>(i)] = p >= 0 ? worldOf(p) * local : local;
    return world[static_cast<size_t>(i)];
  };
  const Mat4 toEngine = yUpToEngine().matrix();

  // The skeleton, from the first skin: a joint's parent is its nearest ancestor that is also a joint.
  std::unordered_map<int, int> jointOfNode;  // node -> joint index (before sorting)
  std::vector<int> skinJoints;               // skin joint slot -> joint index
  if (doc.contains("skins") && !doc["skins"].empty()) {
    const json& skin = doc["skins"][0];
    std::vector<int> jn;
    for (const auto& j : skin.value("joints", json::array())) jn.push_back(j.get<int>());
    std::vector<float> ibm;
    int comps = 0;
    if (skin.contains("inverseBindMatrices") && !rd.floats(skin["inverseBindMatrices"].get<int>(), ibm, comps)) {
      error = rd.error;
      return false;
    }
    Skeleton& sk = out.skeleton;
    // The skeleton is turned upright by a root joint above it. A file this
    // engine wrote has that joint already (as its only root): it is used again.
    std::vector<int> rootJoints;
    {
      std::unordered_map<int, char> isJoint;
      for (int n : jn) isJoint[n] = 1;
      for (int n : jn) {
        if (n < 0 || n >= static_cast<int>(nodeCount)) continue;
        int p = parentOf[static_cast<size_t>(n)];
        while (p >= 0 && !isJoint.count(p)) p = parentOf[static_cast<size_t>(p)];
        if (p < 0) rootJoints.push_back(n);
      }
    }
    const bool reuseAxis = rootJoints.size() == 1 && nodes[static_cast<size_t>(rootJoints[0])].value("name", std::string()) == "__yup";
    const int base = reuseAxis ? 0 : 1;
    const int axis = reuseAxis ? -1 : sk.add("__yup", -1, yUpToEngine());
    for (size_t k = 0; k < jn.size(); ++k) jointOfNode[jn[k]] = static_cast<int>(k) + base;
    for (size_t k = 0; k < jn.size(); ++k) {
      const int node = jn[k];
      if (node < 0 || node >= static_cast<int>(nodeCount)) {
        error = "a skin names a node that isn't there";
        return false;
      }
      int p = parentOf[static_cast<size_t>(node)];
      while (p >= 0 && !jointOfNode.count(p)) p = parentOf[static_cast<size_t>(p)];
      Transform rest = nodeLocal(nodes[static_cast<size_t>(node)]);
      if (p < 0 && parentOf[static_cast<size_t>(node)] >= 0)  // a root joint under plain nodes: take their placement in
        rest = decompose(worldOf(parentOf[static_cast<size_t>(node)]) * rest.matrix());
      if (p < 0 && reuseAxis) rest = decompose(toEngine * rest.matrix());  // the upright joint, upright
      const int j = sk.add(nodes[static_cast<size_t>(node)].value("name", "joint" + std::to_string(k)), p >= 0 ? jointOfNode[p] : axis, rest);
      Mat4 inv;
      if (ibm.size() >= (k + 1) * 16)
        for (size_t i = 0; i < 16; ++i) inv.m[i] = ibm[k * 16 + i];
      // Vertices come in glTF space; the axis joint turns them upright, so bind against that.
      sk.joint(j).inverseBind = inv * inverse(toEngine);
    }
    if (axis >= 0) sk.joint(axis).inverseBind = inverse(toEngine);
    if (ibm.empty()) sk.bindRest();
    const std::vector<int> remap = sk.sortParentsFirst();
    for (auto& [node, j] : jointOfNode) j = remap[static_cast<size_t>(j)];
    for (size_t k = 0; k < jn.size(); ++k) skinJoints.push_back(jointOfNode[jn[k]]);
  }

  // Meshes, placed by the nodes that carry them (scene 0, or every root when there are no scenes).
  std::vector<int> roots;
  if (doc.contains("scenes") && !doc["scenes"].empty()) {
    const size_t sc = doc.value("scene", size_t{0}) < doc["scenes"].size() ? doc.value("scene", size_t{0}) : 0;
    for (const auto& n : doc["scenes"][sc].value("nodes", json::array())) roots.push_back(n.get<int>());
  } else {
    for (size_t i = 0; i < nodeCount; ++i)
      if (parentOf[i] < 0) roots.push_back(static_cast<int>(i));
  }
  std::vector<char> seen(nodeCount, 0);
  std::function<bool(int)> visit = [&](int ni) -> bool {
    if (ni < 0 || ni >= static_cast<int>(nodeCount) || seen[static_cast<size_t>(ni)]) return true;
    seen[static_cast<size_t>(ni)] = 1;
    const json& n = nodes[static_cast<size_t>(ni)];
    if (n.contains("mesh")) {
      const json& mesh = doc["meshes"][n["mesh"].get<size_t>()];
      const bool skinned = n.contains("skin") && !skinJoints.empty();
      const Mat4 place = skinned ? toEngine : toEngine * worldOf(ni);
      const Mat4 nplace = normalMatrix(place);
      int prim = 0;
      for (const auto& p : mesh.value("primitives", json::array())) {
        if (p.value("mode", 4) != 4) continue;  // triangles only
        const json& at = p["attributes"];
        if (!at.contains("POSITION")) continue;
        MeshPart part;
        part.name = mesh.value("name", std::string("mesh")) + (prim ? "_" + std::to_string(prim) : "");
        ++prim;
        part.material = p.value("material", -1);
        part.skinned = skinned;
        std::vector<float> pos, nrm, uv, col, jnt, wgt;
        int cp = 0, cn = 0, cu = 0, cc = 0, cj = 0, cw = 0;
        if (!rd.floats(at["POSITION"].get<int>(), pos, cp) || (at.contains("NORMAL") && !rd.floats(at["NORMAL"].get<int>(), nrm, cn)) ||
            (at.contains("TEXCOORD_0") && !rd.floats(at["TEXCOORD_0"].get<int>(), uv, cu)) ||
            (at.contains("COLOR_0") && !rd.floats(at["COLOR_0"].get<int>(), col, cc)) ||
            (skinned && at.contains("JOINTS_0") && !rd.floats(at["JOINTS_0"].get<int>(), jnt, cj)) ||
            (skinned && at.contains("WEIGHTS_0") && !rd.floats(at["WEIGHTS_0"].get<int>(), wgt, cw))) {
          error = rd.error;
          return false;
        }
        const size_t nv = cp ? pos.size() / static_cast<size_t>(cp) : 0;
        part.vertices.resize(nv);
        for (size_t v = 0; v < nv; ++v) {
          MeshVertexFull& o = part.vertices[v];
          const float g[3] = {pos[v * 3], pos[v * 3 + 1], pos[v * 3 + 2]};
          transformPoint(place, g, o.pos);
          if (cn == 3) {
            const float gn[3] = {nrm[v * 3], nrm[v * 3 + 1], nrm[v * 3 + 2]};
            float t[3];
            transformDirection(nplace, gn, t);
            const float len = std::sqrt(t[0] * t[0] + t[1] * t[1] + t[2] * t[2]);
            for (int i = 0; i < 3; ++i) o.normal[i] = len > 0 ? t[i] / len : 0;
          }
          if (cu == 2) {
            o.uv[0] = uv[v * 2];
            o.uv[1] = uv[v * 2 + 1];
          }
          if (cc >= 3)
            for (int i = 0; i < 4; ++i)
              o.color[i] = static_cast<uint8_t>(std::clamp(std::lround((i < cc ? col[v * static_cast<size_t>(cc) + static_cast<size_t>(i)] : 1.0f) * 255.0f), 0L, 255L));
          if (cj == 4 && cw == 4)
            for (int i = 0; i < 4; ++i) {
              const int slot = static_cast<int>(jnt[v * 4 + static_cast<size_t>(i)]);
              o.joints[i] = static_cast<uint16_t>(slot >= 0 && slot < static_cast<int>(skinJoints.size()) ? skinJoints[static_cast<size_t>(slot)] : 0);
              o.weights[i] = wgt[v * 4 + static_cast<size_t>(i)];
            }
        }
        if (p.contains("indices")) {
          std::vector<float> idx;
          int ci = 0;
          if (!rd.floats(p["indices"].get<int>(), idx, ci)) {
            error = rd.error;
            return false;
          }
          part.indices.reserve(idx.size());
          for (float f : idx) {
            const uint32_t k = static_cast<uint32_t>(f);
            if (k >= nv) {
              error = part.name + ": an index past the last vertex";
              return false;
            }
            part.indices.push_back(k);
          }
        } else {
          for (uint32_t k = 0; k < nv; ++k) part.indices.push_back(k);
        }
        // A mirroring node turns triangles inside out: put them right.
        {
          const Mat4& m = place;
          const float det = m.at(0, 0) * (m.at(1, 1) * m.at(2, 2) - m.at(2, 1) * m.at(1, 2)) -
                            m.at(0, 1) * (m.at(1, 0) * m.at(2, 2) - m.at(2, 0) * m.at(1, 2)) +
                            m.at(0, 2) * (m.at(1, 0) * m.at(2, 1) - m.at(2, 0) * m.at(1, 1));
          if (det < 0)
            for (size_t t = 0; t + 2 < part.indices.size(); t += 3) std::swap(part.indices[t + 1], part.indices[t + 2]);
        }
        out.meshes.push_back(std::move(part));
      }
    }
    for (const auto& c : n.value("children", json::array()))
      if (!visit(c.get<int>())) return false;
    return true;
  };
  for (int r : roots)
    if (!visit(r)) return false;

  // Animations on the skeleton's joints.
  for (const auto& an : doc.value("animations", json::array())) {
    AnimationClip clip;
    clip.name = an.value("name", "clip" + std::to_string(out.clips.size()));
    const json& samplers = an.contains("samplers") ? an["samplers"] : json::array();
    for (const auto& ch : an.value("channels", json::array())) {
      const json& tg = ch["target"];
      if (!tg.contains("node")) continue;
      auto it = jointOfNode.find(tg["node"].get<int>());
      if (it == jointOfNode.end()) continue;  // moves a plain node: not kept
      const std::string path = tg.value("path", std::string());
      Channel c;
      c.joint = it->second;
      if (path == "translation") c.path = ChannelPath::Translation;
      else if (path == "rotation") c.path = ChannelPath::Rotation;
      else if (path == "scale") c.path = ChannelPath::Scale;
      else continue;  // weights (morph targets)
      const size_t si = ch.value("sampler", size_t{0});
      if (si >= samplers.size()) continue;
      const json& s = samplers[si];
      const std::string interp = s.value("interpolation", std::string("LINEAR"));
      c.interpolation = interp == "STEP" ? Interpolation::Step : Interpolation::Linear;
      int ci = 0, co = 0;
      std::vector<float> outv;
      if (!rd.floats(s["input"].get<int>(), c.times, ci) || !rd.floats(s["output"].get<int>(), outv, co)) {
        error = rd.error;
        return false;
      }
      if (interp == "CUBICSPLINE") {  // in-tangent, value, out-tangent per key: keep the values
        const size_t w = static_cast<size_t>(co);
        for (size_t k = 0; k < c.times.size(); ++k)
          for (size_t i = 0; i < w; ++i) c.values.push_back(outv[(k * 3 + 1) * w + i]);
      } else {
        c.values = std::move(outv);
      }
      clip.channels.push_back(std::move(c));
    }
    clip.computeDuration();
    out.clips.push_back(std::move(clip));
  }
  if (out.meshes.empty() && out.skeleton.size() == 0) {
    error = "nothing to read: no meshes and no skeleton";
    return false;
  }
  return true;
}

bool readGltf(const std::string& path, ModelAsset& out, std::string& error) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    error = path + ": cannot open";
    return false;
  }
  const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  if (!readGltfBytes(bytes, std::filesystem::path(path).parent_path().string(), out, error)) {
    error = path + ": " + error;
    return false;
  }
  return true;
}

// ---------------------------------------------------------------------------
// Writing
// ---------------------------------------------------------------------------
namespace {
struct Writer {
  std::vector<uint8_t> bin;
  json views = json::array(), accessors = json::array();

  int add(const void* data, size_t bytes, int componentType, size_t count, const char* type, int target, bool normalized = false,
          const std::vector<float>* mn = nullptr, const std::vector<float>* mx = nullptr) {
    while (bin.size() % 4) bin.push_back(0);
    json v = {{"buffer", 0}, {"byteOffset", bin.size()}, {"byteLength", bytes}};
    if (target) v["target"] = target;
    const auto* p = static_cast<const uint8_t*>(data);
    bin.insert(bin.end(), p, p + bytes);
    views.push_back(v);
    json a = {{"bufferView", views.size() - 1}, {"componentType", componentType}, {"count", count}, {"type", type}};
    if (normalized) a["normalized"] = true;
    if (mn) a["min"] = *mn;
    if (mx) a["max"] = *mx;
    accessors.push_back(a);
    return static_cast<int>(accessors.size()) - 1;
  }
};
}  // namespace

std::vector<uint8_t> writeGlb(const ModelAsset& model) {
  Writer w;
  json doc;
  doc["asset"] = {{"version", "2.0"}, {"generator", "Pioneer Engine"}};
  json nodes = json::array(), meshes = json::array(), materials = json::array();
  for (const Material& m : model.materials) {
    json j = {{"name", m.name},
              {"pbrMetallicRoughness",
               {{"baseColorFactor", {m.baseColor[0], m.baseColor[1], m.baseColor[2], m.baseColor[3]}},
                {"metallicFactor", m.metallic},
                {"roughnessFactor", m.roughness}}}};
    if (m.emissive[0] > 0 || m.emissive[1] > 0 || m.emissive[2] > 0) j["emissiveFactor"] = {m.emissive[0], m.emissive[1], m.emissive[2]};
    if (m.alpha == Material::Alpha::Mask) {
      j["alphaMode"] = "MASK";
      j["alphaCutoff"] = m.alphaCutoff;
    } else if (m.alpha == Material::Alpha::Blend) {
      j["alphaMode"] = "BLEND";
    }
    if (m.doubleSided) j["doubleSided"] = true;
    if (m.team) j["extras"] = {{"pioneer_team", true}};
    materials.push_back(j);
  }
  // Node 0 lays the engine's z-up model down into glTF's y-up.
  const Quat down = quatAxisAngle(1, 0, 0, -1.5707963f);
  nodes.push_back({{"name", "pioneer"}, {"rotation", {down.x, down.y, down.z, down.w}}, {"children", json::array()}});
  // The skeleton's joints, under node 0.
  const Skeleton& sk = model.skeleton;
  std::vector<int> nodeOfJoint(sk.size(), -1);
  for (size_t i = 0; i < sk.size(); ++i) {
    const Joint& j = sk.joint(static_cast<int>(i));
    json n = {{"name", j.name},
              {"translation", {j.rest.t[0], j.rest.t[1], j.rest.t[2]}},
              {"rotation", {j.rest.r.x, j.rest.r.y, j.rest.r.z, j.rest.r.w}},
              {"scale", {j.rest.s[0], j.rest.s[1], j.rest.s[2]}}};
    nodeOfJoint[i] = static_cast<int>(nodes.size());
    nodes.push_back(n);
    const int parentNode = j.parent >= 0 ? nodeOfJoint[static_cast<size_t>(j.parent)] : -1;
    json& parent = nodes[static_cast<size_t>(parentNode >= 0 ? parentNode : 0)];
    if (!parent.contains("children")) parent["children"] = json::array();
    parent["children"].push_back(nodeOfJoint[i]);
  }
  // A joint index (engine) to a skin slot (glTF). Skinned vertices are written
  // in glTF's own (y-up) bind space, so the inverse binds take the turn back.
  std::vector<int> slotOfJoint(sk.size(), 0);
  json skinJoints = json::array();
  std::vector<float> ibm;
  const Mat4 laid = Transform{{0, 0, 0}, down, {1, 1, 1}}.matrix(), up = inverse(laid);
  for (size_t i = 0; i < sk.size(); ++i) {
    slotOfJoint[i] = static_cast<int>(skinJoints.size());
    skinJoints.push_back(nodeOfJoint[i]);
    const Mat4 inv = sk.joint(static_cast<int>(i)).inverseBind * up;
    ibm.insert(ibm.end(), std::begin(inv.m), std::end(inv.m));
  }
  const bool skinned = !skinJoints.empty();
  for (const MeshPart& part : model.meshes) {
    const size_t n = part.vertices.size();
    std::vector<float> pos(n * 3), nrm(n * 3), uv(n * 2), wgt;
    std::vector<uint8_t> col(n * 4);
    std::vector<uint16_t> jnt;
    std::vector<float> mn = {1e30f, 1e30f, 1e30f}, mx = {-1e30f, -1e30f, -1e30f};
    const bool bound = part.skinned && skinned;
    for (size_t v = 0; v < n; ++v) {
      const MeshVertexFull& x = part.vertices[v];
      float pp[3], nn[3];
      if (bound) {  // into glTF's y-up bind space
        transformPoint(laid, x.pos, pp);
        transformDirection(laid, x.normal, nn);
      } else {
        std::copy(x.pos, x.pos + 3, pp);
        std::copy(x.normal, x.normal + 3, nn);
      }
      for (int i = 0; i < 3; ++i) {
        pos[v * 3 + static_cast<size_t>(i)] = pp[i];
        nrm[v * 3 + static_cast<size_t>(i)] = nn[i];
        mn[static_cast<size_t>(i)] = std::min(mn[static_cast<size_t>(i)], pp[i]);
        mx[static_cast<size_t>(i)] = std::max(mx[static_cast<size_t>(i)], pp[i]);
      }
      uv[v * 2] = x.uv[0];
      uv[v * 2 + 1] = x.uv[1];
      for (int i = 0; i < 4; ++i) col[v * 4 + static_cast<size_t>(i)] = x.color[i];
      if (bound)
        for (int i = 0; i < 4; ++i) {
          jnt.push_back(static_cast<uint16_t>(x.joints[i] < slotOfJoint.size() ? slotOfJoint[x.joints[i]] : 0));
          wgt.push_back(x.weights[i]);
        }
    }
    if (!n) continue;
    json attrs = {{"POSITION", w.add(pos.data(), pos.size() * 4, 5126, n, "VEC3", 34962, false, &mn, &mx)},
                  {"NORMAL", w.add(nrm.data(), nrm.size() * 4, 5126, n, "VEC3", 34962)},
                  {"TEXCOORD_0", w.add(uv.data(), uv.size() * 4, 5126, n, "VEC2", 34962)},
                  {"COLOR_0", w.add(col.data(), col.size(), 5121, n, "VEC4", 34962, true)}};
    if (!jnt.empty()) {
      attrs["JOINTS_0"] = w.add(jnt.data(), jnt.size() * 2, 5123, n, "VEC4", 34962);
      attrs["WEIGHTS_0"] = w.add(wgt.data(), wgt.size() * 4, 5126, n, "VEC4", 34962);
    }
    json prim = {{"attributes", attrs}, {"indices", w.add(part.indices.data(), part.indices.size() * 4, 5125, part.indices.size(), "SCALAR", 34963)}};
    if (part.material >= 0 && part.material < static_cast<int>(model.materials.size())) prim["material"] = part.material;
    meshes.push_back({{"name", part.name}, {"primitives", json::array({prim})}});
    json node = {{"name", part.name}, {"mesh", meshes.size() - 1}};
    if (!jnt.empty()) node["skin"] = 0;
    nodes[0]["children"].push_back(nodes.size());
    nodes.push_back(node);
  }
  doc["nodes"] = nodes;
  doc["scenes"] = json::array({{{"nodes", {0}}}});
  doc["scene"] = 0;
  if (!meshes.empty()) doc["meshes"] = meshes;
  if (!materials.empty()) doc["materials"] = materials;
  if (skinned) doc["skins"] = json::array({{{"joints", skinJoints}, {"inverseBindMatrices", w.add(ibm.data(), ibm.size() * 4, 5126, ibm.size() / 16, "MAT4", 0)}}});
  json anims = json::array();
  for (const AnimationClip& c : model.clips) {
    json samplers = json::array(), channels = json::array();
    for (const Channel& ch : c.channels) {
      if (ch.joint < 0 || ch.joint >= static_cast<int>(nodeOfJoint.size()) || nodeOfJoint[static_cast<size_t>(ch.joint)] < 0 || ch.times.empty()) continue;
      const int width = ch.path == ChannelPath::Rotation ? 4 : 3;
      std::vector<float> mn = {ch.times.front()}, mx = {ch.times.back()};
      const int in = w.add(ch.times.data(), ch.times.size() * 4, 5126, ch.times.size(), "SCALAR", 0, false, &mn, &mx);
      const int out = w.add(ch.values.data(), ch.values.size() * 4, 5126, ch.values.size() / static_cast<size_t>(width), width == 4 ? "VEC4" : "VEC3", 0);
      samplers.push_back({{"input", in}, {"output", out}, {"interpolation", ch.interpolation == Interpolation::Step ? "STEP" : "LINEAR"}});
      channels.push_back({{"sampler", samplers.size() - 1},
                          {"target", {{"node", nodeOfJoint[static_cast<size_t>(ch.joint)]},
                                      {"path", ch.path == ChannelPath::Translation ? "translation" : ch.path == ChannelPath::Rotation ? "rotation" : "scale"}}}});
    }
    if (!channels.empty()) anims.push_back({{"name", c.name}, {"samplers", samplers}, {"channels", channels}});
  }
  if (!anims.empty()) doc["animations"] = anims;
  while (w.bin.size() % 4) w.bin.push_back(0);
  doc["buffers"] = json::array({{{"byteLength", w.bin.size()}}});
  doc["bufferViews"] = w.views;
  doc["accessors"] = w.accessors;
  std::string text = doc.dump();
  while (text.size() % 4) text.push_back(' ');
  std::vector<uint8_t> out;
  auto put32 = [&](uint32_t v) {
    for (int i = 0; i < 4; ++i) out.push_back(static_cast<uint8_t>(v >> (8 * i)));
  };
  put32(kGlbMagic);
  put32(2);
  put32(static_cast<uint32_t>(12 + 8 + text.size() + 8 + w.bin.size()));
  put32(static_cast<uint32_t>(text.size()));
  put32(kChunkJson);
  out.insert(out.end(), text.begin(), text.end());
  put32(static_cast<uint32_t>(w.bin.size()));
  put32(kChunkBin);
  out.insert(out.end(), w.bin.begin(), w.bin.end());
  return out;
}

bool writeGlbFile(const std::string& path, const ModelAsset& model, std::string& error) {
  const std::vector<uint8_t> bytes = writeGlb(model);
  std::ofstream o(path, std::ios::binary);
  if (!o || !o.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()))) {
    error = path + ": cannot write";
    return false;
  }
  return true;
}

ModelAsset voxelModelAsset(const VoxelModel& model, const VoxelPalette& palette, float voxelTiles, float pivotX, float pivotY,
                           const std::string& name) {
  ModelAsset a;
  Material plain;
  plain.name = name + "_paint";
  plain.roughness = 0.85f;
  Material team = plain;
  team.name = name + "_team";
  team.team = true;
  a.materials = {plain, team};
  MeshPart parts[2];
  parts[0].name = name;
  parts[0].material = 0;
  parts[1].name = name + "_team";
  parts[1].material = 1;
  static const float kNormals[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
  std::map<std::tuple<int, int, int, int, uint32_t>, uint32_t> shared[2];  // shared corners within a part
  for (const MeshVertex& v : meshVoxels(model, palette)) {
    const int which = v.team ? 1 : 0;
    MeshPart& p = parts[which];
    // Crowded corners a little darker, as the game shades them.
    const float shade = 1.0f - 0.025f * static_cast<float>(v.occlusion);
    uint8_t c[4];
    if (v.team) {
      const uint8_t g = static_cast<uint8_t>(std::clamp(static_cast<float>(v.team) / 100.0f * 200.0f * shade, 0.0f, 255.0f));
      c[0] = c[1] = c[2] = g;
    } else {
      c[0] = static_cast<uint8_t>(v.r * shade);
      c[1] = static_cast<uint8_t>(v.g * shade);
      c[2] = static_cast<uint8_t>(v.b * shade);
    }
    c[3] = 255;
    const uint32_t rgba = static_cast<uint32_t>(c[0]) | static_cast<uint32_t>(c[1]) << 8 | static_cast<uint32_t>(c[2]) << 16;
    const auto key = std::make_tuple(static_cast<int>(std::lround(v.x * 2)), static_cast<int>(std::lround(v.y * 2)), static_cast<int>(std::lround(v.z * 2)),
                                     static_cast<int>(v.normal), rgba);
    auto it = shared[which].find(key);
    if (it != shared[which].end()) {
      p.indices.push_back(it->second);
      continue;
    }
    MeshVertexFull o;
    o.pos[0] = (v.x - pivotX) * voxelTiles;
    o.pos[1] = (v.y - pivotY) * voxelTiles;
    o.pos[2] = v.z * voxelTiles;
    for (int i = 0; i < 3; ++i) o.normal[i] = kNormals[v.normal % 6][i];
    std::copy(c, c + 4, o.color);
    const uint32_t idx = static_cast<uint32_t>(p.vertices.size());
    p.vertices.push_back(o);
    p.indices.push_back(idx);
    shared[which][key] = idx;
  }
  for (auto& p : parts)
    if (!p.vertices.empty()) a.meshes.push_back(std::move(p));
  return a;
}

}  // namespace pioneer
