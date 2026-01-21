#pragma once

#include <Arduino.h>

class HelpBotDiscovery {
public:
  HelpBotDiscovery();

  void begin();
  void loop();
  bool hasUrl() const;
  const char *getUrl() const;

private:
#ifdef ESP32
  class WiFiUDP *_udp;
  unsigned long _last_broadcast_ms;
  bool _has_url;
  char _url[128];

  void broadcastDiscovery();
  void handleResponse(int packet_size);
#endif
};
