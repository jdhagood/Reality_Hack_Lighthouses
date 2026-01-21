#include "LighthouseMesh.h"
#include "LightRing.h"
#include "LightChime.h"
#include "AudioStreamer.h"
#include "DiscordServer.h"
#include "HelpBotClient.h"
#include "global_configs.h"
#include <Arduino.h>
#include <Mesh.h>
#include <helpers/ArduinoHelpers.h>
#include <helpers/StaticPoolPacketManager.h>
#ifdef ESP32
#include <WiFi.h>
#endif

#ifndef LIGHTHOUSE_NUMBER
#error "LIGHTHOUSE_NUMBER must be defined (1-30)"
#endif

LighthouseMesh::LighthouseMesh(mesh::Radio &radio, mesh::RNG &rng, mesh::RTCClock &rtc, SimpleMeshTables &tables)
    : BaseChatMesh(radio, *new ArduinoMillis(), rng, rtc, *new StaticPoolPacketManager(16), tables) {
  _serial = NULL;
  _lighthouse_channel = NULL;
  _light_ring = NULL;
  _light_chime = NULL;
  _audio_streamer = NULL;
  _discord_server = NULL;
  _help_bot_client = NULL;
  _last_button_send = 0;
  _active_ble_pin = 0;
  _help_state = HelpState::Idle;
  _active_request_id[0] = '\0';
  _help_color_name[0] = '\0';
  _request_seq = 0;
  _announcement_active = false;
  _announcement_acknowledged = false;
  _announcement_can_stop = false;
  _announcement_audio_playing = false;
  _announcement_eom_pending = false;
  _announcement_eom_playing = false;
  _announcement_eom_at_ms = 0;
  _announcement_next_play_ms = 0;
  _announcement_url[0] = '\0';
  _mailbox_count = 0;
  _mailbox_active = false;
  _mailbox_open = false;
  _mailbox_alerting = false;
  _mailbox_can_advance = false;
  _mailbox_audio_playing = false;
  _mailbox_eom_pending = false;
  _mailbox_eom_playing = false;
  _mailbox_eom_at_ms = 0;
  _mailbox_next_play_ms = 0;
  _mailbox_current_url[0] = '\0';
  _ack_cache_head = 0;
  for (uint8_t i = 0; i < kAckCacheSize; ++i) {
    _ack_cache[i][0] = '\0';
  }
  
  // Create node name based on lighthouse number
  sprintf(_node_name, "Lighthouse-%d", LIGHTHOUSE_NUMBER);
}

void LighthouseMesh::begin() {
  // Initialize the base mesh
  mesh::Mesh::begin();
  
  // Create the lighthouse network channel
  _lighthouse_channel = addChannel("Lighthouse Network", LIGHTHOUSE_CHANNEL_PSK);
  if (_lighthouse_channel == NULL) {
    Serial.println("ERROR: Failed to create lighthouse channel");
  } else {
    Serial.printf("Lighthouse #%d: Channel created successfully\n", LIGHTHOUSE_NUMBER);
  }
}

void LighthouseMesh::startInterface(BaseSerialInterface &serial) {
  _serial = &serial;
  serial.enable();
}

void LighthouseMesh::setLightRing(LightRing *ring) {
  _light_ring = ring;
}

void LighthouseMesh::setLightChime(LightChime *chime) {
  _light_chime = chime;
}

void LighthouseMesh::setAudioStreamer(AudioStreamer *streamer) {
  _audio_streamer = streamer;
}

void LighthouseMesh::setDiscordServer(DiscordServer *server) {
  _discord_server = server;
}

void LighthouseMesh::setHelpBotClient(HelpBotClient *client) {
  _help_bot_client = client;
}

void LighthouseMesh::setHelpBotUrl(const char *url) {
  if (_help_bot_client) {
    _help_bot_client->setUrl(url);
  }
}

void LighthouseMesh::loop() {
  mesh::Mesh::loop();
  updateAnnouncement();
  updateMailbox();
}

bool LighthouseMesh::sendButtonPressMessage() {
  unsigned long now = millis();
  
  // Rate limiting: prevent sending too frequently
  if (now - _last_button_send < BUTTON_SEND_COOLDOWN_MS_VALUE) {
    return false;
  }
  
  if (_lighthouse_channel == NULL) {
    return false;
  }
  
  // Create message: "Lighthouse <NUMBER>: Button Pressed"
  char message[64];
  sprintf(message, "Lighthouse %d: Button Pressed", LIGHTHOUSE_NUMBER);
  
  uint32_t timestamp = getRTCClock()->getCurrentTime();
  bool success = sendGroupMessage(timestamp, _lighthouse_channel->channel, _node_name, message, strlen(message));
  
  if (success) {
    _last_button_send = now;
    Serial.printf("Lighthouse #%d: Sent button press message\n", LIGHTHOUSE_NUMBER);
    if (_discord_server) {
      _discord_server->sendChannelMessage(message);
    }
  } else {
    Serial.printf("Lighthouse #%d: Failed to send button press message\n", LIGHTHOUSE_NUMBER);
  }
  
  return success;
}

