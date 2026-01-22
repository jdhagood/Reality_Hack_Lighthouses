#include <Arduino.h>
#include <Mesh.h>
#include "LighthouseMesh.h"
#include "LightRing.h"
#include "LightChime.h"
#include "AudioStreamer.h"
#include "global_configs.h"
#include "HelpBotClient.h"
#include "HelpBotDiscovery.h"
#include "HelpGatewayServer.h"
#include "secrets.h"

#ifndef LIGHTHOUSE_NUMBER
#error "LIGHTHOUSE_NUMBER must be defined (1-30)"
#endif

#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  #include <InternalFileSystem.h>
#elif defined(RP2040_PLATFORM)
  #include <LittleFS.h>
#elif defined(ESP32)
  #include <LittleFS.h>
  #include <esp_partition.h>
  #include <esp_wifi.h>
  #include <WiFi.h>
  #include <WiFiUdp.h>
#endif

#ifdef ESP32
  #ifdef BLE_PIN_CODE
    #include <helpers/esp32/SerialBLEInterface.h>
    SerialBLEInterface serial_interface;
  #else
    #include <helpers/ArduinoSerialInterface.h>
    ArduinoSerialInterface serial_interface;
  #endif
#elif defined(RP2040_PLATFORM)
  #include <helpers/ArduinoSerialInterface.h>
  ArduinoSerialInterface serial_interface;
#elif defined(NRF52_PLATFORM)
  #ifdef BLE_PIN_CODE
    #include <helpers/nrf52/SerialBLEInterface.h>
    SerialBLEInterface serial_interface;
  #else
    #include <helpers/ArduinoSerialInterface.h>
    ArduinoSerialInterface serial_interface;
  #endif
#elif defined(STM32_PLATFORM)
  #include <helpers/ArduinoSerialInterface.h>
  ArduinoSerialInterface serial_interface;
#else
  #error "need to define a serial interface"
#endif

/* GLOBAL OBJECTS */
StdRNG fast_rng;
SimpleMeshTables tables;
LighthouseMesh the_mesh(radio_driver, fast_rng, rtc_clock, tables);
LightRing light_ring;
LightChime light_chime;
AudioStreamer audio_streamer;
HelpBotClient help_bot;
HelpBotDiscovery help_discovery;
HelpGatewayServer help_gateway;

/* Button handling */
#ifndef PIN_USER_BTN
#define PIN_USER_BTN 2
#endif

static const unsigned long DEBOUNCE_DELAY_MS = 50;
static bool last_button_state = HIGH;
static bool button_state = HIGH;
static unsigned long last_debounce_time = 0;
static unsigned long press_start_ms = 0;
static bool long_press_sent = false;
static uint8_t help_sfx_stage = 0;
static bool helpbot_hello_sent = false;
#ifdef ESP32
static WiFiUDP registration_udp;
static unsigned long last_registration_ms = 0;
static bool registration_started = false;
#endif

