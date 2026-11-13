// Pioneer Engine - reading sound files: Ogg Vorbis (stb_vorbis) and WAV.
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <fstream>

#include "pioneer/audio/audio.hpp"

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-value"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#pragma GCC diagnostic ignored "-Wshadow"
#elif defined(_MSC_VER)
#pragma warning(push, 0)
#endif
#include "stb_vorbis.c"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#endif

namespace pioneer::audio {

bool readFileBytes(const std::string& path, std::vector<uint8_t>& out) {
  std::ifstream f(path, std::ios::binary);
  if (!f) return false;
  f.seekg(0, std::ios::end);
  const std::streamoff n = f.tellg();
  if (n < 0) return false;
  f.seekg(0);
  out.resize(static_cast<size_t>(n));
  return static_cast<bool>(f.read(reinterpret_cast<char*>(out.data()), n)) || n == 0;
}

bool decodeOgg(const uint8_t* data, size_t size, Sound& out, std::string& error) {
  int channels = 0, rate = 0;
  short* pcm = nullptr;
  const int frames = stb_vorbis_decode_memory(data, static_cast<int>(size), &channels, &rate, &pcm);
  if (frames <= 0 || !pcm) {
    error = "not an Ogg Vorbis file";
    return false;
  }
  out.channels = std::clamp(channels, 1, 2);
  out.rate = rate;
  out.pcm.resize(static_cast<size_t>(frames) * static_cast<size_t>(out.channels));
  if (channels <= 2) {
    std::memcpy(out.pcm.data(), pcm, out.pcm.size() * sizeof(int16_t));
  } else {  // more than stereo: keep the first two
    for (int i = 0; i < frames; ++i) {
      out.pcm[static_cast<size_t>(i) * 2] = pcm[i * channels];
      out.pcm[static_cast<size_t>(i) * 2 + 1] = pcm[i * channels + 1];
    }
  }
  free(pcm);
  return true;
}

namespace {
uint32_t le32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<uint32_t>(p[3]) << 24); }
uint16_t le16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
}  // namespace

bool decodeWav(const uint8_t* d, size_t size, Sound& out, std::string& error) {
  if (size < 44 || std::memcmp(d, "RIFF", 4) || std::memcmp(d + 8, "WAVE", 4)) {
    error = "not a WAV file";
    return false;
  }
  int channels = 0, rate = 0, bits = 0, format = 0;
  size_t pos = 12;
  while (pos + 8 <= size) {
    const uint32_t len = le32(d + pos + 4);
    const uint8_t* body = d + pos + 8;
    if (pos + 8 + len > size) break;
    if (!std::memcmp(d + pos, "fmt ", 4) && len >= 16) {
      format = le16(body);
      channels = le16(body + 2);
      rate = static_cast<int>(le32(body + 4));
      bits = le16(body + 14);
    } else if (!std::memcmp(d + pos, "data", 4)) {
      if ((format != 1 && format != 3) || channels < 1 || (bits != 16 && bits != 32 && bits != 8)) {
        error = "unsupported WAV format (16-bit PCM, 8-bit or float only)";
        return false;
      }
      const int bps = bits / 8;
      const size_t frames = len / static_cast<size_t>(bps * channels);
      out.channels = std::min(channels, 2);
      out.rate = rate;
      out.pcm.resize(frames * static_cast<size_t>(out.channels));
      for (size_t i = 0; i < frames; ++i)
        for (int c = 0; c < out.channels; ++c) {
          const uint8_t* s = body + (i * static_cast<size_t>(channels) + static_cast<size_t>(c)) * static_cast<size_t>(bps);
          int v;
          if (bits == 16) v = static_cast<int16_t>(le16(s));
          else if (bits == 8) v = (static_cast<int>(s[0]) - 128) << 8;
          else if (format == 3) {
            float f;
            std::memcpy(&f, s, 4);
            v = static_cast<int>(std::clamp(f, -1.0f, 1.0f) * 32767.0f);
          } else v = static_cast<int32_t>(le32(s)) >> 16;
          out.pcm[i * static_cast<size_t>(out.channels) + static_cast<size_t>(c)] = static_cast<int16_t>(v);
        }
      return true;
    }
    pos += 8 + len + (len & 1);
  }
  error = "WAV file has no data";
  return false;
}

bool loadSound(const std::string& path, Sound& out, std::string& error) {
  std::vector<uint8_t> bytes;
  if (!readFileBytes(path, bytes)) {
    error = "can't read " + path;
    return false;
  }
  const bool wav = path.size() > 4 && (path.compare(path.size() - 4, 4, ".wav") == 0 || path.compare(path.size() - 4, 4, ".WAV") == 0);
  return wav ? decodeWav(bytes.data(), bytes.size(), out, error) : decodeOgg(bytes.data(), bytes.size(), out, error);
}

}  // namespace pioneer::audio
