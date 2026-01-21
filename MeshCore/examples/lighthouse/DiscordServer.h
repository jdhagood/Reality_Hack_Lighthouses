#pragma once

#include <Arduino.h>

class DiscordServer {
public:
  DiscordServer();

  void begin();
  bool isEnabled() const;
  void sendChannelMessage(const char *text);

private:
  bool _enabled;
#ifdef ESP32
  const char *_bot_auth;
  const char *_channel_id;
#endif
};
