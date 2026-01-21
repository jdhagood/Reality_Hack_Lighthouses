#pragma once

#include <Arduino.h>

class LighthouseMesh;

class HelpGatewayServer {
public:
  HelpGatewayServer();

  void begin(LighthouseMesh *mesh);
  void loop();
  bool isEnabled() const;

private:
  LighthouseMesh *_mesh;
  bool _enabled;
#ifdef ESP32
  class WebServer *_server;
  const char *_token;
#endif

  void handleMeshPost();
};
