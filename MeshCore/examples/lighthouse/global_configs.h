#pragma once

// Global lighthouse configuration values.

#ifndef LIGHTHOUSE_IDLE_R
#define LIGHTHOUSE_IDLE_R 15
#endif
#ifndef LIGHTHOUSE_IDLE_G
#define LIGHTHOUSE_IDLE_G 15
#endif
#ifndef LIGHTHOUSE_IDLE_B
#define LIGHTHOUSE_IDLE_B 15
#endif

#ifndef AUDIO_VOLUME
#define AUDIO_VOLUME 1.0f
#endif

#ifndef AUDIO_SMOOTHING_ALPHA
#define AUDIO_SMOOTHING_ALPHA 0.75f
#endif

#ifndef AUDIO_BUFFER_BYTES
#define AUDIO_BUFFER_BYTES 8192
#endif

#ifndef AUDIO_PREROLL_MS
#define AUDIO_PREROLL_MS 200
#endif



// SFX //
#ifndef SFX_BUTTON_PATH
#define SFX_BUTTON_PATH "/sfx/button.wav"
#endif

#ifndef SFX_CLAIM_PATH
#define SFX_CLAIM_PATH "/sfx/claim.wav"
#endif

#ifndef SFX_RESOLVE_PATH
#define SFX_RESOLVE_PATH "/sfx/resolve.wav"
#endif

#ifndef SFX_DEQUEUE_PATH
#define SFX_DEQUEUE_PATH "/sfx/dequeue.wav"
#endif

#ifndef YOU_HAVE_REQUESTED_HELP_PATH
#define YOU_HAVE_REQUESTED_HELP_PATH "/sfx/you_have_requested_help.wav"
#endif

#ifndef MENTOUR_ON_THEIR_WAY_PATH
#define MENTOUR_ON_THEIR_WAY_PATH "/sfx/mentour_on_their_way.wav"
#endif

#ifndef MAIL_ALERT_PATH
#define MAIL_ALERT_PATH "/sfx/mail_alert.wav"
#endif

#ifndef EOM_PATH
#define EOM_PATH "/sfx/eom.wav"
#endif

#ifndef MAILBOX_QUEUE_SIZE
#define MAILBOX_QUEUE_SIZE 8
#endif

#ifndef MAIL_ALERT_INTERVAL_MS
#define MAIL_ALERT_INTERVAL_MS 10000
#endif

#ifndef HELP_ORBIT_INTERVAL_MS
#define HELP_ORBIT_INTERVAL_MS 120
#endif

#ifndef FIRMWARE_VERSION
#define FIRMWARE_VERSION "lighthouse-1.0.0"
#endif

#ifndef REGISTRATION_SERVER_IP
#define REGISTRATION_SERVER_IP "192.168.50.102"
#endif

#ifndef REGISTRATION_SERVER_PORT
#define REGISTRATION_SERVER_PORT 9010
#endif

#ifndef REGISTRATION_HEARTBEAT_MS
#define REGISTRATION_HEARTBEAT_MS 5000
#endif

#ifndef REGISTRATION_LOCAL_PORT
#define REGISTRATION_LOCAL_PORT 9011
#endif