bool LighthouseMesh::requestHelp(const char *color_name) {
  unsigned long now = millis();
  if (_help_state != HelpState::Idle) {
    return false;
  }
  if (now - _last_button_send < BUTTON_SEND_COOLDOWN_MS_VALUE) {
    return false;
  }
  if (_lighthouse_channel == NULL) {
    return false;
  }

  uint32_t timestamp = getRTCClock()->getCurrentTime();
  _request_seq++;
  snprintf(_active_request_id, sizeof(_active_request_id), "LH%02d-%lu-%u",
           LIGHTHOUSE_NUMBER, (unsigned long)timestamp, _request_seq);
  if (color_name && color_name[0] != '\0') {
    strncpy(_help_color_name, color_name, sizeof(_help_color_name) - 1);
    _help_color_name[sizeof(_help_color_name) - 1] = '\0';
  } else {
    _help_color_name[0] = '\0';
  }

  char message[96];
  if (_help_color_name[0] != '\0') {
    snprintf(message, sizeof(message), "HELP|REQ|%s|%d|%lu|%s",
             _active_request_id, LIGHTHOUSE_NUMBER, (unsigned long)timestamp, _help_color_name);
  } else {
    snprintf(message, sizeof(message), "HELP|REQ|%s|%d|%lu",
             _active_request_id, LIGHTHOUSE_NUMBER, (unsigned long)timestamp);
  }

  bool success = sendGroupMessage(timestamp, _lighthouse_channel->channel, _node_name, message, strlen(message));
  if (success) {
    _help_state = HelpState::Pending;
    _last_button_send = now;
    Serial.printf("Lighthouse #%d: Help requested (%s)\n", LIGHTHOUSE_NUMBER, _active_request_id);
    forwardHelpMessage("REQ", _active_request_id, message);
    if (_light_ring) {
      _light_ring->setOrbiting(true, HELP_ORBIT_INTERVAL_MS);
    }
  } else {
    Serial.printf("Lighthouse #%d: Failed to send help request\n", LIGHTHOUSE_NUMBER);
  }
  return success;
}

bool LighthouseMesh::cancelHelp() {
  if (_help_state == HelpState::Idle || _active_request_id[0] == '\0') {
    return false;
  }
  if (_lighthouse_channel == NULL) {
    return false;
  }

  uint32_t timestamp = getRTCClock()->getCurrentTime();
  char message[96];
  snprintf(message, sizeof(message), "HELP|CANCEL|%s|%d|%lu",
           _active_request_id, LIGHTHOUSE_NUMBER, (unsigned long)timestamp);

  bool success = sendGroupMessage(timestamp, _lighthouse_channel->channel, _node_name, message, strlen(message));
  if (success) {
    Serial.printf("Lighthouse #%d: Help canceled (%s)\n", LIGHTHOUSE_NUMBER, _active_request_id);
    forwardHelpMessage("CANCEL", _active_request_id, message);
    _help_state = HelpState::Idle;
    _active_request_id[0] = '\0';
    _help_color_name[0] = '\0';
    if (_light_ring) {
      _light_ring->setIdleColor(LIGHTHOUSE_IDLE_R, LIGHTHOUSE_IDLE_G, LIGHTHOUSE_IDLE_B);
    }
  } else {
    Serial.printf("Lighthouse #%d: Failed to cancel help request\n", LIGHTHOUSE_NUMBER);
  }
  return success;
}

bool LighthouseMesh::isHelpActive() const {
  return _help_state != HelpState::Idle;
}

bool LighthouseMesh::isHelpClaimed() const {
  return _help_state == HelpState::Claimed;
}

bool LighthouseMesh::isAnnouncementActive() const {
  return _announcement_active;
}

bool LighthouseMesh::isMailboxActive() const {
  return _mailbox_active;
}

bool LighthouseMesh::handleAnnouncementButton() {
  if (!_announcement_active) {
    return false;
  }
  if (!_announcement_acknowledged) {
    _announcement_acknowledged = true;
    _announcement_can_stop = false;
    _announcement_audio_playing = false;
    _announcement_next_play_ms = 0;
    if (_light_ring) {
      _light_ring->setBlinking(false);
    }
    return true;
  }
  if (_announcement_can_stop) {
    stopAnnouncement();
    return true;
  }
  return true;
}

