#include "HelpGatewayServer.h"

#include "LighthouseMesh.h"

#ifdef ESP32
#include <WebServer.h>
#include "secrets.h"
#endif

HelpGatewayServer::HelpGatewayServer()
  : _mesh(nullptr),
    _enabled(false)
#ifdef ESP32
    , _server(nullptr),
    _token(nullptr)
#endif
{
}

void HelpGatewayServer::begin(LighthouseMesh *mesh) {
  _mesh = mesh;
#ifdef ESP32
#ifdef HELP_BOT_TOKEN
  _token = HELP_BOT_TOKEN;
#else
  _token = nullptr;
#endif
#ifndef HELP_GATEWAY_PORT
#define HELP_GATEWAY_PORT 8081
#endif

  if (_token == nullptr || _token[0] == '\0') {
    Serial.println("HelpGatewayServer: missing HELP_BOT_TOKEN");
    _enabled = false;
    return;
  }

  _server = new WebServer(HELP_GATEWAY_PORT);
#if defined(ESP32)
  const char *header_keys[] = {"X-Help-Token"};
  _server->collectHeaders(header_keys, 1);
#endif
  _server->on("/mesh", HTTP_POST, [this]() { this->handleMeshPost(); });
  _server->begin();
  _enabled = true;
  Serial.printf("HelpGatewayServer: listening on port %d\n", HELP_GATEWAY_PORT);
#else
  _enabled = false;
#endif
}

void HelpGatewayServer::loop() {
#ifdef ESP32
  if (_enabled && _server) {
    _server->handleClient();
  }
#endif
}

bool HelpGatewayServer::isEnabled() const {
  return _enabled;
}

void HelpGatewayServer::handleMeshPost() {
#ifdef ESP32
  if (!_server) {
    return;
  }
  if (_token && _token[0] != '\0') {
    String header_token = _server->header("X-Help-Token");
    if (header_token.length() == 0 || header_token != _token) {
      Serial.printf("HelpGatewayServer: unauthorized request (header len=%d, expected len=%u)\n",
                    header_token.length(), (unsigned int)strlen(_token));
      _server->send(401, "text/plain", "unauthorized");
      return;
    }
  }
  String body = _server->arg("plain");
  if (body.length() == 0) {
    Serial.println("HelpGatewayServer: missing body");
    _server->send(400, "text/plain", "missing body");
    return;
  }
  Serial.printf("HelpGatewayServer: received %s\n", body.c_str());
  if (_mesh) {
    _mesh->sendHelpBroadcast(body.c_str());
    _mesh->handleHelpPayload(body.c_str());
  }
  _server->send(200, "text/plain", "ok");
#endif
}
