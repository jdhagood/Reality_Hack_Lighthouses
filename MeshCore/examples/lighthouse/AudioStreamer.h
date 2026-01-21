#pragma once

#include <Arduino.h>

class AudioStreamer {
public:
  AudioStreamer();

  void begin();
  void loop();
  bool play(const char *url);
  bool playFile(const char *path);
  void stop();
  bool isPlaying() const;
  float getLevel() const;

private:
#ifdef ESP32
  class AudioGenerator *_decoder;
  class AudioFileSource *_source;
  class AudioFileSourceBuffer *_buffer;
  class AudioOutputI2S *_output;
  float _smoothed_level;
  bool _fs_ready;
  bool _preroll_active;
  unsigned long _preroll_until_ms;

  void updateLevel(int16_t sample[2]);
  friend class MeteredI2S;
#endif
};
