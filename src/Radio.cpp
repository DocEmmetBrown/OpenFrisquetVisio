#include "Radio.h"
#include "Logs.h"

Radio::Radio() : _radio(new Module(SS, DIO0, RST_LoRa, BUSY_LoRa)) {}

#ifdef WIFI_LORA_32_V4
// Heltec V4: the SX1262 reaches the antenna through an RF front-end (FEM) that must be powered.
// V4.2 uses a GC1109, V4.3 a KCT8103L; both share the power/enable pins.
// See https://github.com/meshtastic/firmware/blob/master/variants/esp32s3/heltec_v4/variant.h
static const uint8_t FEM_POWER = 7;    // FEM LDO enable
static const uint8_t FEM_CSD = 2;      // chip enable (HIGH = on)
static const uint8_t GC1109_CPS = 46;  // GC1109: TX PA mode (HIGH = full PA, LOW = bypass)
static const uint8_t KCT8103L_CTX = 5; // KCT8103L: LOW = RX LNA, HIGH = TX PA

// Keep TX output close to the V3 (SX1262 default 10 dBm)
static const int8_t KCT8103L_TX_POWER = 10 - 13; // KCT8103L PA adds ~13 dB, it can't be bypassed

static bool initFrontEnd() {
    pinMode(FEM_POWER, OUTPUT);
    digitalWrite(FEM_POWER, HIGH);
    delay(5);

    // The KCT8103L pulls CSD high, the GC1109 doesn't
    pinMode(FEM_CSD, INPUT);
    delay(1);
    bool isKCT8103L = digitalRead(FEM_CSD) == HIGH;

    pinMode(FEM_CSD, OUTPUT);
    digitalWrite(FEM_CSD, HIGH);

    if (!isKCT8103L) {
        // GC1109: RX/TX path is switched by SX1262 DIO2, keep the PA bypassed on TX
        pinMode(GC1109_CPS, OUTPUT);
        digitalWrite(GC1109_CPS, LOW);
    }
    return isKCT8103L;
}
#endif

void Radio::init() {
#ifdef WIFI_LORA_32_V4
    bool isKCT8103L = initFrontEnd();
#endif
    _radio.beginFSK();
    _radio.setFrequency(868.96);
    _radio.setBitRate(25.0);
    _radio.setFrequencyDeviation(50.0);
    _radio.setRxBandwidth(250.0);
    _radio.setPreambleLength(4);
#ifdef WIFI_LORA_32_V4
    if (isKCT8103L) {
        _radio.setRfSwitchPins(RADIOLIB_NC, KCT8103L_CTX);
        _radio.setOutputPower(KCT8103L_TX_POWER);
    }
    info("[RADIO] Heltec V4, FEM %s", isKCT8103L ? "KCT8103L" : "GC1109");
#endif
}