void LighthouseMesh::startAnnouncement(const char *url) {
  if (!url || url[0] == '\0') {
    return;
  }
  strncpy(_announcement_url, url, sizeof(_announcement_url) - 1);
  _announcement_url[sizeof(_announcement_url) - 1] = '\0';
  _announcement_active = true;
  _announcement_acknowledged = false;
  _announcement_can_stop = false;
  _announcement_audio_playing = false;
  _announcement_eom_pending = false;
  _announcement_eom_playing = false;
  _announcement_eom_at_ms = 0;
  _announcement_next_play_ms = 0;
  if (_light_ring) {
    _light_ring->setBlinking(true, 255, 255, 255, 500);
  }
}

void LighthouseMesh::stopAnnouncement() {
  _announcement_active = false;
  _announcement_acknowledged = false;
  _announcement_can_stop = false;
  _announcement_audio_playing = false;
  _announcement_eom_pending = false;
  _announcement_eom_playing = false;
  _announcement_eom_at_ms = 0;
  _announcement_next_play_ms = 0;
  _announcement_url[0] = '\0';
  if (_audio_streamer) {
    _audio_streamer->stop();
  }
  if (_light_ring) {
    _light_ring->setBlinking(false);
    restoreIdleColor();
  }
}

void LighthouseMesh::updateAnnouncement() {
  if (!_announcement_active || !_audio_streamer) {
    return;
  }

  bool now_playing = _audio_streamer->isPlaying();
  unsigned long now = millis();

  if (_announcement_audio_playing && !now_playing) {
    _announcement_audio_playing = false;
    _announcement_eom_pending = true;
    _announcement_eom_at_ms = now + 500;
    if (_announcement_acknowledged) {
      _announcement_can_stop = false;
    }
  }

  if (_announcement_eom_pending && !_announcement_eom_playing && !now_playing && now >= _announcement_eom_at_ms) {
    if (_audio_streamer->playFile(EOM_PATH)) {
      _announcement_eom_playing = true;
    }
  }

  if (_announcement_eom_playing && !now_playing) {
    _announcement_eom_playing = false;
    _announcement_eom_pending = false;
    if (_announcement_acknowledged) {
      _announcement_can_stop = true;
      _announcement_next_play_ms = now + 3000;
    } else {
      _announcement_next_play_ms = now + 5000;
    }
  }

  if (_announcement_eom_pending || _announcement_eom_playing) {
    return;
  }

  if (!now_playing && now >= _announcement_next_play_ms) {
    if (_announcement_acknowledged) {
      if (_announcement_url[0] != '\0') {
        if (_audio_streamer->play(_announcement_url)) {
          _announcement_audio_playing = true;
        }
      }
    } else {
      if (_audio_streamer->playFile(MAIL_ALERT_PATH)) {
        _announcement_audio_playing = true;
      }
    }
  }
}

void LighthouseMesh::enqueueMailbox(const char *url) {
  if (!url || url[0] == '\0') {
    return;
  }
  if (_mailbox_count >= MAILBOX_QUEUE_SIZE) {
    for (uint8_t i = 1; i < _mailbox_count; ++i) {
      strncpy(_mailbox_queue[i - 1], _mailbox_queue[i], sizeof(_mailbox_queue[i - 1]) - 1);
      _mailbox_queue[i - 1][sizeof(_mailbox_queue[i - 1]) - 1] = '\0';
    }
    _mailbox_count = MAILBOX_QUEUE_SIZE - 1;
  }
  strncpy(_mailbox_queue[_mailbox_count], url, sizeof(_mailbox_queue[_mailbox_count]) - 1);
  _mailbox_queue[_mailbox_count][sizeof(_mailbox_queue[_mailbox_count]) - 1] = '\0';
  _mailbox_count++;
}

bool LighthouseMesh::dequeueMailbox(char *out, size_t out_len) {
  if (!out || out_len == 0 || _mailbox_count == 0) {
    return false;
  }
  uint8_t index = (uint8_t)(_mailbox_count - 1);
  strncpy(out, _mailbox_queue[index], out_len - 1);
  out[out_len - 1] = '\0';
  _mailbox_count--;
  return true;
}

void LighthouseMesh::startMailboxAlert() {
  _mailbox_active = true;
  _mailbox_open = false;
  _mailbox_alerting = true;
  _mailbox_can_advance = false;
  _mailbox_audio_playing = false;
  _mailbox_eom_pending = false;
  _mailbox_eom_playing = false;
  _mailbox_eom_at_ms = 0;
  _mailbox_next_play_ms = 0;
  _mailbox_current_url[0] = '\0';
  if (_light_ring) {
    _light_ring->setBlinking(true, 255, 255, 255, 500);
  }
}

