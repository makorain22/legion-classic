// Pioneer Engine - skeletons and skeletal animation. See skeleton.hpp.
#include "pioneer/assets/skeleton.hpp"

#include <algorithm>
#include <cmath>
#include <functional>

namespace pioneer {

Quat quatAxisAngle(float ax, float ay, float az, float radians) {
  const float len = std::sqrt(ax * ax + ay * ay + az * az);
  if (len <= 0) return {};
  const float s = std::sin(radians * 0.5f) / len;
  return {ax * s, ay * s, az * s, std::cos(radians * 0.5f)};
}

Quat quatMul(const Quat& a, const Quat& b) {
  return {a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y, a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
          a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w, a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
}

Quat quatNormalize(const Quat& q) {
  const float n = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
  if (n <= 0) return {};
  return {q.x / n, q.y / n, q.z / n, q.w / n};
}

Quat quatSlerp(const Quat& a, const Quat& bIn, float t) {
  Quat b = bIn;
  float d = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
  if (d < 0) {  // the short way round
    d = -d;
    b = {-b.x, -b.y, -b.z, -b.w};
  }
  if (d > 0.9995f)  // nearly the same: a straight line is as good and stable
    return quatNormalize({a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t});
  const float th = std::acos(std::clamp(d, -1.0f, 1.0f));
  const float s = std::sin(th);
  const float ka = std::sin((1 - t) * th) / s, kb = std::sin(t * th) / s;
  return {a.x * ka + b.x * kb, a.y * ka + b.y * kb, a.z * ka + b.z * kb, a.w * ka + b.w * kb};
}

void transformPoint(const Mat4& m, const float in[3], float out[3]) {
  for (int r = 0; r < 3; ++r) out[r] = m.at(r, 0) * in[0] + m.at(r, 1) * in[1] + m.at(r, 2) * in[2] + m.at(r, 3);
}
void transformDirection(const Mat4& m, const float in[3], float out[3]) {
  for (int r = 0; r < 3; ++r) out[r] = m.at(r, 0) * in[0] + m.at(r, 1) * in[1] + m.at(r, 2) * in[2];
}

Mat4 Transform::matrix() const {
  const Quat q = quatNormalize(r);
  const float xx = q.x * q.x, yy = q.y * q.y, zz = q.z * q.z;
  const float xy = q.x * q.y, xz = q.x * q.z, yz = q.y * q.z, wx = q.w * q.x, wy = q.w * q.y, wz = q.w * q.z;
  Mat4 m;
  m.at(0, 0) = (1 - 2 * (yy + zz)) * s[0];
  m.at(1, 0) = 2 * (xy + wz) * s[0];
  m.at(2, 0) = 2 * (xz - wy) * s[0];
  m.at(0, 1) = 2 * (xy - wz) * s[1];
  m.at(1, 1) = (1 - 2 * (xx + zz)) * s[1];
  m.at(2, 1) = 2 * (yz + wx) * s[1];
  m.at(0, 2) = 2 * (xz + wy) * s[2];
  m.at(1, 2) = 2 * (yz - wx) * s[2];
  m.at(2, 2) = (1 - 2 * (xx + yy)) * s[2];
  m.at(0, 3) = t[0];
  m.at(1, 3) = t[1];
  m.at(2, 3) = t[2];
  return m;
}

Transform decompose(const Mat4& m) {
  Transform tr;
  for (int i = 0; i < 3; ++i) tr.t[i] = m.at(i, 3);
  float col[3][3];
  for (int c = 0; c < 3; ++c) {
    const float len = std::sqrt(m.at(0, c) * m.at(0, c) + m.at(1, c) * m.at(1, c) + m.at(2, c) * m.at(2, c));
    tr.s[c] = len;
    for (int r = 0; r < 3; ++r) col[c][r] = len > 0 ? m.at(r, c) / len : 0.0f;
  }
  // A mirrored basis: flip one scale so the rest is a rotation.
  const float det = col[0][0] * (col[1][1] * col[2][2] - col[2][1] * col[1][2]) -
                    col[1][0] * (col[0][1] * col[2][2] - col[2][1] * col[0][2]) +
                    col[2][0] * (col[0][1] * col[1][2] - col[1][1] * col[0][2]);
  if (det < 0) {
    tr.s[0] = -tr.s[0];
    for (int r = 0; r < 3; ++r) col[0][r] = -col[0][r];
  }
  // Rotation matrix to quaternion (R[r][c] = col[c][r]).
  const float r00 = col[0][0], r11 = col[1][1], r22 = col[2][2];
  const float trace = r00 + r11 + r22;
  Quat q;
  if (trace > 0) {
    const float s = std::sqrt(trace + 1.0f) * 2;
    q = {(col[1][2] - col[2][1]) / s, (col[2][0] - col[0][2]) / s, (col[0][1] - col[1][0]) / s, 0.25f * s};
  } else if (r00 > r11 && r00 > r22) {
    const float s = std::sqrt(1.0f + r00 - r11 - r22) * 2;
    q = {0.25f * s, (col[1][0] + col[0][1]) / s, (col[2][0] + col[0][2]) / s, (col[1][2] - col[2][1]) / s};
  } else if (r11 > r22) {
    const float s = std::sqrt(1.0f + r11 - r00 - r22) * 2;
    q = {(col[1][0] + col[0][1]) / s, 0.25f * s, (col[2][1] + col[1][2]) / s, (col[2][0] - col[0][2]) / s};
  } else {
    const float s = std::sqrt(1.0f + r22 - r00 - r11) * 2;
    q = {(col[2][0] + col[0][2]) / s, (col[2][1] + col[1][2]) / s, 0.25f * s, (col[0][1] - col[1][0]) / s};
  }
  tr.r = quatNormalize(q);
  return tr;
}

Transform blend(const Transform& a, const Transform& b, float k) {
  Transform o;
  for (int i = 0; i < 3; ++i) {
    o.t[i] = a.t[i] + (b.t[i] - a.t[i]) * k;
    o.s[i] = a.s[i] + (b.s[i] - a.s[i]) * k;
  }
  o.r = quatSlerp(a.r, b.r, k);
  return o;
}

int Skeleton::add(const std::string& name, int parent, const Transform& rest) {
  Joint j;
  j.name = name;
  j.parent = parent;
  j.rest = rest;
  joints_.push_back(j);
  return static_cast<int>(joints_.size()) - 1;
}

int Skeleton::find(const std::string& name) const {
  for (size_t i = 0; i < joints_.size(); ++i)
    if (joints_[i].name == name) return static_cast<int>(i);
  return -1;
}

void Skeleton::bindRest() {
  const std::vector<Mat4> g = globalMatrices(*this, restPose(*this));
  for (size_t i = 0; i < joints_.size(); ++i) joints_[i].inverseBind = inverse(g[i]);
}

std::vector<int> Skeleton::sortParentsFirst() {
  const size_t n = joints_.size();
  std::vector<int> order;
  std::vector<char> placed(n, 0);
  std::function<void(int)> place = [&](int i) {
    if (placed[static_cast<size_t>(i)]) return;
    placed[static_cast<size_t>(i)] = 1;  // guards a loop in a damaged file
    const int p = joints_[static_cast<size_t>(i)].parent;
    if (p >= 0 && p < static_cast<int>(n)) place(p);
    order.push_back(i);
  };
  for (size_t i = 0; i < n; ++i) place(static_cast<int>(i));
  std::vector<int> oldToNew(n);
  for (size_t k = 0; k < n; ++k) oldToNew[static_cast<size_t>(order[k])] = static_cast<int>(k);
  std::vector<Joint> sorted;
  sorted.reserve(n);
  for (int i : order) {
    Joint j = joints_[static_cast<size_t>(i)];
    j.parent = j.parent >= 0 && j.parent < static_cast<int>(n) ? oldToNew[static_cast<size_t>(j.parent)] : -1;
    sorted.push_back(std::move(j));
  }
  joints_ = std::move(sorted);
  return oldToNew;
}

Pose restPose(const Skeleton& s) {
  Pose p;
  p.reserve(s.size());
  for (const Joint& j : s.joints()) p.push_back(j.rest);
  return p;
}

std::vector<Mat4> globalMatrices(const Skeleton& s, const Pose& p) {
  std::vector<Mat4> g(s.size());
  for (size_t i = 0; i < s.size(); ++i) {
    const Mat4 local = (i < p.size() ? p[i] : s.joint(static_cast<int>(i)).rest).matrix();
    const int parent = s.joint(static_cast<int>(i)).parent;
    g[i] = parent >= 0 ? g[static_cast<size_t>(parent)] * local : local;
  }
  return g;
}

std::vector<Mat4> skinningMatrices(const Skeleton& s, const Pose& p) {
  std::vector<Mat4> g = globalMatrices(s, p);
  for (size_t i = 0; i < g.size(); ++i) g[i] = g[i] * s.joint(static_cast<int>(i)).inverseBind;
  return g;
}

Pose blend(const Pose& a, const Pose& b, float k, const std::vector<float>* mask) {
  Pose o(a.size());
  for (size_t i = 0; i < a.size(); ++i) {
    const float w = k * (mask && i < mask->size() ? (*mask)[i] : 1.0f);
    o[i] = i < b.size() ? blend(a[i], b[i], w) : a[i];
  }
  return o;
}

std::vector<float> maskBelow(const Skeleton& s, int joint) {
  std::vector<float> m(s.size(), 0.0f);
  for (size_t i = 0; i < s.size(); ++i) {
    // Parents come first, so a joint is under `joint` if it is it or its parent is.
    const int p = s.joint(static_cast<int>(i)).parent;
    if (static_cast<int>(i) == joint || (p >= 0 && m[static_cast<size_t>(p)] > 0)) m[i] = 1.0f;
  }
  return m;
}

void AnimationClip::computeDuration() {
  duration = 0;
  for (const Channel& c : channels)
    if (!c.times.empty()) duration = std::max(duration, c.times.back());
}

void sample(const AnimationClip& clip, float t, const Pose& base, Pose& out) {
  out = base;
  if (clip.duration > 0) t = clip.loop ? std::fmod(std::fmod(t, clip.duration) + clip.duration, clip.duration) : std::clamp(t, 0.0f, clip.duration);
  for (const Channel& c : clip.channels) {
    if (c.joint < 0 || c.joint >= static_cast<int>(out.size()) || c.times.empty()) continue;
    const size_t width = c.path == ChannelPath::Rotation ? 4 : 3;
    if (c.values.size() < c.times.size() * width) continue;
    // The key at or before t, and the one after it.
    const auto it = std::upper_bound(c.times.begin(), c.times.end(), t);
    size_t k1 = static_cast<size_t>(it - c.times.begin());
    size_t k0 = k1 == 0 ? 0 : k1 - 1;
    if (k1 >= c.times.size()) k1 = c.times.size() - 1;
    float f = 0;
    if (c.interpolation == Interpolation::Linear && k1 != k0) f = (t - c.times[k0]) / std::max(1e-6f, c.times[k1] - c.times[k0]);
    const float* a = &c.values[k0 * width];
    const float* b = &c.values[k1 * width];
    Transform& tr = out[static_cast<size_t>(c.joint)];
    if (c.path == ChannelPath::Rotation) {
      tr.r = quatSlerp(Quat{a[0], a[1], a[2], a[3]}, Quat{b[0], b[1], b[2], b[3]}, f);
    } else {
      float* dst = c.path == ChannelPath::Translation ? tr.t : tr.s;
      for (int i = 0; i < 3; ++i) dst[i] = a[i] + (b[i] - a[i]) * f;
    }
  }
}

void skinVertex(const std::vector<Mat4>& skin, const float pos[3], const float nrm[3], const uint16_t joints[4],
                const float weights[4], float outPos[3], float outNrm[3]) {
  float p[3] = {0, 0, 0}, n[3] = {0, 0, 0};
  float total = 0;
  for (int k = 0; k < 4; ++k) {
    if (weights[k] <= 0 || joints[k] >= skin.size()) continue;
    float tp[3], tn[3];
    transformPoint(skin[joints[k]], pos, tp);
    transformDirection(skin[joints[k]], nrm, tn);
    for (int i = 0; i < 3; ++i) {
      p[i] += tp[i] * weights[k];
      n[i] += tn[i] * weights[k];
    }
    total += weights[k];
  }
  if (total <= 0) {
    for (int i = 0; i < 3; ++i) {
      outPos[i] = pos[i];
      outNrm[i] = nrm[i];
    }
    return;
  }
  const float len = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
  for (int i = 0; i < 3; ++i) {
    outPos[i] = p[i] / total;
    outNrm[i] = len > 0 ? n[i] / len : nrm[i];
  }
}

}  // namespace pioneer
