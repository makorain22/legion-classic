// Pioneer Engine - the sound card, through SDL3.
#include <SDL3/SDL.h>

#include "pioneer/audio/audio.hpp"

namespace pioneer::audio {

AudioDevice::~AudioDevice() { close(); }

void AudioDevice::callback(void* user, void* stream, int additional, int /*total*/) {
  auto* self = static_cast<AudioDevice*>(user);
  if (!self->mixer_ || additional <= 0) return;
  const int frames = additional / static_cast<int>(sizeof(float) * 2);
  if (frames <= 0) return;
  self->buf_.resize(static_cast<size_t>(frames) * 2);
  self->mixer_->mix(self->buf_.data(), frames);
  SDL_PutAudioStreamData(static_cast<SDL_AudioStream*>(stream), self->buf_.data(),
                         frames * static_cast<int>(sizeof(float) * 2));
}

bool AudioDevice::open(Mixer& mixer, std::string& error) {
  close();
  if (!SDL_WasInit(SDL_INIT_AUDIO) && !SDL_InitSubSystem(SDL_INIT_AUDIO)) {
    error = SDL_GetError();
    return false;
  }
  mixer_ = &mixer;
  SDL_AudioSpec spec{};
  spec.format = SDL_AUDIO_F32;
  spec.channels = 2;
  spec.freq = mixer.sampleRate();
  SDL_AudioStream* s = SDL_OpenAudioDeviceStream(
      SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec,
      [](void* user, SDL_AudioStream* stream, int additional, int total) { callback(user, stream, additional, total); },
      this);
  if (!s) {
    error = SDL_GetError();
    mixer_ = nullptr;
    return false;
  }
  stream_ = s;
  SDL_ResumeAudioStreamDevice(s);
  return true;
}

void AudioDevice::close() {
  if (stream_) SDL_DestroyAudioStream(static_cast<SDL_AudioStream*>(stream_));
  stream_ = nullptr;
  mixer_ = nullptr;
}

}  // namespace pioneer::audio