void LighthouseMesh::stopMailbox() {
  _mailbox_active = false;
  _mailbox_open = false;
  _mailbox_alerting = false;
  _mailbox_can_advance = false;
  _mailbox_audio_playing = false;
  _mailbox_eom_pending = false;
  _mailbox_eom_playing = false;
  _mailbox_eom_at_ms = 0;
  _mailbox_next_play_ms = 0;
  _mailbox_count = 0;
  _mailbox_current_url[0] = '\0';
  if (_audio_streamer) {
    _audio_streamer->stop();
  }
  if (_light_ring) {
    _light_ring->setBlinking(false);
    restoreIdleColor();
  }
}

void LighthouseMesh::updateMailbox() {
  if (!_mailbox_active || !_audio_streamer) {
    return;
  }
  if (_announcement_active) {
    return;
  }
  if (!_mailbox_open && !_mailbox_alerting) {
    _mailbox_alerting = true;
    if (_light_ring) {
      _light_ring->setBlinking(true, 255, 255, 255, 500);
    }
  }

  unsigned long now = millis();
  bool now_playing = _audio_streamer->isPlaying();

  if (_mailbox_audio_playing && !now_playing) {
    _mailbox_audio_playing = false;
    if (_mailbox_open) {
      _mailbox_eom_pending = true;
      _mailbox_eom_at_ms = now + 500;
      _mailbox_can_advance = false;
    } else {
      _mailbox_next_play_ms = now + MAIL_ALERT_INTERVAL_MS;
    }
  }

  if (_mailbox_eom_pending && !_mailbox_eom_playing && !now_playing && now >= _mailbox_eom_at_ms) {
    if (_audio_streamer->playFile(EOM_PATH)) {
      _mailbox_eom_playing = true;
    }
  }

  if (_mailbox_eom_playing && !now_playing) {
    _mailbox_eom_playing = false;
    _mailbox_eom_pending = false;
    if (_mailbox_open) {
      _mailbox_can_advance = true;
      _mailbox_next_play_ms = now + 3000;
    } else {
      _mailbox_next_play_ms = now + MAIL_ALERT_INTERVAL_MS;
    }
  }

  if (_mailbox_eom_pending || _mailbox_eom_playing) {
    return;
  }

  if (!now_playing && now >= _mailbox_next_play_ms) {
    if (_mailbox_open) {
      if (_mailbox_current_url[0] == '\0') {
        char next_url[192];
        if (dequeueMailbox(next_url, sizeof(next_url))) {
          strncpy(_mailbox_current_url, next_url, sizeof(_mailbox_current_url) - 1);
          _mailbox_current_url[sizeof(_mailbox_current_url) - 1] = '\0';
          _mailbox_can_advance = false;
        } else {
          stopMailbox();
          return;
        }
      }
      if (_mailbox_current_url[0] != '\0') {
        if (_audio_streamer->play(_mailbox_current_url)) {
          _mailbox_audio_playing = true;
        }
      }
    } else {
      if (_audio_streamer->playFile(MAIL_ALERT_PATH)) {
        _mailbox_audio_playing = true;
      }
    }
  }
}

void LighthouseMesh::restoreIdleColor() {
  if (!_light_ring) {
    return;
  }
  if (_help_state != HelpState::Idle && _help_color_name[0] != '\0') {
    if (strcmp(_help_color_name, "RED") == 0) {
      _light_ring->setIdleColor(255, 0, 0);
    } else if (strcmp(_help_color_name, "ORANGE") == 0) {
      _light_ring->setIdleColor(255, 128, 0);
    } else if (strcmp(_help_color_name, "YELLOW") == 0) {
      _light_ring->setIdleColor(255, 255, 0);
    } else if (strcmp(_help_color_name, "GREEN") == 0) {
      _light_ring->setIdleColor(0, 200, 0);
    } else if (strcmp(_help_color_name, "BLUE") == 0) {
      _light_ring->setIdleColor(0, 120, 255);
    } else if (strcmp(_help_color_name, "VIOLET") == 0) {
      _light_ring->setIdleColor(160, 0, 255);
    } else {
      _light_ring->setIdleColor(LIGHTHOUSE_IDLE_R, LIGHTHOUSE_IDLE_G, LIGHTHOUSE_IDLE_B);
    }
    return;
  }
  _light_ring->setIdleColor(LIGHTHOUSE_IDLE_R, LIGHTHOUSE_IDLE_G, LIGHTHOUSE_IDLE_B);
  _light_ring->setOrbiting(false, HELP_ORBIT_INTERVAL_MS);
}

