#include "AudioStreamer.h"

#ifdef ESP32
#include <AudioFileSource.h>
#include <AudioFileSourceBuffer.h>
#include <AudioFileSourceHTTPStream.h>
#include <AudioFileSourceLittleFS.h>
#include <AudioGeneratorMP3.h>
#include <AudioGeneratorWAV.h>
#include <AudioOutputI2S.h>
#include <LittleFS.h>
#include "Amplifier.h"
#include "LightChime.h"
#include "global_configs.h"
#endif

namespace {
bool ends_with(const char *text, const char *suffix) {
  if (!text || !suffix) {
    return false;
  }
  size_t text_len = strlen(text);
  size_t suffix_len = strlen(suffix);
  if (suffix_len > text_len) {
    return false;
  }
  return strncasecmp(text + text_len - suffix_len, suffix, suffix_len) == 0;
}

const char *strip_query(const char *url, char *buffer, size_t buffer_len) {
  if (!url || buffer_len == 0) {
    return "";
  }
  strncpy(buffer, url, buffer_len - 1);
  buffer[buffer_len - 1] = '\0';
  char *query = strchr(buffer, '?');
  if (query) {
    *query = '\0';
  }
  return buffer;
}
}

class MeteredI2S : public AudioOutputI2S {
public:
  explicit MeteredI2S(AudioStreamer *owner) : _owner(owner) {}

  bool ConsumeSample(int16_t sample[2]) override {
    if (_owner) {
      _owner->updateLevel(sample);
    }
    return AudioOutputI2S::ConsumeSample(sample);
  }

private:
  AudioStreamer *_owner;
};

AudioStreamer::AudioStreamer()
#ifdef ESP32
  : _decoder(nullptr),
    _source(nullptr),
    _buffer(nullptr),
    _output(nullptr),
    _smoothed_level(0.0f),
    _fs_ready(false),
    _preroll_active(false),
    _preroll_until_ms(0)
#endif
{
}

void AudioStreamer::begin() {
#ifdef ESP32
  if (!_fs_ready) {
    _fs_ready = LittleFS.begin(true);
    if (!_fs_ready) {
      Serial.println("AudioStreamer: LittleFS mount failed");
    }
  }
  if (_output == nullptr) {
    _output = new MeteredI2S(this);
    _output->SetPinout(AMP_I2S_BCLK, AMP_I2S_WS, AMP_I2S_DOUT);
    _output->SetGain(AUDIO_VOLUME);
  }
#endif
}

bool AudioStreamer::play(const char *url) {
#ifdef ESP32
  if (!url || url[0] == '\0') {
    return false;
  }
  if (isPlaying()) {
    return false;
  }

  stop();

  _source = new AudioFileSourceHTTPStream(url);
  _buffer = new AudioFileSourceBuffer(_source, AUDIO_BUFFER_BYTES);

  char url_no_query[192];
  const char *clean = strip_query(url, url_no_query, sizeof(url_no_query));

  if (ends_with(clean, ".mp3")) {
    _decoder = new AudioGeneratorMP3();
  } else if (ends_with(clean, ".ogg")) {
    Serial.println("AudioStreamer: OGG not supported in this build.");
    stop();
    return false;
  } else {
    _decoder = new AudioGeneratorWAV();
  }

  if (!_decoder->begin(_buffer, _output)) {
    stop();
    return false;
  }
  if (AUDIO_PREROLL_MS > 0 && _output) {
    _preroll_active = true;
    _preroll_until_ms = millis() + AUDIO_PREROLL_MS;
    _output->SetGain(0.0f);
  }
  return true;
#else
  (void)url;
  return false;
#endif
}

bool AudioStreamer::playFile(const char *path) {
#ifdef ESP32
  if (!path || path[0] == '\0') {
    return false;
  }
  if (!_fs_ready) {
    Serial.println("AudioStreamer: LittleFS not mounted");
    return false;
  }
  if (isPlaying()) {
    return false;
  }

  stop();

  _source = new AudioFileSourceLittleFS(path);
  _buffer = new AudioFileSourceBuffer(_source, AUDIO_BUFFER_BYTES);

  if (ends_with(path, ".mp3")) {
    _decoder = new AudioGeneratorMP3();
  } else if (ends_with(path, ".ogg")) {
    Serial.println("AudioStreamer: OGG not supported in this build.");
    stop();
    return false;
  } else {
    _decoder = new AudioGeneratorWAV();
  }

  if (!_decoder->begin(_buffer, _output)) {
    stop();
    return false;
  }
  if (AUDIO_PREROLL_MS > 0 && _output) {
    _preroll_active = true;
    _preroll_until_ms = millis() + AUDIO_PREROLL_MS;
    _output->SetGain(0.0f);
  }
  return true;
#else
  (void)path;
  return false;
#endif
}

void AudioStreamer::loop() {
#ifdef ESP32
  if (_decoder) {
    if (!_decoder->loop()) {
      stop();
    }
    if (_preroll_active && millis() >= _preroll_until_ms && _output) {
      _preroll_active = false;
      _output->SetGain(AUDIO_VOLUME);
    }
  } else if (_smoothed_level > 0.001f) {
    _smoothed_level *= 0.9f;
    if (_smoothed_level < 0.001f) {
      _smoothed_level = 0.0f;
    }
  }
#endif
}

void AudioStreamer::stop() {
#ifdef ESP32
  if (_decoder) {
    _decoder->stop();
    delete _decoder;
    _decoder = nullptr;
  }
  if (_buffer) {
    delete _buffer;
    _buffer = nullptr;
  }
  if (_source) {
    delete _source;
    _source = nullptr;
  }
  _smoothed_level = 0.0f;
  _preroll_active = false;
  _preroll_until_ms = 0;
  if (_output) {
    _output->SetGain(AUDIO_VOLUME);
  }
#endif
}

bool AudioStreamer::isPlaying() const {
#ifdef ESP32
  return _decoder != nullptr;
#else
  return false;
#endif
}

float AudioStreamer::getLevel() const {
#ifdef ESP32
  return _smoothed_level;
#else
  return 0.0f;
#endif
}

#ifdef ESP32
void AudioStreamer::updateLevel(int16_t sample[2]) {
  int32_t left = sample[0] < 0 ? -sample[0] : sample[0];
  int32_t right = sample[1] < 0 ? -sample[1] : sample[1];
  float magnitude = (float)((left > right) ? left : right) / 32768.0f;
  const float alpha = AUDIO_SMOOTHING_ALPHA;
  _smoothed_level = (_smoothed_level * (1.0f - alpha)) + (magnitude * alpha);
}
#endif
