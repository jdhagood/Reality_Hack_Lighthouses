#include "HelpBotDiscovery.h"

#ifdef ESP32
#include <WiFiUdp.h>
#include "secrets.h"
#endif

namespace {
const unsigned long kDiscoveryIntervalMs = 5000;
}

HelpBotDiscovery::HelpBotDiscovery()
#ifdef ESP32
  : _udp(nullptr),
    _last_broadcast_ms(0),
    _has_url(false),
    _url{0}
#endif
{
}

void HelpBotDiscovery::begin() {
#ifdef ESP32
#ifndef HELP_BOT_DISCOVERY_PORT
#define HELP_BOT_DISCOVERY_PORT 45678
#endif
  if (_udp == nullptr) {
    _udp = new WiFiUDP();
  }
  _udp->begin(HELP_BOT_DISCOVERY_PORT);
  _last_broadcast_ms = 0;
  _has_url = false;
  _url[0] = '\0';
#endif
}

void HelpBotDiscovery::loop() {
#ifdef ESP32
  if (_udp == nullptr) {
    return;
  }
  unsigned long now = millis();
  if (!_has_url && (now - _last_broadcast_ms) >= kDiscoveryIntervalMs) {
    _last_broadcast_ms = now;
    broadcastDiscovery();
  }

  int packet_size = _udp->parsePacket();
  if (packet_size > 0) {
    handleResponse(packet_size);
  }
#endif
}

bool HelpBotDiscovery::hasUrl() const {
#ifdef ESP32
  return _has_url;
#else
  return false;
#endif
}

const char *HelpBotDiscovery::getUrl() const {
#ifdef ESP32
  return _url;
#else
  return "";
#endif
}

#ifdef ESP32
void HelpBotDiscovery::broadcastDiscovery() {
#ifdef HELP_BOT_TOKEN
  const char *token = HELP_BOT_TOKEN;
#else
  const char *token = "";
#endif
  _udp->beginPacket(IPAddress(255, 255, 255, 255), HELP_BOT_DISCOVERY_PORT);
  _udp->print("HELPBOT_DISCOVERY|");
  _udp->print(token);
  _udp->endPacket();
}

void HelpBotDiscovery::handleResponse(int packet_size) {
  char buffer[192];
  int len = _udp->read(buffer, sizeof(buffer) - 1);
  if (len <= 0) {
    return;
  }
  buffer[len] = '\0';

  const char *prefix = "HELPBOT_URL|";
  if (strncmp(buffer, prefix, strlen(prefix)) != 0) {
    return;
  }

  char *saveptr = nullptr;
  strtok_r(buffer, "|", &saveptr);  // HELPBOT_URL
  char *url = strtok_r(nullptr, "|", &saveptr);
  char *token = strtok_r(nullptr, "|", &saveptr);
  if (!url || !token) {
    return;
  }

#ifdef HELP_BOT_TOKEN
  if (strcmp(token, HELP_BOT_TOKEN) != 0) {
    return;
  }
#endif

  strncpy(_url, url, sizeof(_url) - 1);
  _url[sizeof(_url) - 1] = '\0';
  _has_url = true;
}
#endif