bool LighthouseMesh::handleMailboxButton() {
  if (!_mailbox_active) {
    return false;
  }
  if (!_mailbox_open) {
    _mailbox_open = true;
    _mailbox_alerting = false;
    _mailbox_can_advance = false;
    _mailbox_audio_playing = false;
    _mailbox_eom_pending = false;
    _mailbox_eom_playing = false;
    _mailbox_eom_at_ms = 0;
    _mailbox_next_play_ms = 0;
    if (_light_ring) {
      _light_ring->setBlinking(false);
    }
    return true;
  }
  if (_mailbox_can_advance) {
    _mailbox_can_advance = false;
    _mailbox_audio_playing = false;
    _mailbox_eom_pending = false;
    _mailbox_eom_playing = false;
    _mailbox_eom_at_ms = 0;
    _mailbox_next_play_ms = 0;
    char next_url[192];
    if (!dequeueMailbox(next_url, sizeof(next_url))) {
      stopMailbox();
      return true;
    }
    strncpy(_mailbox_current_url, next_url, sizeof(_mailbox_current_url) - 1);
    _mailbox_current_url[sizeof(_mailbox_current_url) - 1] = '\0';
    return true;
  }
  return true;
}

const char *LighthouseMesh::getActiveRequestId() const {
  return _active_request_id;
}

void LighthouseMesh::sendHelpBroadcast(const char *text) {
  if (!text || _lighthouse_channel == NULL) {
    return;
  }
  uint32_t timestamp = getRTCClock()->getCurrentTime();
  sendGroupMessage(timestamp, _lighthouse_channel->channel, _node_name, text, strlen(text));
}

void LighthouseMesh::handleHelpPayload(const char *text) {
  handleHelpMessage(text);
}

const char *LighthouseMesh::getNodeName() {
  return _node_name;
}

uint32_t LighthouseMesh::getBLEPin() {
  if (_active_ble_pin == 0) {
    // Generate a PIN based on lighthouse number (ensures uniqueness)
    _active_ble_pin = 100000 + LIGHTHOUSE_NUMBER;
  }
  return _active_ble_pin;
}

// Override methods from BaseChatMesh
float LighthouseMesh::getAirtimeBudgetFactor() const {
  return 1.0f;
}

int LighthouseMesh::getInterferenceThreshold() const {
  return 0;
}

int LighthouseMesh::calcRxDelay(float score, uint32_t air_time) const {
  return 0;
}

uint8_t LighthouseMesh::getExtraAckTransmitCount() const {
  return 0;
}

bool LighthouseMesh::filterRecvFloodPacket(mesh::Packet* packet) {
  return false;
}

void LighthouseMesh::sendFloodScoped(const ContactInfo& recipient, mesh::Packet* pkt, uint32_t delay_millis) {
  sendFlood(pkt, delay_millis);
}

void LighthouseMesh::sendFloodScoped(const mesh::GroupChannel& channel, mesh::Packet* pkt, uint32_t delay_millis) {
  sendFlood(pkt, delay_millis);
}

void LighthouseMesh::logRxRaw(float snr, float rssi, const uint8_t raw[], int len) {
  // Optional: log received packets for debugging
}

bool LighthouseMesh::isAutoAddEnabled() const {
  return false; // Don't auto-add contacts for lighthouse network
}

bool LighthouseMesh::onContactPathRecv(ContactInfo& from, uint8_t* in_path, uint8_t in_path_len, uint8_t* out_path, uint8_t out_path_len, uint8_t extra_type, uint8_t* extra, uint8_t extra_len) {
  return false;
}

void LighthouseMesh::onDiscoveredContact(ContactInfo &contact, bool is_new, uint8_t path_len, const uint8_t* path) {
  Serial.printf("Lighthouse #%d: %s contact %s (path_len=%u)\n",
                LIGHTHOUSE_NUMBER,
                is_new ? "Discovered" : "Updated",
                contact.name,
                (unsigned int)path_len);
}

void LighthouseMesh::onContactPathUpdated(const ContactInfo &contact) {
  Serial.printf("Lighthouse #%d: Contact path updated for %s (out_path_len=%u)\n",
                LIGHTHOUSE_NUMBER,
                contact.name,
                (unsigned int)contact.out_path_len);
}

ContactInfo* LighthouseMesh::processAck(const uint8_t *data) {
  return NULL; // Not needed for lighthouse network
}

void LighthouseMesh::onMessageRecv(const ContactInfo &from, mesh::Packet *pkt, uint32_t sender_timestamp, const char *text) {
  Serial.printf("Lighthouse #%d: Received message from %s: %s\n", LIGHTHOUSE_NUMBER, from.name, text);
  if (_discord_server) {
    char message[192];
    snprintf(message, sizeof(message), "Lighthouse %d received from %s: %s", LIGHTHOUSE_NUMBER, from.name, text ? text : "");
    _discord_server->sendChannelMessage(message);
  }
}

