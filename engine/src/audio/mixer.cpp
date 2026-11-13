// Pioneer Engine - the software mixer. See audio.hpp.
#include "pioneer/audio/audio.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

#define STB_VORBIS_HEADER_ONLY
#include "stb_vorbis.c"

namespace pioneer::audio {

namespace {
constexpr float kPi = 3.14159265f;

float lowpassCoeff(float cutoff, int rate) {
  if (cutoff >= 0.45f * static_cast<float>(rate)) return 1.0f;
  return 1.0f - std::exp(-2.0f * kPi * cutoff / static_cast<float>(rate));
}
// Equal-power pan; the centre is full level on both sides.
void panGains(float pan, float& l, float& r) {
  const float a = (std::clamp(pan, -1.0f, 1.0f) + 1.0f) * kPi * 0.25f;
  l = std::min(1.0f, std::cos(a) * 1.41421356f);
  r = std::min(1.0f, std::sin(a) * 1.41421356f);
}
}  // namespace

const char* busName(Bus b) {
  switch (b) {
    case Bus::Sfx: return "sfx";
    case Bus::Voice: return "voice";
    case Bus::Music: return "music";
    case Bus::Ambience: return "ambience";
    case Bus::Ui: return "ui";
    default: return "?";
  }
}

Hearing hear(const Emitter& e, const Listener& l) {
  Hearing h;
  if (!e.positional) return h;
  const float dx = e.x - l.x, dy = e.y - l.y;
  const float d = std::sqrt(dx * dx + dy * dy);
  // The near zone: what is on screen around the centre is heard in full.
  const float inner = std::max(2.0f, l.viewTiles * 0.4f);
  const float dd = std::max(0.0f, d - inner);
  const float range = std::max(0.5f, e.range);
  const float k = dd / range;
  h.gain = 1.0f / (1.0f + k * k);
  // Lifting the camera lifts the ear.
  const float z = std::clamp(l.zoom, 0.25f, 4.0f);
  h.gain *= z >= 2.0f ? 1.0f : z >= 1.0f ? 0.88f + 0.12f * (z - 1.0f) : 0.7f + 0.18f * (z - 0.5f) / 0.5f;
  // Distance takes the top off and adds the hall.
  h.cutoff = std::clamp(18000.0f / (1.0f + 1.6f * k), 350.0f, 18000.0f);
  h.wet = std::clamp(0.45f * (1.0f - h.gain), 0.0f, 0.45f);
  // Left and right by the sound's place across the screen (isometric: x - y).
  const float across = ((e.x - e.y) - (l.x - l.y)) * 0.70710678f;
  h.pan = std::clamp(across / std::max(4.0f, l.viewTiles * 1.3f), -1.0f, 1.0f) * 0.85f;
  return h;
}

// ---------------------------------------------------------------------------
// Voices
// ---------------------------------------------------------------------------
struct Mixer::Voice {
  bool active = false;
  VoiceId id = 0;
  int sound = -1;
  Bus bus = Bus::Sfx;
  double pos = 0, step = 1;
  bool loop = false;
  int priority = 0;
  float volume = 1.0f;
  float reverb = 0.15f;
  Emitter at;
  // Envelope: fades in, fades out, volume changes.
  float env = 1.0f, envTarget = 1.0f, envStep = 0.0f;
  bool stopping = false;
  // Smoothed per block.
  float gl = 0, gr = 0, wet = 0, lpA = 1;
  bool primed = false;
  float lp[2] = {0, 0};
  float audibility = 1.0f;  // last positional gain, for stealing
  uint64_t started = 0;
};

// ---------------------------------------------------------------------------
// Music decks: Ogg Vorbis streamed from memory
// ---------------------------------------------------------------------------
struct Mixer::Deck {
  std::shared_ptr<const std::vector<uint8_t>> bytes;
  stb_vorbis* v = nullptr;
  int srcRate = 48000;
  int64_t totalFrames = 0, readFrames = 0;
  float gain = 0, target = 1, step = 0;
  bool leaving = false;  // fading out, then removed
  bool ended = false;
  // Resampling state.
  std::vector<float> chunk;
  size_t chunkPos = 0, chunkLen = 0;
  double frac = 0;
  float prev[2] = {0, 0}, next[2] = {0, 0};
  bool havePrev = false;
  ~Deck() {
    if (v) stb_vorbis_close(v);
  }
  // Next source frame, or false at the end.
  bool pull(float& l, float& r) {
    if (chunkPos >= chunkLen) {
      if (!v) return false;
      chunk.resize(4096 * 2);
      const int n = stb_vorbis_get_samples_float_interleaved(v, 2, chunk.data(), static_cast<int>(chunk.size()));
      if (n <= 0) return false;
      chunkLen = static_cast<size_t>(n);
      chunkPos = 0;
      readFrames += n;
    }
    l = chunk[chunkPos * 2];
    r = chunk[chunkPos * 2 + 1];
    ++chunkPos;
    return true;
  }
};

// ---------------------------------------------------------------------------
// A large hall: Freeverb-style combs and allpasses after a short pre-delay.
// ---------------------------------------------------------------------------
struct Mixer::Reverb {
  struct Comb {
    std::vector<float> buf;
    size_t i = 0;
    float store = 0;
    float process(float in, float feedback, float damp) {
      const float out = buf[i];
      store = out * (1.0f - damp) + store * damp;
      buf[i] = in + store * feedback;
      if (++i >= buf.size()) i = 0;
      return out;
    }
  };
  struct Allpass {
    std::vector<float> buf;
    size_t i = 0;
    float process(float in) {
      const float b = buf[i];
      const float out = -in + b;
      buf[i] = in + b * 0.5f;
      if (++i >= buf.size()) i = 0;
      return out;
    }
  };
  Comb comb[2][8];
  Allpass ap[2][4];
  std::vector<float> pre;
  size_t preI = 0;
  float feedback = 0.93f, damp = 0.28f;
  explicit Reverb(int rate) {
    static const int kComb[8] = {1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617};
    static const int kAp[4] = {556, 441, 341, 225};
    const float k = static_cast<float>(rate) / 44100.0f;
    for (int c = 0; c < 2; ++c) {
      const int spread = c ? 23 : 0;
      for (int j = 0; j < 8; ++j) comb[c][j].buf.assign(static_cast<size_t>((kComb[j] + spread) * k), 0.0f);
      for (int j = 0; j < 4; ++j) ap[c][j].buf.assign(static_cast<size_t>((kAp[j] + spread) * k), 0.0f);
    }
    pre.assign(static_cast<size_t>(0.028f * static_cast<float>(rate)), 0.0f);
  }
  void process(float in, float& l, float& r) {
    const float x = pre[preI];
    pre[preI] = in;
    if (++preI >= pre.size()) preI = 0;
    const float input = x * 0.015f;
    float o[2] = {0, 0};
    for (int c = 0; c < 2; ++c) {
      for (int j = 0; j < 8; ++j) o[c] += comb[c][j].process(input, feedback, damp);
      for (int j = 0; j < 4; ++j) o[c] = ap[c][j].process(o[c]);
    }
    l = o[0] * 3.0f;
    r = o[1] * 3.0f;
  }
};

// ---------------------------------------------------------------------------
Mixer::Mixer(int sampleRate, int maxVoices) : rate_(sampleRate), reverb_(std::make_unique<Reverb>(sampleRate)) {
  voices_.resize(static_cast<size_t>(std::max(4, maxVoices)));
  for (auto& v : voices_) v = std::make_unique<Voice>();
}
Mixer::~Mixer() = default;

int Mixer::addSound(Sound s) {
  std::lock_guard<std::mutex> lock(m_);
  sounds_.push_back(std::move(s));
  return static_cast<int>(sounds_.size()) - 1;
}
const Sound* Mixer::sound(int i) const {
  std::lock_guard<std::mutex> lock(m_);
  return i >= 0 && i < static_cast<int>(sounds_.size()) ? &sounds_[static_cast<size_t>(i)] : nullptr;
}
int Mixer::soundCount() const {
  std::lock_guard<std::mutex> lock(m_);
  return static_cast<int>(sounds_.size());
}

VoiceId Mixer::play(const PlayDesc& d) {
  std::lock_guard<std::mutex> lock(m_);
  if (d.sound < 0 || d.sound >= static_cast<int>(sounds_.size())) return 0;
  const Sound& s = sounds_[static_cast<size_t>(d.sound)];
  if (s.frames() < 2) return 0;
  const Hearing h = hear(d.at, listener_);
  const float audible = h.gain * d.volume;
  if (!d.loop && audible < 0.004f) return 0;  // too far away to matter
  // A free voice, or the least important one that matters less than this.
  Voice* slot = nullptr;
  for (auto& v : voices_)
    if (!v->active) {
      slot = v.get();
      break;
    }
  if (!slot) {
    float worst = 1e9f;
    const float mine = static_cast<float>(d.priority) + audible;
    for (auto& v : voices_) {
      if (v->bus == Bus::Music) continue;
      const float score = static_cast<float>(v->priority) + v->audibility * v->volume * (v->stopping ? 0.1f : 1.0f);
      if (score < worst) {
        worst = score;
        slot = v.get();
      }
    }
    if (!slot || worst >= mine) return 0;
  }
  Voice& v = *slot;
  v = Voice{};
  v.active = true;
  v.id = nextId_++;
  if (nextId_ == 0) nextId_ = 1;
  v.sound = d.sound;
  v.bus = d.bus;
  v.pos = 0;
  v.step = static_cast<double>(std::clamp(d.pitch, 0.25f, 4.0f)) * s.rate / rate_;
  v.loop = d.loop;
  v.priority = d.priority;
  v.volume = d.volume;
  v.reverb = d.reverb;
  v.at = d.at;
  v.audibility = h.gain;
  if (d.fadeIn > 0) {
    v.env = 0;
    v.envTarget = 1;
    v.envStep = 1.0f / (d.fadeIn * static_cast<float>(rate_));
  }
  return v.id;
}

void Mixer::stop(VoiceId id, float fade) {
  std::lock_guard<std::mutex> lock(m_);
  for (auto& v : voices_)
    if (v->active && v->id == id) {
      v->stopping = true;
      v->envTarget = 0;
      v->envStep = 1.0f / (std::max(0.002f, fade) * static_cast<float>(rate_));
    }
}

void Mixer::stopBus(Bus b, float fade) {
  std::lock_guard<std::mutex> lock(m_);
  for (auto& v : voices_)
    if (v->active && v->bus == b) {
      v->stopping = true;
      v->envTarget = 0;
      v->envStep = 1.0f / (std::max(0.002f, fade) * static_cast<float>(rate_));
    }
}

void Mixer::setVolume(VoiceId id, float volume, float fade) {
  std::lock_guard<std::mutex> lock(m_);
  for (auto& v : voices_)
    if (v->active && v->id == id && !v->stopping) {
      // The envelope carries the change so it glides.
      const float now = v->env * v->volume;
      v->volume = std::max(0.0f, volume);
      if (v->volume <= 0.0f) {
        v->env = 0;
        v->envTarget = 0;
        v->envStep = 0;
        v->volume = 0;
        continue;
      }
      v->env = now / v->volume;
      v->envTarget = 1;
      v->envStep = std::abs(1.0f - v->env) / (std::max(0.002f, fade) * static_cast<float>(rate_));
    }
}

void Mixer::setPosition(VoiceId id, float x, float y) {
  std::lock_guard<std::mutex> lock(m_);
  for (auto& v : voices_)
    if (v->active && v->id == id) {
      v->at.x = x;
      v->at.y = y;
    }
}

bool Mixer::playing(VoiceId id) const {
  std::lock_guard<std::mutex> lock(m_);
  for (const auto& v : voices_)
    if (v->active && v->id == id && !v->stopping) return true;
  return false;
}

int Mixer::activeVoices() const {
  std::lock_guard<std::mutex> lock(m_);
  int n = 0;
  for (const auto& v : voices_) n += v->active;
  return n;
}

void Mixer::setListener(const Listener& l) {
  std::lock_guard<std::mutex> lock(m_);
  listener_ = l;
}
Listener Mixer::listener() const {
  std::lock_guard<std::mutex> lock(m_);
  return listener_;
}
void Mixer::setBusVolume(Bus b, float v) {
  std::lock_guard<std::mutex> lock(m_);
  bus_[static_cast<int>(b)] = std::clamp(v, 0.0f, 1.0f);
}
float Mixer::busVolume(Bus b) const {
  std::lock_guard<std::mutex> lock(m_);
  return bus_[static_cast<int>(b)];
}
void Mixer::setMaster(float v) {
  std::lock_guard<std::mutex> lock(m_);
  master_ = std::clamp(v, 0.0f, 1.0f);
}
void Mixer::setMuffle(float amount) {
  std::lock_guard<std::mutex> lock(m_);
  muffle_ = std::clamp(amount, 0.0f, 1.0f);
}

// ---------------------------------------------------------------------------
// Music
// ---------------------------------------------------------------------------
bool Mixer::playMusic(std::shared_ptr<const std::vector<uint8_t>> ogg, float fade, float volume) {
  if (!ogg || ogg->empty()) return false;
  int err = 0;
  stb_vorbis* v = stb_vorbis_open_memory(ogg->data(), static_cast<int>(ogg->size()), &err, nullptr);
  if (!v) return false;
  auto d = std::make_unique<Deck>();
  d->bytes = std::move(ogg);
  d->v = v;
  const stb_vorbis_info info = stb_vorbis_get_info(v);
  d->srcRate = static_cast<int>(info.sample_rate);
  d->totalFrames = static_cast<int64_t>(stb_vorbis_stream_length_in_samples(v));
  d->target = std::clamp(volume, 0.0f, 1.0f);
  const float samples = std::max(0.002f, fade) * static_cast<float>(rate_);
  d->gain = fade > 0 ? 0.0f : d->target;
  d->step = d->target / samples;
  std::lock_guard<std::mutex> lock(m_);
  for (auto& o : decks_)
    if (!o->leaving) {
      o->leaving = true;
      o->target = 0;
      o->step = std::max(o->gain, 0.001f) / samples;
    }
  decks_.push_back(std::move(d));
  return true;
}

void Mixer::stopMusic(float fade) {
  std::lock_guard<std::mutex> lock(m_);
  const float samples = std::max(0.002f, fade) * static_cast<float>(rate_);
  for (auto& o : decks_) {
    o->leaving = true;
    o->target = 0;
    o->step = std::max(o->gain, 0.001f) / samples;
  }
}

void Mixer::setMusicVolume(float volume, float fade) {
  std::lock_guard<std::mutex> lock(m_);
  const float samples = std::max(0.002f, fade) * static_cast<float>(rate_);
  for (auto& o : decks_)
    if (!o->leaving) {
      o->target = std::clamp(volume, 0.0f, 1.0f);
      o->step = std::abs(o->target - o->gain) / samples + 1e-7f;
    }
}

bool Mixer::musicPlaying() const {
  std::lock_guard<std::mutex> lock(m_);
  for (const auto& o : decks_)
    if (!o->leaving && !o->ended) return true;
  return false;
}

float Mixer::musicSecondsLeft() const {
  std::lock_guard<std::mutex> lock(m_);
  for (const auto& o : decks_)
    if (!o->leaving && !o->ended)
      return static_cast<float>(o->totalFrames - o->readFrames + static_cast<int64_t>(o->chunkLen - o->chunkPos)) /
             static_cast<float>(std::max(1, o->srcRate));
  return 0.0f;
}

void Mixer::mixMusic(float* out, int frames) {
  for (auto& d : decks_) {
    const double step = static_cast<double>(d->srcRate) / rate_;
    for (int i = 0; i < frames && !d->ended; ++i) {
      // Linear resampling (a no-op when the rates agree).
      if (!d->havePrev) {
        if (!d->pull(d->prev[0], d->prev[1]) || !d->pull(d->next[0], d->next[1])) {
          d->ended = true;
          break;
        }
        d->havePrev = true;
      }
      const float t = static_cast<float>(d->frac);
      const float l = d->prev[0] + (d->next[0] - d->prev[0]) * t;
      const float r = d->prev[1] + (d->next[1] - d->prev[1]) * t;
      if (d->gain < d->target) d->gain = std::min(d->target, d->gain + d->step);
      else if (d->gain > d->target) d->gain = std::max(d->target, d->gain - d->step);
      out[i * 2] += l * d->gain;
      out[i * 2 + 1] += r * d->gain;
      d->frac += step;
      while (d->frac >= 1.0) {
        d->frac -= 1.0;
        d->prev[0] = d->next[0];
        d->prev[1] = d->next[1];
        if (!d->pull(d->next[0], d->next[1])) {
          d->ended = true;
          break;
        }
      }
    }
  }
  decks_.erase(std::remove_if(decks_.begin(), decks_.end(),
                              [](const std::unique_ptr<Deck>& d) { return d->ended || (d->leaving && d->gain <= 0.0f); }),
               decks_.end());
}

// ---------------------------------------------------------------------------
// Mixing
// ---------------------------------------------------------------------------
void Mixer::mixVoices(float* dry, float* front, int frames) {
  const float inv = 1.0f / 32768.0f;
  std::vector<float>& wet = wet_;
  for (auto& vp : voices_) {
    Voice& v = *vp;
    if (!v.active) continue;
    const Sound& s = sounds_[static_cast<size_t>(v.sound)];
    const size_t n = s.frames();
    const int ch = s.channels;
    // Where it is heard from, recomputed once a block and glided across it.
    const Hearing h = hear(v.at, listener_);
    v.audibility = h.gain;
    float tl, tr;
    if (ch == 2 && !v.at.positional) tl = tr = 1.0f;
    else panGains(h.pan, tl, tr);
    const float level = v.volume * h.gain * bus_[static_cast<int>(v.bus)] * master_;
    tl *= level;
    tr *= level;
    const float tw = v.bus == Bus::Ui ? 0.0f : (v.reverb + h.wet) * v.volume * bus_[static_cast<int>(v.bus)] * master_ *
                                                   std::sqrt(std::max(h.gain, 0.0f));
    const float ta = lowpassCoeff(h.cutoff, rate_);
    if (!v.primed) {
      v.gl = tl;
      v.gr = tr;
      v.wet = tw;
      v.lpA = ta;
      v.primed = true;
    }
    const float k = 1.0f / static_cast<float>(frames);
    const float dl = (tl - v.gl) * k, dr = (tr - v.gr) * k, dw = (tw - v.wet) * k, da = (ta - v.lpA) * k;
    float* target = v.bus == Bus::Ui ? front : dry;
    for (int i = 0; i < frames; ++i) {
      const size_t i0 = static_cast<size_t>(v.pos);
      if (i0 >= n) {
        v.active = false;
        break;
      }
      size_t i1 = i0 + 1;
      if (i1 >= n) i1 = v.loop ? 0 : i0;
      const float t = static_cast<float>(v.pos - static_cast<double>(i0));
      float a, b;
      if (ch == 2) {
        a = (s.pcm[i0 * 2] + (s.pcm[i1 * 2] - s.pcm[i0 * 2]) * t) * inv;
        b = (s.pcm[i0 * 2 + 1] + (s.pcm[i1 * 2 + 1] - s.pcm[i0 * 2 + 1]) * t) * inv;
      } else {
        a = b = (s.pcm[i0] + (s.pcm[i1] - s.pcm[i0]) * t) * inv;
      }
      v.lp[0] += v.lpA * (a - v.lp[0]);
      v.lp[1] += v.lpA * (b - v.lp[1]);
      // Envelope.
      if (v.env != v.envTarget) {
        v.env = v.env < v.envTarget ? std::min(v.envTarget, v.env + v.envStep) : std::max(v.envTarget, v.env - v.envStep);
      }
      const float e = v.env;
      target[i * 2] += v.lp[0] * v.gl * e;
      target[i * 2 + 1] += v.lp[1] * v.gr * e;
      wet[static_cast<size_t>(i)] += (v.lp[0] + v.lp[1]) * 0.5f * v.wet * e;
      v.gl += dl;
      v.gr += dr;
      v.wet += dw;
      v.lpA += da;
      v.pos += v.step;
      if (v.pos >= static_cast<double>(n)) {
        if (v.loop) v.pos -= static_cast<double>(n);
        else {
          v.active = false;
          break;
        }
      }
      if (v.stopping && v.env <= 0.0f) {
        v.active = false;
        break;
      }
    }
    v.gl = tl;
    v.gr = tr;
    v.wet = tw;
    v.lpA = ta;
  }
}

void Mixer::mix(float* out, int frames) {
  std::lock_guard<std::mutex> lock(m_);
  const size_t n2 = static_cast<size_t>(frames) * 2;
  dry_.assign(n2, 0.0f);
  music_.assign(n2, 0.0f);
  wet_.assign(static_cast<size_t>(frames), 0.0f);
  std::vector<float> front(n2, 0.0f);
  mixVoices(dry_.data(), front.data(), frames);
  mixMusic(music_.data(), frames);
  // The hall.
  for (int i = 0; i < frames; ++i) {
    float l, r;
    reverb_->process(wet_[static_cast<size_t>(i)], l, r);
    dry_[static_cast<size_t>(i) * 2] += l;
    dry_[static_cast<size_t>(i) * 2 + 1] += r;
  }
  // Muffled world (a menu over the battle).
  const float mA = lowpassCoeff(400.0f + 17600.0f * muffle_ * muffle_, rate_);
  const float musicGain = bus_[static_cast<int>(Bus::Music)] * master_;
  float peak = 0.0f;
  const float release = std::exp(-1.0f / (0.25f * static_cast<float>(rate_)));
  for (int i = 0; i < frames; ++i) {
    float s[2];
    for (int c = 0; c < 2; ++c) {
      const size_t j = static_cast<size_t>(i) * 2 + static_cast<size_t>(c);
      muffleState_[c] += mA * (dry_[j] - muffleState_[c]);
      s[c] = muffleState_[c] * (0.55f + 0.45f * muffle_) + front[j] + music_[j] * musicGain;
    }
    // Limiter: instant attack, quarter-second release, then a soft knee.
    const float p = std::max(std::abs(s[0]), std::abs(s[1]));
    const float want = p > 0.89f ? 0.89f / p : 1.0f;
    limiter_ = want < limiter_ ? want : 1.0f - (1.0f - limiter_) * release;
    for (int c = 0; c < 2; ++c) {
      float x = s[c] * limiter_;
      if (std::abs(x) > 0.9f) x = std::copysign(0.9f + 0.1f * std::tanh((std::abs(x) - 0.9f) * 10.0f), x);
      out[i * 2 + c] = x;
      peak = std::max(peak, std::abs(x));
    }
  }
  lastPeak_ = peak;
}

}  // namespace pioneer::audio
