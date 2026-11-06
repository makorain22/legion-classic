#pragma once
// Slave Engine - sound.
//
// A software mixer built for a big battlefield heard from above:
//   * Positional voices fade with distance from the camera, lose their high
//     end (a far battle is a rumble, not a crackle) and get wetter, so the
//     front reads as far away rather than merely quiet. Zooming out lifts the
//     listener: everything is a little more distant.
//   * A large-hall reverb on a send bus gives guns and explosions their size.
//   * Voices have priorities; when every voice is busy the least important,
//     least audible one is taken.
//   * Two music decks stream Ogg Vorbis and crossfade.
//   * Buses (effects, voices, music, ambience, interface) each have a volume;
//     a limiter keeps a barrage from clipping.
//
// Nothing here touches the simulation: sound is presentation only and never
// affects determinism. The mixer has no device of its own (AudioDevice feeds
// it to SDL), so it runs headless in tests.

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace slave::audio {

enum class Bus : uint8_t { Sfx, Voice, Music, Ambience, Ui, Count };
constexpr int kBusCount = static_cast<int>(Bus::Count);
const char* busName(Bus b);

// Decoded PCM, 16-bit, interleaved.
struct Sound {
  std::vector<int16_t> pcm;
  int channels = 1;
  int rate = 48000;
  size_t frames() const { return channels ? pcm.size() / static_cast<size_t>(channels) : 0; }
  float seconds() const { return rate ? static_cast<float>(frames()) / static_cast<float>(rate) : 0.0f; }
};

bool decodeOgg(const uint8_t* data, size_t size, Sound& out, std::string& error);
bool decodeWav(const uint8_t* data, size_t size, Sound& out, std::string& error);
// .ogg or .wav by extension.
bool loadSound(const std::string& path, Sound& out, std::string& error);
bool readFileBytes(const std::string& path, std::vector<uint8_t>& out);

// Where a sound comes from. Ground position in tiles.
struct Emitter {
  bool positional = false;
  float x = 0, y = 0;
  float range = 12.0f;  // tiles beyond the near zone at which it has faded to half
};

struct PlayDesc {
  int sound = -1;
  Bus bus = Bus::Sfx;
  float volume = 1.0f;
  float pitch = 1.0f;
  int priority = 0;       // higher wins a voice
  bool loop = false;
  float reverb = 0.15f;   // send to the hall, 0..1
  float fadeIn = 0.0f;    // seconds
  Emitter at;
};

using VoiceId = uint32_t;

// The ear: the camera's centre on the ground, in tiles.
struct Listener {
  float x = 0, y = 0;
  float zoom = 1.0f;         // 0.5, 1, 2
  float viewTiles = 20.0f;   // roughly half the width of the view, in tiles
};

// How a positional sound is heard from the listener.
struct Hearing {
  float gain = 1.0f;     // 0..1
  float cutoff = 20000;  // Hz, low-pass
  float pan = 0.0f;      // -1 left .. +1 right
  float wet = 0.0f;      // extra reverb for distance
};
Hearing hear(const Emitter& e, const Listener& l);

class Mixer {
 public:
  explicit Mixer(int sampleRate = 48000, int maxVoices = 64);
  ~Mixer();
  Mixer(const Mixer&) = delete;
  Mixer& operator=(const Mixer&) = delete;

  int sampleRate() const { return rate_; }

  // Sounds are kept for the mixer's life; returns the index for PlayDesc.
  int addSound(Sound s);
  const Sound* sound(int i) const;
  int soundCount() const;

  // 0 when it wasn't started (inaudible, or every voice busy with more important sounds).
  VoiceId play(const PlayDesc& d);
  void stop(VoiceId v, float fadeSeconds = 0.05f);
  void stopBus(Bus b, float fadeSeconds = 0.3f);
  void setVolume(VoiceId v, float volume, float fadeSeconds = 0.1f);
  void setPosition(VoiceId v, float x, float y);
  bool playing(VoiceId v) const;
  int activeVoices() const;

  void setListener(const Listener& l);
  Listener listener() const;
  void setBusVolume(Bus b, float v);
  float busVolume(Bus b) const;
  void setMaster(float v);
  // Muffles everything (menus over a battle): 1 = clear.
  void setMuffle(float amount);

  // Music: streamed from Ogg Vorbis bytes; the old track fades out as the new one fades in.
  bool playMusic(std::shared_ptr<const std::vector<uint8_t>> ogg, float fadeSeconds, float volume = 1.0f);
  void stopMusic(float fadeSeconds);
  void setMusicVolume(float volume, float fadeSeconds);
  bool musicPlaying() const;          // the current track hasn't ended (fading-out ones don't count)
  float musicSecondsLeft() const;     // of the current track

  // Fills `frames` stereo frames (interleaved float). Called from the audio thread.
  void mix(float* out, int frames);
  // Peak level of the last mixed block (for tests and the debug panel).
  float lastPeak() const { return lastPeak_; }

 private:
  struct Voice;
  struct Deck;
  struct Reverb;
  void mixVoices(float* dry, float* wet, int frames);
  void mixMusic(float* out, int frames);

  int rate_;
  mutable std::mutex m_;
  std::vector<Sound> sounds_;
  std::vector<std::unique_ptr<Voice>> voices_;
  std::vector<std::unique_ptr<Deck>> decks_;
  std::unique_ptr<Reverb> reverb_;
  Listener listener_;
  float bus_[kBusCount] = {1, 1, 1, 1, 1};
  float master_ = 1.0f;
  float muffle_ = 1.0f;
  float muffleState_[2] = {0, 0};
  float limiter_ = 1.0f;
  float lastPeak_ = 0.0f;
  uint32_t nextId_ = 1;
  std::vector<float> dry_, wet_, music_;
};

// Feeds a Mixer to the sound card through SDL. Safe to use when there is no
// sound card: open() fails and nothing is heard.
class AudioDevice {
 public:
  ~AudioDevice();
  bool open(Mixer& mixer, std::string& error);
  void close();
  bool isOpen() const { return stream_ != nullptr; }

 private:
  void* stream_ = nullptr;  // SDL_AudioStream*
  Mixer* mixer_ = nullptr;
  std::vector<float> buf_;
  static void callback(void* user, void* stream, int additional, int total);
};

}  // namespace slave::audio
