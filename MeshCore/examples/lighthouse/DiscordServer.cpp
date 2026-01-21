#include "DiscordServer.h"

#ifdef ESP32
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include "secrets.h"
#endif

DiscordServer::DiscordServer()
  : _enabled(false) {
#ifdef ESP32
  _bot_auth = nullptr;
  _channel_id = nullptr;
#endif
}

void DiscordServer::begin() {
#ifdef ESP32
  _bot_auth = DISCORD_BOT_AUTH;
  _channel_id = DISCORD_CHANNEL_ID;
  if (_bot_auth == nullptr || _channel_id == nullptr) {
    Serial.println("DiscordServer: missing credentials");
    _enabled = false;
    return;
  }
  if (strstr(_bot_auth, "REPLACE_WITH") || strstr(_channel_id, "REPLACE_WITH")) {
    Serial.println("DiscordServer: placeholder credentials, disabled");
    _enabled = false;
    return;
  }
  _enabled = true;
  Serial.println("DiscordServer: enabled");
#else
  _enabled = false;
#endif
}

bool DiscordServer::isEnabled() const {
  return _enabled;
}

void DiscordServer::sendChannelMessage(const char *text) {
  if (!_enabled || text == nullptr) {
    return;
  }
#ifdef ESP32
  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  String url = "https://discord.com/api/v10/channels/";
  url += _channel_id;
  url += "/messages";

  if (!http.begin(client, url)) {
    Serial.println("DiscordServer: http begin failed");
    return;
  }

  http.addHeader("Content-Type", "application/json");
  http.addHeader("Authorization", _bot_auth);

  String payload = "{\"content\":\"";
  for (const char *p = text; *p; ++p) {
    char c = *p;
    if (c == '\\' || c == '\"') {
      payload += '\\';
      payload += c;
    } else if (c == '\n') {
      payload += "\\n";
    } else if (c == '\r') {
      payload += "\\r";
    } else {
      payload += c;
    }
  }
  payload += "\"}";

  int status = http.POST(payload);
  if (status <= 0) {
    Serial.printf("DiscordServer: POST failed (%d)\n", status);
  } else if (status < 200 || status >= 300) {
    Serial.printf("DiscordServer: POST status %d\n", status);
  }
  http.end();
#else
  Serial.printf("DiscordServer: would send: %s\n", text);
#endif
}
