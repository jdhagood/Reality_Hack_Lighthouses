#pragma once

#include <Arduino.h>

class HelpBotClient {
public:
  HelpBotClient();

  void begin();
  void setUrl(const char *url);
  bool isEnabled() const;
  bool postMeshEvent(const char *text, const char *sender_name);

private:
  bool _enabled;
  const char *_bot_url;
  char _bot_url_storage[128];
  const char *_bot_token;
};