void LighthouseMesh::onCommandDataRecv(const ContactInfo &from, mesh::Packet *pkt, uint32_t sender_timestamp, const char *text) {
  // Not used in lighthouse network
}

void LighthouseMesh::onSignedMessageRecv(const ContactInfo &from, mesh::Packet *pkt, uint32_t sender_timestamp, const uint8_t *sender_prefix, const char *text) {
  // Not used in lighthouse network
}

void LighthouseMesh::onChannelMessageRecv(const mesh::GroupChannel &channel, mesh::Packet *pkt, uint32_t timestamp, const char *text) {
  Serial.printf("Lighthouse #%d: Channel message: %s\n", LIGHTHOUSE_NUMBER, text);
  if (text && handleHelpMessage(text)) {
    return;
  }
  if (_light_ring) {
    // No generic effects for non-help channel messages.
  }
}

uint8_t LighthouseMesh::onContactRequest(const ContactInfo &contact, uint32_t sender_timestamp, const uint8_t *data, uint8_t len, uint8_t *reply) {
  // Not used in lighthouse network - return 0 to indicate no response
  return 0;
}

void LighthouseMesh::onContactResponse(const ContactInfo &contact, const uint8_t *data, uint8_t len) {
  // Not used in lighthouse network
}

uint32_t LighthouseMesh::calcFloodTimeoutMillisFor(uint32_t pkt_airtime_millis) const {
  return pkt_airtime_millis * 16; // Standard flood timeout
}

uint32_t LighthouseMesh::calcDirectTimeoutMillisFor(uint32_t pkt_airtime_millis, uint8_t path_len) const {
  return pkt_airtime_millis * (6 * path_len + 250); // Standard direct timeout
}

void LighthouseMesh::onSendTimeout() {
  // Optional: handle send timeouts
}

bool LighthouseMesh::isAcked(const char *key) const {
  if (!key || key[0] == '\0') {
    return false;
  }
  for (uint8_t i = 0; i < kAckCacheSize; ++i) {
    if (strcmp(_ack_cache[i], key) == 0) {
      return true;
    }
  }
  return false;
}

void LighthouseMesh::rememberAck(const char *key) {
  if (!key || key[0] == '\0') {
    return;
  }
  strncpy(_ack_cache[_ack_cache_head], key, sizeof(_ack_cache[_ack_cache_head]) - 1);
  _ack_cache[_ack_cache_head][sizeof(_ack_cache[_ack_cache_head]) - 1] = '\0';
  _ack_cache_head = (uint8_t)((_ack_cache_head + 1) % kAckCacheSize);
}

void LighthouseMesh::broadcastAck(const char *type, const char *req_id) {
  if (!type || !req_id || _lighthouse_channel == NULL) {
    return;
  }
  char ack_key[40];
  snprintf(ack_key, sizeof(ack_key), "%s|%s", type, req_id);
  rememberAck(ack_key);

  uint32_t timestamp = getRTCClock()->getCurrentTime();
  char message[96];
  snprintf(message, sizeof(message), "HELP|ACK|%s|%s|%d|%lu",
           type, req_id, LIGHTHOUSE_NUMBER, (unsigned long)timestamp);
  sendGroupMessage(timestamp, _lighthouse_channel->channel, _node_name, message, strlen(message));
}

bool LighthouseMesh::forwardHelpMessage(const char *type, const char *req_id, const char *text) {
  if (!type || !req_id || !text) {
    return false;
  }
  char ack_key[40];
  snprintf(ack_key, sizeof(ack_key), "%s|%s", type, req_id);
  if (isAcked(ack_key)) {
    Serial.printf("Help relay: already acked %s\n", ack_key);
    return false;
  }
#ifdef ESP32
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Help relay: WiFi not connected");
    return false;
  }
#endif
  if (_help_bot_client && _help_bot_client->isEnabled()) {
    if (_help_bot_client->postMeshEvent(text, _node_name)) {
      broadcastAck(type, req_id);
      Serial.printf("Help relay: forwarded %s %s\n", type, req_id);
      return true;
    }
    Serial.printf("Help relay: post failed for %s %s\n", type, req_id);
    return false;
  }
  Serial.println("Help relay: client disabled");
  return false;
}

