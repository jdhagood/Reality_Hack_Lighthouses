#include "GpioButtonMessageModule.h"

#include "MeshService.h"
#include "Router.h"
#include "Throttle.h"
#include "concurrency/OSThread.h"
#include "main.h"
#include "mesh/generated/meshtastic/mesh.pb.h"
#include "mesh/generated/meshtastic/portnums.pb.h"

namespace
{
constexpr uint8_t kButtonPin = 4;
constexpr uint32_t kDebounceMs = 50;
constexpr char kMessage[] = "button pressed on ESP32 1";

volatile bool g_pending = false;
uint32_t g_lastSendMs = 0;

IRAM_ATTR void onButtonFalling()
{
    g_pending = true;
    runASAP = true;
    BaseType_t higherWake = 0;
    concurrency::mainDelay.interruptFromISR(&higherWake);
}
} // namespace

GpioButtonMessageModule::GpioButtonMessageModule() : OSThread("GpioButtonMsg")
{
#ifdef ARCH_ESP32
    pinMode(kButtonPin, INPUT_PULLUP);
    attachInterrupt(kButtonPin, onButtonFalling, FALLING);
#endif
}

int32_t GpioButtonMessageModule::runOnce()
{
#ifdef ARCH_ESP32
    if (g_pending) {
        g_pending = false;

        if (!Throttle::isWithinTimespanMs(g_lastSendMs, kDebounceMs) && digitalRead(kButtonPin) == LOW) {
            g_lastSendMs = millis();

            meshtastic_MeshPacket *p = router->allocForSending();
            size_t len = strnlen(kMessage, meshtastic_Constants_DATA_PAYLOAD_LEN);

            p->decoded.portnum = meshtastic_PortNum_TEXT_MESSAGE_APP;
            p->to = NODENUM_BROADCAST;
            p->channel = 0;
            p->want_ack = false;
            p->decoded.want_response = false;
            p->decoded.payload.size = len;
            memcpy(p->decoded.payload.bytes, kMessage, len);

            service->sendToMesh(p, RX_SRC_LOCAL, true);
        }
    }
#endif
    return 1000;
}
