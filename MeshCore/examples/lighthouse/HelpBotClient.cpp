#include "HelpBotClient.h"

#ifdef ESP32
#include <HTTPClient.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include "secrets.h"
#endif

namespace {
bool starts_with(const char *text, const char *prefix) {
  if (!text || !prefix) {
    return false;
  }
  while (*prefix) {
    if (*text++ != *prefix++) {
      return false;
    }
  }
  return true;
}
}

HelpBotClient::HelpBotClient()
  : _enabled(false),
    _bot_url(nullptr),
    _bot_url_storage{0},
    _bot_token(nullptr) {}

void HelpBotClient::begin() {
#ifdef ESP32
#ifdef HELP_BOT_TOKEN
  _bot_token = HELP_BOT_TOKEN;
#else
  _bot_token = nullptr;
#endif
  if (_bot_token == nullptr || _bot_token[0] == '\0') {
    Serial.println("HelpBotClient: missing HELP_BOT_TOKEN");
    _enabled = false;
    return;
  }
  _enabled = (_bot_url != nullptr && _bot_url[0] != '\0');
  if (_enabled) {
    Serial.println("HelpBotClient: enabled");
  } else {
    Serial.println("HelpBotClient: waiting for discovery");
  }
#else
  _enabled = false;
#endif
}

void HelpBotClient::setUrl(const char *url) {
  if (!url || url[0] == '\0') {
    return;
  }
  strncpy(_bot_url_storage, url, sizeof(_bot_url_storage) - 1);
  _bot_url_storage[sizeof(_bot_url_storage) - 1] = '\0';
  _bot_url = _bot_url_storage;
  _enabled = (_bot_token != nullptr && _bot_token[0] != '\0');
  if (_enabled) {
    Serial.printf("HelpBotClient: discovered %s\n", _bot_url);
  }
}

bool HelpBotClient::isEnabled() const {
  return _enabled;
}

bool HelpBotClient::postMeshEvent(const char *text, const char *sender_name) {
  if (!_enabled || text == nullptr) {
    return false;
  }
#ifdef ESP32
  bool use_tls = starts_with(_bot_url, "https://");
  WiFiClient *base_client = nullptr;
  WiFiClientSecure secure_client;
  WiFiClient plain_client;

  if (use_tls) {
    secure_client.setInsecure();
    base_client = &secure_client;
  } else {
    base_client = &plain_client;
  }

  HTTPClient http;
  if (!http.begin(*base_client, _bot_url)) {
    Serial.println("HelpBotClient: http begin failed");
    return false;
  }

  http.addHeader("Content-Type", "text/plain");
  http.addHeader("X-Help-Token", _bot_token);
  if (sender_name && sender_name[0] != '\0') {
    http.addHeader("X-Help-Sender", sender_name);
  }

  int status = http.POST((uint8_t *)text, strlen(text));
  if (status <= 0) {
    Serial.printf("HelpBotClient: POST failed (%d)\n", status);
    http.end();
    return false;
  }
  if (status < 200 || status >= 300) {
    Serial.printf("HelpBotClient: POST status %d\n", status);
    http.end();
    return false;
  }
  http.end();
  return true;
#else
  Serial.printf("HelpBotClient: would send: %s\n", text);
  return true;
#endif
}
