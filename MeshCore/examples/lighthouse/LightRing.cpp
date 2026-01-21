#include "LightRing.h"
#include "global_configs.h"

LightRing::LightRing()
  : _pixels(LIGHT_RING_COUNT, LIGHT_RING_PIN, NEO_GRB + NEO_KHZ800),
    _pulse_color(_pixels.Color(0, 80, 255)),
    _pulse_duration_ms(600),
    _pulse_start_ms(0),
    _pulse_active(false),
    _mode(Mode::Idle),
    _spin_color(_pixels.Color(20, 20, 20)),
    _spin_interval_ms(80),
    _last_spin_ms(0),
    _spin_index(0),
    _status_color(0),
    _status_duration_ms(0),
    _status_start_ms(0),
    _idle_color(_pixels.Color(LIGHTHOUSE_IDLE_R, LIGHTHOUSE_IDLE_G, LIGHTHOUSE_IDLE_B)),
    _idle_enabled(true),
    _idle_dirty(true),
    _audio_level(0.0f),
    _audio_active(false),
    _audio_dirty(true),
    _blink_active(false),
    _blink_on(false),
    _blink_color(_pixels.Color(255, 255, 255)),
    _blink_interval_ms(500),
    _blink_last_ms(0),
    _blink_dirty(true),
    _orbit_active(false),
    _orbit_interval_ms(120),
    _orbit_last_ms(0),
    _orbit_index(0) {
}

void LightRing::begin() {
  _pixels.begin();
  _pixels.clear();
  _pixels.show();
}

void LightRing::startStartupSpin(uint8_t r, uint8_t g, uint8_t b, uint16_t interval_ms) {
  _spin_color = _pixels.Color(r, g, b);
  _spin_interval_ms = interval_ms;
  _spin_index = 0;
  _last_spin_ms = millis();
  _mode = Mode::StartupSpin;
  applySpin();
}

void LightRing::finishStartup(bool wifi_connected, uint16_t duration_ms) {
  _status_color = wifi_connected ? _pixels.Color(0, 160, 0) : _pixels.Color(160, 0, 0);
  _status_duration_ms = duration_ms;
  _status_start_ms = millis();
  _mode = Mode::StatusPulse;
  applyStatus();
}

void LightRing::setIdleColor(uint8_t r, uint8_t g, uint8_t b) {
  _idle_color = _pixels.Color(r, g, b);
  _idle_dirty = true;
}

void LightRing::setIdleEnabled(bool enabled) {
  _idle_enabled = enabled;
  _idle_dirty = true;
}

void LightRing::setPulseColor(uint8_t r, uint8_t g, uint8_t b) {
  _pulse_color = _pixels.Color(r, g, b);
}

void LightRing::setPulseDuration(uint16_t millis) {
  _pulse_duration_ms = millis;
}

void LightRing::setAudioLevel(float level) {
  if (level < 0.0f) {
    level = 0.0f;
  } else if (level > 1.0f) {
    level = 1.0f;
  }
  _audio_level = level;
  _audio_active = level > 0.02f;
  _audio_dirty = true;
}

void LightRing::setBlinking(bool enabled, uint8_t r, uint8_t g, uint8_t b, uint16_t interval_ms) {
  _blink_active = enabled;
  _blink_color = _pixels.Color(r, g, b);
  _blink_interval_ms = interval_ms;
  _blink_last_ms = millis();
  _blink_on = true;
  _blink_dirty = true;
}

void LightRing::setOrbiting(bool enabled, uint16_t interval_ms) {
  _orbit_active = enabled;
  _orbit_interval_ms = interval_ms;
  _orbit_last_ms = millis();
  _orbit_index = 0;
  if (enabled) {
    applyOrbit();
  } else {
    _idle_dirty = true;
  }
}

void LightRing::notifyChannelMessage() {
  _pulse_start_ms = millis();
  _pulse_active = true;
}

