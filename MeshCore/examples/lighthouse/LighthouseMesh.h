#pragma once

#include <Arduino.h>
#include <Mesh.h>

#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
#include <InternalFileSystem.h>
#elif defined(RP2040_PLATFORM)
#include <LittleFS.h>
#elif defined(ESP32)
#include <LittleFS.h>
#endif

#include <RTClib.h>
#include <helpers/ArduinoHelpers.h>
#include <helpers/BaseSerialInterface.h>
#include <helpers/IdentityStore.h>
#include <helpers/SimpleMeshTables.h>
#include <helpers/StaticPoolPacketManager.h>
#include <helpers/ArduinoHelpers.h>
#include <target.h>
#include "global_configs.h"

/* ---------------------------------- CONFIGURATION ------------------------------------- */

#ifndef LORA_FREQ
#define LORA_FREQ 910.525
#endif
#ifndef LORA_BW
#define LORA_BW 62.5
#endif
#ifndef LORA_SF
#define LORA_SF 7
#endif
#ifndef LORA_CR
#define LORA_CR 5
#endif
#ifndef LORA_TX_POWER
#define LORA_TX_POWER 22
#endif

#ifndef MAX_CONTACTS
#define MAX_CONTACTS 100
#endif

#ifndef MAX_GROUP_CHANNELS
#define MAX_GROUP_CHANNELS 1
#endif

#ifndef BLE_NAME_PREFIX
#define BLE_NAME_PREFIX "Lighthouse-"
#endif

#include <helpers/BaseChatMesh.h>

/* -------------------------------------------------------------------------------------- */

// Shared PSK for lighthouse network channel (base64 encoded)
// All 30 lighthouses use this same PSK
#define LIGHTHOUSE_CHANNEL_PSK "TEhvdXNlTmV0MjAyNEtleQ=="

class LighthouseMesh : public BaseChatMesh {
public:
  LighthouseMesh(mesh::Radio &radio, mesh::RNG &rng, mesh::RTCClock &rtc, SimpleMeshTables &tables);

  void begin();
  void startInterface(BaseSerialInterface &serial);
  void setLightRing(class LightRing *ring);
  void setLightChime(class LightChime *chime);
  void setAudioStreamer(class AudioStreamer *streamer);
  void setDiscordServer(class DiscordServer *server);
  void setHelpBotClient(class HelpBotClient *client);
  void setHelpBotUrl(const char *url);
  void loop();
  bool sendButtonPressMessage();
  bool requestHelp(const char *color_name);
  bool cancelHelp();
  bool isHelpActive() const;
  bool isHelpClaimed() const;
  bool isAnnouncementActive() const;
  bool isMailboxActive() const;
  bool handleAnnouncementButton();
  bool handleMailboxButton();
  const char *getActiveRequestId() const;
  void sendHelpBroadcast(const char *text);
  void handleHelpPayload(const char *text);
  const char *getNodeName();
  uint32_t getBLEPin();

protected:
  float getAirtimeBudgetFactor() const override;
  int getInterferenceThreshold() const override;
  int calcRxDelay(float score, uint32_t air_time) const override;
  uint8_t getExtraAckTransmitCount() const override;
  bool filterRecvFloodPacket(mesh::Packet* packet) override;

  void sendFloodScoped(const ContactInfo& recipient, mesh::Packet* pkt, uint32_t delay_millis=0) override;
  void sendFloodScoped(const mesh::GroupChannel& channel, mesh::Packet* pkt, uint32_t delay_millis=0) override;

  void logRxRaw(float snr, float rssi, const uint8_t raw[], int len) override;
  bool isAutoAddEnabled() const override;
  bool onContactPathRecv(ContactInfo& from, uint8_t* in_path, uint8_t in_path_len, uint8_t* out_path, uint8_t out_path_len, uint8_t extra_type, uint8_t* extra, uint8_t extra_len) override;
  void onDiscoveredContact(ContactInfo &contact, bool is_new, uint8_t path_len, const uint8_t* path) override;
  void onContactPathUpdated(const ContactInfo &contact) override;
  ContactInfo* processAck(const uint8_t *data) override;

