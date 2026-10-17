#pragma once

#include <Arduino.h>
#include "MQTT/MqttManager.h"

// Mesure de la batterie LiPo (Heltec V4 uniquement).
// La carte n'expose pas l'état du chargeur : "en charge" est déduit de la tension
// (proche de 4.2 V ou en hausse) ou de la présence d'un hôte USB.
class Battery {
public:
#ifdef WIFI_LORA_32_V4
    static constexpr bool supported = true;
#else
    static constexpr bool supported = false;
#endif

    void begin(MqttManager& mqtt, uint16_t intervalMin);
    void loop();

    bool hasReading() const { return _hasReading; }
    bool present() const { return _milliVolts >= kNoBatteryMv; }
    uint32_t milliVolts() const { return _milliVolts; }
    uint8_t percent() const { return _percent; }
    bool charging() const { return _charging; }
    bool usbHost() const { return _usbHost; }

private:
    static constexpr uint32_t kNoBatteryMv = 2500;
    static constexpr uint32_t kUsbPollMs = 5000;

    MqttManager* _mqtt = nullptr;
    uint32_t _intervalMs = 0;
    uint32_t _lastReadMs = 0;
    uint32_t _lastUsbPollMs = 0;
    bool _hasReading = false;
    bool _pendingPublish = false;

    uint32_t _milliVolts = 0;
    bool _rising = false;
    uint8_t _percent = 0;
    bool _charging = false;
    bool _usbHost = false;

    struct {
        MqttEntity voltage;
        MqttEntity level;
        MqttEntity charging;
        MqttEntity usbHost;
    } _entities;

    void sample();
    bool computeCharging() const;
    void publish();
    static uint32_t readMilliVolts();
    static uint8_t milliVoltsToPercent(uint32_t mv);
    static bool readUsbHost();
};