void halt() {
  while (1) ;
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.printf("\n\n=== Lighthouse #%d Starting ===\n", LIGHTHOUSE_NUMBER);
  
  light_ring.begin();
  light_ring.setIdleColor(LIGHTHOUSE_IDLE_R, LIGHTHOUSE_IDLE_G, LIGHTHOUSE_IDLE_B);
  light_ring.setIdleEnabled(true);
  light_ring.startStartupSpin(30, 30, 30, 80);

  board.begin();

  if (!radio_init()) { 
    Serial.println("ERROR: Radio initialization failed");
    halt(); 
  }

  fast_rng.begin(radio_get_rng_seed());

#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  InternalFS.begin();
  // No DataStore needed for lighthouse
#elif defined(RP2040_PLATFORM)
  LittleFS.begin();
#elif defined(ESP32)
  LittleFS.begin(true);
  size_t fs_total = LittleFS.totalBytes();
  size_t fs_used = LittleFS.usedBytes();
  size_t fs_free = fs_total >= fs_used ? (fs_total - fs_used) : 0;
  Serial.printf("LittleFS: total=%u bytes, used=%u bytes, free=%u bytes\n",
                (unsigned int)fs_total, (unsigned int)fs_used, (unsigned int)fs_free);
  const esp_partition_t *fs_part = esp_partition_find_first(
      ESP_PARTITION_TYPE_DATA,
      ESP_PARTITION_SUBTYPE_DATA_SPIFFS,
      "spiffs");
  if (fs_part) {
    Serial.printf("LittleFS partition: label=%s addr=0x%06x size=%u bytes\n",
                  fs_part->label,
                  (unsigned int)fs_part->address,
                  (unsigned int)fs_part->size);
  } else {
    Serial.println("LittleFS partition: not found");
  }
#else
  #error "need to define filesystem"
#endif

#ifdef ESP32
  const char *wifi_ssid = WIFI_SSID;
  const char *wifi_pass = WIFI_PASS;
  Serial.printf("WiFi: connecting to %s...\n", wifi_ssid);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  uint8_t base_mac[6];
  esp_read_mac(base_mac, ESP_MAC_WIFI_STA);
  esp_wifi_set_mac(WIFI_IF_STA, base_mac);
  WiFi.begin(wifi_ssid, wifi_pass, 0, NULL, true);
  unsigned long wifi_start = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - wifi_start) < 8000) {
    light_ring.loop();
    delay(250);
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("WiFi: connected, IP=%s\n", WiFi.localIP().toString().c_str());
    if (LIGHTHOUSE_NUMBER == 1) {
      help_bot.begin();
      help_discovery.begin();
      the_mesh.setHelpBotClient(&help_bot);
      help_gateway.begin(&the_mesh);
    }
    light_ring.finishStartup(true, 200);
  } else {
    Serial.println("WiFi: connection failed, continuing without help relay");
    light_ring.finishStartup(false, 200);
  }
#endif

  the_mesh.begin();

#ifdef BLE_PIN_CODE
  char dev_name[32+16];
  sprintf(dev_name, "%s%d", BLE_NAME_PREFIX, LIGHTHOUSE_NUMBER);
  uint32_t ble_pin = (uint32_t)BLE_PIN_CODE;
  serial_interface.begin(dev_name, ble_pin);
  Serial.printf("BLE started: %s (PIN: %lu)\n", dev_name, (unsigned long)ble_pin);
#else
  serial_interface.begin(Serial);
  Serial.println("Serial interface started");
#endif

  the_mesh.startInterface(serial_interface);

  // Configure button pin
  pinMode(PIN_USER_BTN, INPUT_PULLUP);
  last_button_state = digitalRead(PIN_USER_BTN);
  button_state = last_button_state;

  Serial.printf("Lighthouse #%d initialized successfully\n", LIGHTHOUSE_NUMBER);
  Serial.printf("Node name: %s\n", the_mesh.getNodeName());
  Serial.printf("Press button on GPIO %d to request help; hold 2s to cancel\n", PIN_USER_BTN);

  the_mesh.setLightRing(&light_ring);
  light_chime.begin();
  the_mesh.setLightChime(&light_chime);
  audio_streamer.begin();
  the_mesh.setAudioStreamer(&audio_streamer);
}

#ifdef ESP32
static void send_registration_packet(bool heartbeat) {
  if (WiFi.status() != WL_CONNECTED) {
    return;
  }
  IPAddress server_ip;
  if (!server_ip.fromString(REGISTRATION_SERVER_IP)) {
    return;
  }
  char lighthouse_id[8];
  snprintf(lighthouse_id, sizeof(lighthouse_id), "LH-%02d", LIGHTHOUSE_NUMBER);
  String ip = WiFi.localIP().toString();
  String mac = WiFi.macAddress();
  unsigned long uptime = millis() / 1000;
  char payload[196];
  snprintf(payload, sizeof(payload), "LHREG|%s|%s|%s|%s|%lu",
           lighthouse_id,
           ip.c_str(),
           mac.c_str(),
           FIRMWARE_VERSION,
           uptime);
  registration_udp.beginPacket(server_ip, REGISTRATION_SERVER_PORT);
  registration_udp.write(reinterpret_cast<const uint8_t *>(payload), strlen(payload));
  registration_udp.endPacket();
  Serial.printf("Registrar: sent %s\n", heartbeat ? "heartbeat" : "registration");
}
#endif

