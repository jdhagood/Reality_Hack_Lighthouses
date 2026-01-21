#include "LightChime.h"
#include "Amplifier.h"

#ifdef ESP32
#include <math.h>

namespace {
constexpr float kPi = 3.14159265f;
constexpr uint16_t kToneCount = 2;
constexpr float kTonesHz[kToneCount] = { 880.0f, 1174.7f };
constexpr uint16_t kToneMs[kToneCount] = { 120, 180 };
constexpr uint16_t kGapMs = 40;
}

LightChime::LightChime()
  : _initialized(false),
    _amplifier(nullptr),
    _playing(false),
    _tone_index(0),
    _tone_elapsed_ms(0),
    _phase(0.0f),
    _last_sample_ms(0) {
}

void LightChime::begin() {
  if (_amplifier == nullptr) {
    _amplifier = new Amplifier();
  }
  _amplifier->begin(CHIME_SAMPLE_RATE);
  _initialized = true;
}

void LightChime::playMessageChime() {
  if (!_initialized) {
    return;
  }
  _playing = true;
  _tone_index = 0;
  _tone_elapsed_ms = 0;
  _phase = 0.0f;
  _last_sample_ms = millis();
}

void LightChime::loop() {
  if (!_initialized || !_playing) {
    return;
  }

  uint32_t now = millis();
  uint32_t elapsed_ms = now - _last_sample_ms;
  if (elapsed_ms == 0) {
    return;
  }

  _last_sample_ms = now;

  uint16_t samples = (uint16_t)((CHIME_SAMPLE_RATE * elapsed_ms) / 1000);
  if (samples == 0) {
    return;
  }

  while (samples > 0 && _playing) {
    uint16_t batch = samples > kBufferSamples ? kBufferSamples : samples;
    fillBuffer(batch);
    _amplifier->writeMonoSamples(_buffer, batch);
    samples -= batch;
  }
}

void LightChime::fillBuffer(uint16_t samples) {
  float freq = currentToneFreq();
  bool silent = (freq <= 0.1f);
  float phase_step = (2.0f * kPi * freq) / CHIME_SAMPLE_RATE;

  for (uint16_t i = 0; i < samples; ++i) {
    int16_t sample = 0;
    if (!silent) {
      float s = sinf(_phase) * CHIME_VOLUME;
      sample = (int16_t)(s * 32767.0f);
      _phase += phase_step;
      if (_phase >= 2.0f * kPi) {
        _phase -= 2.0f * kPi;
      }
    }
    _buffer[i] = sample;
  }

  uint16_t ms_advanced = (uint16_t)((samples * 1000) / CHIME_SAMPLE_RATE);
  _tone_elapsed_ms += ms_advanced;
  if (_tone_elapsed_ms >= currentToneDurationMs()) {
    _tone_index++;
    _tone_elapsed_ms = 0;
    _phase = 0.0f;
    if (_tone_index >= kToneCount * 2) {
      _playing = false;
      return;
    }
  }
}

float LightChime::currentToneFreq() const {
  if (_tone_index >= kToneCount * 2) {
    return 0.0f;
  }
  if ((_tone_index % 2) == 1) {
    return 0.0f;
  }
  return kTonesHz[_tone_index / 2];
}

uint16_t LightChime::currentToneDurationMs() const {
  if (_tone_index >= kToneCount * 2) {
    return 0;
  }
  if ((_tone_index % 2) == 1) {
    return kGapMs;
  }
  return kToneMs[_tone_index / 2];
}

#else

LightChime::LightChime() {}
void LightChime::begin() {}
void LightChime::loop() {}
void LightChime::playMessageChime() {}

#endif