  void onMessageRecv(const ContactInfo &from, mesh::Packet *pkt, uint32_t sender_timestamp,
                     const char *text) override;
  void onCommandDataRecv(const ContactInfo &from, mesh::Packet *pkt, uint32_t sender_timestamp,
                         const char *text) override;
  void onSignedMessageRecv(const ContactInfo &from, mesh::Packet *pkt, uint32_t sender_timestamp,
                           const uint8_t *sender_prefix, const char *text) override;
  void onChannelMessageRecv(const mesh::GroupChannel &channel, mesh::Packet *pkt, uint32_t timestamp,
                            const char *text) override;

  uint8_t onContactRequest(const ContactInfo &contact, uint32_t sender_timestamp, const uint8_t *data,
                           uint8_t len, uint8_t *reply) override;
  void onContactResponse(const ContactInfo &contact, const uint8_t *data, uint8_t len) override;

  uint32_t calcFloodTimeoutMillisFor(uint32_t pkt_airtime_millis) const override;
  uint32_t calcDirectTimeoutMillisFor(uint32_t pkt_airtime_millis, uint8_t path_len) const override;
  void onSendTimeout() override;

private:
  BaseSerialInterface *_serial;
  ChannelDetails* _lighthouse_channel;
  class LightRing *_light_ring;
  class LightChime *_light_chime;
  class AudioStreamer *_audio_streamer;
  class DiscordServer *_discord_server;
  class HelpBotClient *_help_bot_client;
  char _node_name[32];
  uint32_t _active_ble_pin;
  unsigned long _last_button_send;
#ifdef BUTTON_SEND_COOLDOWN_MS
  static const unsigned long BUTTON_SEND_COOLDOWN_MS_VALUE = BUTTON_SEND_COOLDOWN_MS;
#else
  static const unsigned long BUTTON_SEND_COOLDOWN_MS_VALUE = 2000; // 2 second cooldown
#endif

  enum class HelpState {
    Idle,
    Pending,
    Claimed
  };

  HelpState _help_state;
  char _active_request_id[32];
  char _help_color_name[8];
  uint16_t _request_seq;
  bool _announcement_active;
  bool _announcement_acknowledged;
  bool _announcement_can_stop;
  bool _announcement_audio_playing;
  bool _announcement_eom_pending;
  bool _announcement_eom_playing;
  unsigned long _announcement_eom_at_ms;
  unsigned long _announcement_next_play_ms;
  char _announcement_url[192];

  void updateAnnouncement();
  void startAnnouncement(const char *url);
  void stopAnnouncement();
  void restoreIdleColor();

  char _mailbox_queue[MAILBOX_QUEUE_SIZE][192];
  uint8_t _mailbox_count;
  bool _mailbox_active;
  bool _mailbox_open;
  bool _mailbox_alerting;
  bool _mailbox_can_advance;
  bool _mailbox_audio_playing;
  bool _mailbox_eom_pending;
  bool _mailbox_eom_playing;
  unsigned long _mailbox_eom_at_ms;
  unsigned long _mailbox_next_play_ms;
  char _mailbox_current_url[192];

  void updateMailbox();
  void enqueueMailbox(const char *url);
  bool dequeueMailbox(char *out, size_t out_len);
  void startMailboxAlert();
  void stopMailbox();

  static const uint8_t kAckCacheSize = 64;
  char _ack_cache[kAckCacheSize][40];
  uint8_t _ack_cache_head;

  bool isAcked(const char *key) const;
  void rememberAck(const char *key);
  void broadcastAck(const char *type, const char *req_id);
  bool handleHelpMessage(const char *text);
  bool forwardHelpMessage(const char *type, const char *req_id, const char *text);
};
