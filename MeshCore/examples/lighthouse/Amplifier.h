#pragma once

#include <Arduino.h>

#ifndef AMP_I2S_DOUT
#define AMP_I2S_DOUT 18
#endif

#ifndef AMP_I2S_WS
#define AMP_I2S_WS 17
#endif

#ifndef AMP_I2S_BCLK
#define AMP_I2S_BCLK 16
#endif

#ifndef AMP_SAMPLE_RATE
#define AMP_SAMPLE_RATE 16000
#endif

class Amplifier {
public:
  Amplifier();

  void begin(uint32_t sample_rate = AMP_SAMPLE_RATE);
  void end();

  bool isInitialized() const;

  size_t writeSamples(const int16_t *interleaved_stereo, size_t frames);
  size_t writeMonoSamples(const int16_t *mono, size_t frames);

private:
#ifdef ESP32
  bool _initialized;
  uint32_t _sample_rate;

  size_t writeBytes(const void *data, size_t bytes);
#endif
};
