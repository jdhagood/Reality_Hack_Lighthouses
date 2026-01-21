#pragma once

#include <Arduino.h>
#include <Adafruit_NeoPixel.h>

#ifndef LIGHT_RING_PIN
#define LIGHT_RING_PIN 48
#endif

#ifndef LIGHT_RING_COUNT
#define LIGHT_RING_COUNT 12
#endif

class LightRing {
public:
  LightRing();

  void begin();
  void loop();
  void notifyChannelMessage();
  void startStartupSpin(uint8_t r, uint8_t g, uint8_t b, uint16_t interval_ms = 80);
  void finishStartup(bool wifi_connected, uint16_t duration_ms = 200);

  void setIdleColor(uint8_t r, uint8_t g, uint8_t b);
  void setIdleEnabled(bool enabled);
  void setPulseColor(uint8_t r, uint8_t g, uint8_t b);
  void setPulseDuration(uint16_t millis);
  void setAudioLevel(float level);
  void setBlinking(bool enabled, uint8_t r = 255, uint8_t g = 255, uint8_t b = 255, uint16_t interval_ms = 500);
  void setOrbiting(bool enabled, uint16_t interval_ms);

private:
  enum class Mode {
    Idle,
    StartupSpin,
    StatusPulse
  };

  Adafruit_NeoPixel _pixels;
  uint32_t _pulse_color;
  uint16_t _pulse_duration_ms;
  unsigned long _pulse_start_ms;
  bool _pulse_active;
  Mode _mode;
  uint32_t _spin_color;
  uint16_t _spin_interval_ms;
  unsigned long _last_spin_ms;
  uint16_t _spin_index;
  uint32_t _status_color;
  uint16_t _status_duration_ms;
  unsigned long _status_start_ms;
  uint32_t _idle_color;
  bool _idle_enabled;
  bool _idle_dirty;
  float _audio_level;
  bool _audio_active;
  bool _audio_dirty;
  bool _blink_active;
  bool _blink_on;
  uint32_t _blink_color;
  uint16_t _blink_interval_ms;
  unsigned long _blink_last_ms;
  bool _blink_dirty;
  bool _orbit_active;
  uint16_t _orbit_interval_ms;
  unsigned long _orbit_last_ms;
  uint16_t _orbit_index;

  void applyPulse(uint16_t elapsed_ms);
  void applySpin();
  void applyStatus();
  void applyIdle();
  void applyAudio();
  void applyBlink();
  void applyOrbit();
};
