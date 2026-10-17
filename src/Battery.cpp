#include "Battery.h"
#include "Logs.h"

#if ARDUINO_USB_CDC_ON_BOOT && ARDUINO_USB_MODE
#include <HWCDC.h>
#endif

// Heltec V4 : VBAT via pont diviseur sur GPIO1, activé par GPIO37 (HIGH).
// Valeurs reprises de Meshtastic (variants/esp32s3/heltec_v4/variant.h).
static const uint8_t BAT_ADC_PIN = 1;
static const uint8_t BAT_ADC_CTRL_PIN = 37;
static const float BAT_DIVIDER = 4.9f * 1.045f;

// Courbe tension à vide LiPo (mV) de 100 % à 0 %, par pas de 10 %
static const uint16_t OCV[] = {4190, 4050, 3990, 3890, 3800, 3720, 3630, 3530, 3420, 3300, 3100};
static const size_t OCV_POINTS = sizeof(OCV) / sizeof(OCV[0]);

static const uint32_t CHARGING_MV = 4150;     // au-delà : chargeur actif ou batterie pleine sur secteur
static const uint32_t RISING_DELTA_MV = 15;   // hausse entre deux mesures => en charge

void Battery::begin(MqttManager& mqtt, uint16_t intervalMin) {
    if (!supported) return;

    _mqtt = &mqtt;
    _intervalMs = (uint32_t)(intervalMin ? intervalMin : 1) * 60000UL;

    MqttDevice* device = mqtt.getDevice("openFrisquetVisio");

    _entities.voltage.id = "batteryVoltage";
    _entities.voltage.name = "Tension batterie";
    _entities.voltage.component = "sensor";
    _entities.voltage.stateTopic = MqttTopic(MqttManager::compose({device->baseTopic, "batterie", "tension"}), 0, true);
    _entities.voltage.set("device_class", "voltage");
    _entities.voltage.set("state_class", "measurement");
    _entities.voltage.set("unit_of_measurement", "V");
    _entities.voltage.set("suggested_display_precision", 2);
    _entities.voltage.set("entity_category", "diagnostic");
    mqtt.registerEntity(*device, _entities.voltage, true);

    _entities.level.id = "batteryLevel";
    _entities.level.name = "Niveau batterie";
    _entities.level.component = "sensor";
    _entities.level.stateTopic = MqttTopic(MqttManager::compose({device->baseTopic, "batterie", "niveau"}), 0, true);
    _entities.level.set("device_class", "battery");
    _entities.level.set("state_class", "measurement");
    _entities.level.set("unit_of_measurement", "%");
    _entities.level.set("entity_category", "diagnostic");
    mqtt.registerEntity(*device, _entities.level, true);

    _entities.charging.id = "batteryCharging";
    _entities.charging.name = "Batterie en charge";
    _entities.charging.component = "binary_sensor";
    _entities.charging.stateTopic = MqttTopic(MqttManager::compose({device->baseTopic, "batterie", "enCharge"}), 0, true);
    _entities.charging.set("device_class", "battery_charging");
    _entities.charging.set("entity_category", "diagnostic");
    mqtt.registerEntity(*device, _entities.charging, true);

    _entities.usbHost.id = "usbHost";
    _entities.usbHost.name = "USB connecté à un ordinateur";
    _entities.usbHost.component = "binary_sensor";
    _entities.usbHost.stateTopic = MqttTopic(MqttManager::compose({device->baseTopic, "batterie", "usbHote"}), 0, true);
    _entities.usbHost.set("device_class", "plug");
    _entities.usbHost.set("entity_category", "diagnostic");
    mqtt.registerEntity(*device, _entities.usbHost, true);

    sample();
}

void Battery::loop() {
    if (!supported || !_mqtt) return;

    uint32_t now = millis();

    if (now - _lastUsbPollMs >= kUsbPollMs) {
        _lastUsbPollMs = now;
        bool usb = readUsbHost();
        if (usb != _usbHost) {
            _usbHost = usb;
            _charging = computeCharging();
            _pendingPublish = true;
            info("[BATTERIE] USB %s", usb ? "connecté à un ordinateur" : "déconnecté");
        }
    }

    if (now - _lastReadMs >= _intervalMs) {
        sample();
    }

    if (_pendingPublish && _mqtt->connected()) {
        publish();
    }
}

void Battery::sample() {
    _lastReadMs = millis();
    _usbHost = readUsbHost();

    uint32_t mv = readMilliVolts();
    _rising = _hasReading && mv >= _milliVolts + RISING_DELTA_MV;

    _milliVolts = mv;
    _percent = milliVoltsToPercent(mv);
    _charging = computeCharging();
    _hasReading = true;
    _pendingPublish = true;

    if (present()) {
        info("[BATTERIE] %.2f V, %u %%%s", mv / 1000.0f, _percent, _charging ? ", en charge" : "");
    } else {
        info("[BATTERIE] Absente (%.2f V)", mv / 1000.0f);
    }
}

bool Battery::computeCharging() const {
    return present() && (_milliVolts >= CHARGING_MV || _rising || _usbHost);
}

void Battery::publish() {
    _pendingPublish = false;
    _mqtt->publishState(_entities.usbHost, _usbHost ? "ON" : "OFF");
    _mqtt->publishState(_entities.charging, _charging ? "ON" : "OFF");
    if (present()) {
        _mqtt->publishState(_entities.voltage, _milliVolts / 1000.0f, 2);
        _mqtt->publishState(_entities.level, String(_percent));
    } else {
        _mqtt->publishState(_entities.voltage, "None");
        _mqtt->publishState(_entities.level, "None");
    }
}

uint32_t Battery::readMilliVolts() {
#ifdef WIFI_LORA_32_V4
    pinMode(BAT_ADC_CTRL_PIN, OUTPUT);
    digitalWrite(BAT_ADC_CTRL_PIN, HIGH);
    delay(10);

    analogSetPinAttenuation(BAT_ADC_PIN, ADC_2_5db);
    uint32_t sum = 0;
    const int samples = 16;
    for (int i = 0; i < samples; i++) {
        sum += analogReadMilliVolts(BAT_ADC_PIN);
    }

    digitalWrite(BAT_ADC_CTRL_PIN, LOW);
    return (uint32_t)((sum / (float)samples) * BAT_DIVIDER);
#else
    return 0;
#endif
}

uint8_t Battery::milliVoltsToPercent(uint32_t mv) {
    if (mv >= OCV[0]) return 100;
    if (mv <= OCV[OCV_POINTS - 1]) return 0;
    for (size_t i = 1; i < OCV_POINTS; i++) {
        if (mv >= OCV[i]) {
            float frac = (float)(mv - OCV[i]) / (OCV[i - 1] - OCV[i]);
            return (uint8_t)roundf(100.0f - 10.0f * (i - frac));
        }
    }
    return 0;
}

bool Battery::readUsbHost() {
#if ARDUINO_USB_CDC_ON_BOOT && ARDUINO_USB_MODE
    // Détecte un hôte USB (trames SOF) : un chargeur secteur n'est pas vu.
    return HWCDC::isPlugged();
#else
    return false;
#endif
}
