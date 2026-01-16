// NanoVHF T-Energy-S3 + E22(0)-xxxM - DIY
// https://github.com/NanoVHF/Meshtastic-DIY/tree/main/PCB/ESP-32-devkit_EBYTE-E22/Mesh-v1.06-TTGO-T18


// Button on NanoVHF PCB
#define BUTTON_PIN 2

// I2C via connectors on NanoVHF PCB
#define I2C_SCL 5
#define I2C_SDA 4

// Screen (disabled)
#define HAS_SCREEN 0 // Assume no screen present by default to prevent crash...

#define HAS_GPS 0

// LoRa
#define USE_SX1262 // E22-900M30S, E22-900M22S, and E22-900MM22S (not E220!) use SX1262
#define USE_SX1268 // E22-400M30S, E22-400M33S, E22-400M22S, and E22-400MM22S use SX1268


#define SX126X_CS 10    // EBYTE module's NSS pin // FIXME: rename to SX126X_SS
#define SX126X_SCK 12   // EBYTE module's SCK pin
#define SX126X_MOSI 11 // EBYTE module's MOSI pin
#define SX126X_MISO 13  // EBYTE module's MISO pin
#define SX126X_RESET -1 // EBYTE module's NRST pin
#define SX126X_BUSY 14 // EBYTE module's BUSY pin
#define SX126X_DIO1 9 // EBYTE module's DIO1 pin


#define SX126X_TXEN 10 // Schematic connects EBYTE module's TXEN pin to MCU
#define SX126X_RXEN 12 // Schematic connects EBYTE module's RXEN pin to MCU

#define LORA_CS SX126X_CS     // Compatibility with variant file configuration structure
#define LORA_SCK SX126X_SCK   // Compatibility with variant file configuration structure
#define LORA_MOSI SX126X_MOSI // Compatibility with variant file configuration structure
#define LORA_MISO SX126X_MISO // Compatibility with variant file configuration structure
#define LORA_DIO1 SX126X_DIO1 // Compatibility with variant file configuration structure
#define LORA_EN 15


// Prevent Meshtastic from using external RF switch pins
#ifdef SX126X_RXEN
#undef SX126X_RXEN
#endif
#ifdef SX126X_TXEN
#undef SX126X_TXEN
#endif

#define SX126X_RXEN (-1)
#define SX126X_TXEN (-1)