void LightRing::loop() {
  unsigned long now = millis();

  if (_blink_active) {
    if ((now - _blink_last_ms) >= _blink_interval_ms) {
      _blink_last_ms = now;
      _blink_on = !_blink_on;
      _blink_dirty = true;
    }
    if (_blink_dirty) {
      applyBlink();
    }
    return;
  }

  if (_audio_active) {
    if (_audio_dirty) {
      applyAudio();
    }
    return;
  }

  if (_orbit_active) {
    if (_idle_dirty || (now - _orbit_last_ms) >= _orbit_interval_ms) {
      _orbit_last_ms = now;
      _orbit_index = (_orbit_index + 1) % _pixels.numPixels();
      applyOrbit();
    }
    return;
  }

  if (_mode == Mode::StatusPulse) {
    if ((now - _status_start_ms) >= _status_duration_ms) {
      if (_idle_enabled) {
        applyIdle();
      } else {
        _pixels.clear();
        _pixels.show();
      }
      _mode = Mode::Idle;
    }
    return;
  }

  if (_pulse_active) {
    uint16_t elapsed = (uint16_t)(now - _pulse_start_ms);
    if (elapsed >= _pulse_duration_ms) {
      if (_idle_enabled) {
        applyIdle();
      } else {
        _pixels.clear();
        _pixels.show();
      }
      _pulse_active = false;
    } else {
      applyPulse(elapsed);
    }
    return;
  }

  if (_mode == Mode::StartupSpin) {
    if ((now - _last_spin_ms) >= _spin_interval_ms) {
      _last_spin_ms = now;
      _spin_index = (_spin_index + 1) % _pixels.numPixels();
      applySpin();
    }
    return;
  }

  if (_mode == Mode::Idle && _idle_enabled && _idle_dirty) {
    applyIdle();
  }
}

void LightRing::applyPulse(uint16_t elapsed_ms) {
  uint8_t brightness;
  uint16_t half = _pulse_duration_ms / 2;
  if (elapsed_ms <= half) {
    brightness = (uint8_t)(255 * elapsed_ms / half);
  } else {
    uint16_t down = _pulse_duration_ms - elapsed_ms;
    brightness = (uint8_t)(255 * down / half);
  }

  uint8_t r = (uint8_t)((_pulse_color >> 16) & 0xFF);
  uint8_t g = (uint8_t)((_pulse_color >> 8) & 0xFF);
  uint8_t b = (uint8_t)(_pulse_color & 0xFF);

  uint32_t color = _pixels.Color((uint8_t)(r * brightness / 255),
                                 (uint8_t)(g * brightness / 255),
                                 (uint8_t)(b * brightness / 255));

  for (uint16_t i = 0; i < _pixels.numPixels(); ++i) {
    _pixels.setPixelColor(i, color);
  }
  _pixels.show();
}

void LightRing::applySpin() {
  _pixels.clear();
  _pixels.setPixelColor(_spin_index, _spin_color);
  _pixels.show();
}

void LightRing::applyStatus() {
  for (uint16_t i = 0; i < _pixels.numPixels(); ++i) {
    _pixels.setPixelColor(i, _status_color);
  }
  _pixels.show();
}

void LightRing::applyIdle() {
  for (uint16_t i = 0; i < _pixels.numPixels(); ++i) {
    _pixels.setPixelColor(i, _idle_color);
  }
  _pixels.show();
  _idle_dirty = false;
}

void LightRing::applyAudio() {
  uint8_t brightness = (uint8_t)(255.0f * _audio_level);
  uint32_t color = _pixels.Color(brightness, brightness, brightness);
  for (uint16_t i = 0; i < _pixels.numPixels(); ++i) {
    _pixels.setPixelColor(i, color);
  }
  _pixels.show();
  _audio_dirty = false;
}

void LightRing::applyBlink() {
  uint32_t color = _blink_on ? _blink_color : 0;
  for (uint16_t i = 0; i < _pixels.numPixels(); ++i) {
    _pixels.setPixelColor(i, color);
  }
  _pixels.show();
  _blink_dirty = false;
}

void LightRing::applyOrbit() {
  uint8_t base_r = (uint8_t)((_idle_color >> 16) & 0xFF);
  uint8_t base_g = (uint8_t)((_idle_color >> 8) & 0xFF);
  uint8_t base_b = (uint8_t)(_idle_color & 0xFF);
  uint8_t bright_r = (uint8_t)min(255, base_r * 3);
  uint8_t bright_g = (uint8_t)min(255, base_g * 3);
  uint8_t bright_b = (uint8_t)min(255, base_b * 3);
  uint32_t base_color = _pixels.Color(base_r, base_g, base_b);
  uint32_t bright_color = _pixels.Color(bright_r, bright_g, bright_b);

  for (uint16_t i = 0; i < _pixels.numPixels(); ++i) {
    _pixels.setPixelColor(i, base_color);
  }
  _pixels.setPixelColor(_orbit_index, bright_color);
  _pixels.show();
  _idle_dirty = false;
}