void loop() {
  the_mesh.loop();
  rtc_clock.tick();
  light_ring.loop();
  light_chime.loop();
  audio_streamer.loop();
  if (audio_streamer.isPlaying()) {
    light_ring.setAudioLevel(audio_streamer.getLevel());
  } else {
    light_ring.setAudioLevel(0.0f);
  }
  if (help_sfx_stage > 0 && !audio_streamer.isPlaying()) {
    if (help_sfx_stage == 1) {
      if (audio_streamer.playFile(SFX_BUTTON_PATH)) {
        help_sfx_stage = 2;
      }
    } else if (help_sfx_stage == 2) {
      if (audio_streamer.playFile(YOU_HAVE_REQUESTED_HELP_PATH)) {
        help_sfx_stage = 0;
      }
    }
  }
  help_gateway.loop();
  if (LIGHTHOUSE_NUMBER == 1) {
    help_discovery.loop();
    if (!help_bot.isEnabled() && help_discovery.hasUrl()) {
      help_bot.setUrl(help_discovery.getUrl());
    }
    if (help_bot.isEnabled() && !helpbot_hello_sent) {
      char hello[32];
      snprintf(hello, sizeof(hello), "HELP|HELLO|LH%02d", LIGHTHOUSE_NUMBER);
      helpbot_hello_sent = help_bot.postMeshEvent(hello, the_mesh.getNodeName());
    }
  }

#ifdef ESP32
  if (WiFi.status() == WL_CONNECTED) {
    if (!registration_started) {
      registration_udp.begin(0);
      registration_started = true;
      last_registration_ms = 0;
    }
    unsigned long now = millis();
    if (last_registration_ms == 0 || (now - last_registration_ms) >= REGISTRATION_HEARTBEAT_MS) {
      send_registration_packet(last_registration_ms != 0);
      last_registration_ms = now;
    }
  }
#endif

  // Button debouncing and detection
  int reading = digitalRead(PIN_USER_BTN);
  
  // Check if button state changed (noise or press)
  if (reading != last_button_state) {
    last_debounce_time = millis();
  }
  
  // If enough time has passed since last state change, update button state
  if ((millis() - last_debounce_time) > DEBOUNCE_DELAY_MS) {
    // If button state has changed and is now LOW (pressed)
    if (reading != button_state) {
      button_state = reading;
      
      // Button pressed (LOW because of pull-up)
      if (button_state == LOW) {
        press_start_ms = millis();
        long_press_sent = false;
        Serial.printf("Lighthouse #%d: Button pressed\n", LIGHTHOUSE_NUMBER);
        if (the_mesh.handleMailboxButton()) {
          return;
        }
        if (the_mesh.handleAnnouncementButton()) {
          return;
        }
        if (!the_mesh.isHelpActive()) {
          struct ColorChoice {
            const char *name;
            uint8_t r;
            uint8_t g;
            uint8_t b;
          };
          const ColorChoice choices[] = {
            {"RED", 255, 0, 0},
            {"ORANGE", 255, 128, 0},
            {"YELLOW", 255, 255, 0},
            {"GREEN", 0, 200, 0},
            {"BLUE", 0, 120, 255},
            {"VIOLET", 160, 0, 255},
          };
          size_t choice_index = (size_t)(millis() % (sizeof(choices) / sizeof(choices[0])));
          const ColorChoice &choice = choices[choice_index];
          if (the_mesh.requestHelp(choice.name)) {
            help_sfx_stage = 1;
            light_ring.setIdleColor(choice.r, choice.g, choice.b);
          }
        }
      }
    }
  }

  if (button_state == LOW && !long_press_sent) {
    unsigned long held_ms = millis() - press_start_ms;
    if (held_ms >= 2000 && the_mesh.isHelpActive() && !the_mesh.isAnnouncementActive() && !the_mesh.isMailboxActive()) {
      long_press_sent = true;
      Serial.printf("Lighthouse #%d: Long press detected, canceling help\n", LIGHTHOUSE_NUMBER);
      if (the_mesh.cancelHelp()) {
        if (!audio_streamer.isPlaying()) {
          audio_streamer.playFile(SFX_DEQUEUE_PATH);
        }
        light_ring.setPulseColor(255, 64, 64);
        light_ring.notifyChannelMessage();
      }
    }
  }
  
  last_button_state = reading;
}
