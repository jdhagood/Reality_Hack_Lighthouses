#pragma once

#include <Arduino.h>

#ifndef CHIME_SAMPLE_RATE
#define CHIME_SAMPLE_RATE 16000
#endif

#ifndef CHIME_VOLUME
#define CHIME_VOLUME 0.4f
#endif

class LightChime {
public:
  LightChime();

  void begin();
  void loop();
  void playMessageChime();

private:
#ifdef ESP32
  bool _initialized;
  class Amplifier *_amplifier;
  bool _playing;
  uint16_t _tone_index;
  uint32_t _tone_elapsed_ms;
  float _phase;
  unsigned long _last_sample_ms;

  static const uint16_t kBufferSamples = 256;
  int16_t _buffer[kBufferSamples];

  void fillBuffer(uint16_t samples);
  float currentToneFreq() const;
  uint16_t currentToneDurationMs() const;
#endif
};
