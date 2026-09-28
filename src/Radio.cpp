#include "Radio.h"

#if defined(WIFI_LORA_32_V4) && defined(USE_KCT8103L_PA)
namespace {
constexpr uint8_t KCT8103L_POWER_PIN = GPIO_NUM_7;
constexpr uint8_t KCT8103L_ENABLE_PIN = GPIO_NUM_2;
constexpr uint8_t KCT8103L_TX_ENABLE_PIN = GPIO_NUM_5;
}
#endif

Radio::Radio() : _radio(new Module(SS, DIO0, RST_LoRa, BUSY_LoRa)) {}

void Radio::init() {
#if defined(WIFI_LORA_32_V4) && defined(USE_KCT8103L_PA)
    // Heltec WiFi LoRa 32 V4.3: power and enable the KCT8103L FEM.
    pinMode(KCT8103L_POWER_PIN, OUTPUT);
    digitalWrite(KCT8103L_POWER_PIN, HIGH);
    pinMode(KCT8103L_ENABLE_PIN, OUTPUT);
    digitalWrite(KCT8103L_ENABLE_PIN, HIGH);

    // PA_CTX must only be high while transmitting. RadioLib also configures
    // the SX1262 DIO2 pin as the KCT8103L RF-path switch during beginFSK().
    _radio.setRfSwitchPins(RADIOLIB_NC, KCT8103L_TX_ENABLE_PIN);
#endif

    _radio.beginFSK();
    _radio.setFrequency(868.96);
    _radio.setBitRate(25.0);
    _radio.setFrequencyDeviation(50.0);
    _radio.setRxBandwidth(250.0);
    _radio.setPreambleLength(4);
}