bool LighthouseMesh::handleHelpMessage(const char *text) {
  if (!text) {
    return false;
  }
  const char *payload = text;
  if (strncmp(payload, "HELP|", 5) != 0) {
    payload = strstr(text, "HELP|");
    if (!payload) {
      return false;
    }
  }

  char buffer[192];
  strncpy(buffer, payload, sizeof(buffer) - 1);
  buffer[sizeof(buffer) - 1] = '\0';

  char *saveptr = nullptr;
  char *token = strtok_r(buffer, "|", &saveptr);
  if (!token || strcmp(token, "HELP") != 0) {
    return true;
  }
  const char *type = strtok_r(nullptr, "|", &saveptr);
  if (!type) {
    return true;
  }

  if (strcmp(type, "PING") == 0) {
    const char *ping_id = strtok_r(nullptr, "|", &saveptr);
    if (!ping_id) {
      return true;
    }
    uint32_t timestamp = getRTCClock()->getCurrentTime();
    char message[96];
    snprintf(message, sizeof(message), "HELP|PONG|%s|%d|%lu",
             ping_id, LIGHTHOUSE_NUMBER, (unsigned long)timestamp);
    sendHelpBroadcast(message);
    char ack_key[40];
    snprintf(ack_key, sizeof(ack_key), "PONG|%s|%d", ping_id, LIGHTHOUSE_NUMBER);
    if (!isAcked(ack_key) && _help_bot_client && _help_bot_client->isEnabled()) {
      rememberAck(ack_key);
      if (_help_bot_client->postMeshEvent(message, _node_name)) {
        Serial.printf("Help relay: forwarded PONG %s\n", ack_key);
      }
    }
    return true;
  }

  if (strcmp(type, "PONG") == 0) {
    const char *ping_id = strtok_r(nullptr, "|", &saveptr);
    const char *lh_str = strtok_r(nullptr, "|", &saveptr);
    if (!ping_id || !lh_str) {
      return true;
    }
    char ack_key[40];
    snprintf(ack_key, sizeof(ack_key), "PONG|%s|%s", ping_id, lh_str);
    if (!isAcked(ack_key) && _help_bot_client && _help_bot_client->isEnabled()) {
      rememberAck(ack_key);
      if (_help_bot_client->postMeshEvent(payload, _node_name)) {
        Serial.printf("Help relay: forwarded PONG %s\n", ack_key);
      }
    }
    return true;
  }

  if (strcmp(type, "AUDIO") == 0 || strcmp(type, "ANNOUNCE") == 0 || strcmp(type, "MAIL") == 0) {
    const char *target = strtok_r(nullptr, "|", &saveptr);
    const char *url = strtok_r(nullptr, "|", &saveptr);
    if (!target || !url) {
      return true;
    }
    bool matches = false;
    if (strcmp(target, "ALL") == 0) {
      matches = true;
    } else {
      int target_id = atoi(target);
      matches = (target_id == LIGHTHOUSE_NUMBER);
    }
    if (matches) {
      Serial.printf("Audio request for lighthouse %d: %s\n", LIGHTHOUSE_NUMBER, url);
      if (strcmp(type, "ANNOUNCE") == 0) {
        startAnnouncement(url);
      } else if (strcmp(type, "MAIL") == 0) {
        enqueueMailbox(url);
        if (!_mailbox_active) {
          if (_announcement_active) {
            _mailbox_active = true;
            _mailbox_alerting = false;
          } else {
            startMailboxAlert();
          }
        }
      } else if (_audio_streamer && !_audio_streamer->isPlaying()) {
        _audio_streamer->play(url);
      }
    }
    return true;
  }

  if (strcmp(type, "ACK") == 0) {
    const char *ack_type = strtok_r(nullptr, "|", &saveptr);
    const char *req_id = strtok_r(nullptr, "|", &saveptr);
    if (!ack_type || !req_id) {
      return true;
    }
    char ack_key[40];
    snprintf(ack_key, sizeof(ack_key), "%s|%s", ack_type, req_id);
    rememberAck(ack_key);
    return true;
  }

  if (strcmp(type, "DETAILS") == 0) {
    const char *req_id = strtok_r(nullptr, "|", &saveptr);
    const char *lh_str = strtok_r(nullptr, "|", &saveptr);
    const char *reason = strtok_r(nullptr, "|", &saveptr);
    if (!req_id || !lh_str) {
      return true;
    }
    int lh_id = atoi(lh_str);
    if (lh_id == LIGHTHOUSE_NUMBER && strcmp(_active_request_id, req_id) == 0) {
      if (_audio_streamer && !_audio_streamer->isPlaying()) {
        _audio_streamer->playFile(MENTOUR_ON_THEIR_WAY_PATH);
      }
      if (reason && reason[0] != '\0') {
        Serial.printf("Help details: %s\n", reason);
      }
    }
    return true;
  }

  const char *req_id = strtok_r(nullptr, "|", &saveptr);
  const char *lh_str = strtok_r(nullptr, "|", &saveptr);
  if (!req_id || !lh_str) {
    return true;
  }
  int lh_id = atoi(lh_str);
  const char *color_name = strtok_r(nullptr, "|", &saveptr);
  Serial.printf("Help msg: %s req=%s lh=%d\n", type, req_id, lh_id);

  if (strcmp(type, "REQ") == 0) {
    forwardHelpMessage(type, req_id, payload);
    if (lh_id == LIGHTHOUSE_NUMBER && _help_state == HelpState::Idle) {
      strncpy(_active_request_id, req_id, sizeof(_active_request_id) - 1);
      _active_request_id[sizeof(_active_request_id) - 1] = '\0';
      _help_state = HelpState::Pending;
      if (color_name && color_name[0] != '\0') {
        strncpy(_help_color_name, color_name, sizeof(_help_color_name) - 1);
        _help_color_name[sizeof(_help_color_name) - 1] = '\0';
        if (_light_ring) {
          if (strcmp(color_name, "RED") == 0) {
            _light_ring->setIdleColor(255, 0, 0);
          } else if (strcmp(color_name, "ORANGE") == 0) {
            _light_ring->setIdleColor(255, 128, 0);
          } else if (strcmp(color_name, "YELLOW") == 0) {
            _light_ring->setIdleColor(255, 255, 0);
          } else if (strcmp(color_name, "GREEN") == 0) {
            _light_ring->setIdleColor(0, 200, 0);
          } else if (strcmp(color_name, "BLUE") == 0) {
            _light_ring->setIdleColor(0, 120, 255);
          } else if (strcmp(color_name, "VIOLET") == 0) {
            _light_ring->setIdleColor(160, 0, 255);
          }
        }
      }
      if (_light_ring) {
        _light_ring->setOrbiting(true, HELP_ORBIT_INTERVAL_MS);
      }
    }
    return true;
  }

  if (strcmp(type, "CANCEL") == 0) {
    forwardHelpMessage(type, req_id, payload);
    if (lh_id == LIGHTHOUSE_NUMBER && strcmp(_active_request_id, req_id) == 0) {
      _help_state = HelpState::Idle;
      _active_request_id[0] = '\0';
      _help_color_name[0] = '\0';
      if (_light_ring) {
      _light_ring->setIdleColor(LIGHTHOUSE_IDLE_R, LIGHTHOUSE_IDLE_G, LIGHTHOUSE_IDLE_B);
      _light_ring->setOrbiting(false, HELP_ORBIT_INTERVAL_MS);
      }
      if (_light_ring) {
        _light_ring->setPulseColor(255, 64, 64);
        _light_ring->notifyChannelMessage();
      }
      if (_audio_streamer && !_audio_streamer->isPlaying()) {
        if (!_audio_streamer->playFile(SFX_DEQUEUE_PATH) && _light_chime) {
          _light_chime->playMessageChime();
        }
      } else if (_light_chime && (!_audio_streamer || !_audio_streamer->isPlaying())) {
        _light_chime->playMessageChime();
      }
    }
    return true;
  }

  if (strcmp(type, "CLAIM") == 0) {
    if (lh_id == LIGHTHOUSE_NUMBER && strcmp(_active_request_id, req_id) == 0) {
      _help_state = HelpState::Claimed;
      if (_light_ring) {
        _light_ring->setPulseColor(0, 200, 0);
        _light_ring->notifyChannelMessage();
      }
      if (_audio_streamer && !_audio_streamer->isPlaying()) {
        if (!_audio_streamer->playFile(SFX_CLAIM_PATH) && _light_chime) {
          _light_chime->playMessageChime();
        }
      } else if (_light_chime && (!_audio_streamer || !_audio_streamer->isPlaying())) {
        _light_chime->playMessageChime();
      }
    }
    return true;
  }

  if (strcmp(type, "RESOLVE") == 0) {
    if (lh_id == LIGHTHOUSE_NUMBER && strcmp(_active_request_id, req_id) == 0) {
      _help_state = HelpState::Idle;
      _active_request_id[0] = '\0';
      _help_color_name[0] = '\0';
      if (_light_ring) {
      _light_ring->setIdleColor(LIGHTHOUSE_IDLE_R, LIGHTHOUSE_IDLE_G, LIGHTHOUSE_IDLE_B);
      _light_ring->setOrbiting(false, HELP_ORBIT_INTERVAL_MS);
      }
      if (_light_ring) {
        _light_ring->setPulseColor(0, 120, 255);
        _light_ring->notifyChannelMessage();
      }
      if (_audio_streamer && !_audio_streamer->isPlaying()) {
        if (!_audio_streamer->playFile(SFX_RESOLVE_PATH) && _light_chime) {
          _light_chime->playMessageChime();
        }
      } else if (_light_chime && (!_audio_streamer || !_audio_streamer->isPlaying())) {
        _light_chime->playMessageChime();
      }
    }
    return true;
  }

  return true;
}
