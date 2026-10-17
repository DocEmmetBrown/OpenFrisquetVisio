#include "App.h"

App::App() : _mqtt(_wifiClient), _frisquetManager(_radio, _cfg, _mqtt) {}

void App::begin() {
  Serial.begin(115200);
  delay(50);

  Heltec.begin(false /*DisplayEnable disable*/, false /*LoRa Disable*/, true /*Serial Enable*/);
#ifdef WIFI_LORA_32_V4
  // Écran non utilisé : coupe son alimentation (Vext, active à l'état bas)
  pinMode(Vext, OUTPUT);
  digitalWrite(Vext, HIGH);
#endif

  initConfig();
  initNetwork();
  initMqtt();
  initPortal();
  initOta();

  _frisquetManager.begin();
  _battery.begin(_mqtt, _cfg.batteryIntervalMin());
}

void App::loop() {
  // boucle des services
  _networkManager.loop();
  if (_apFallbackArmed && !WiFi.isConnected()) {
    if ((int32_t)(millis() - _apFallbackAtMs) >= 0) {
      _apFallbackArmed = false;
      WiFi.mode(WIFI_AP_STA);
      info("[WIFI] AP fallback activé après délai.");
    }
  }
  _ota.loop();
  _portal->loop();
  _mqtt.loop();
  _frisquetManager.loop();
  _battery.loop();
  syncClock();
  delay(10);
}

void App::initConfig() {
  _cfg.load();
}

void App::initNetwork() {
    _networkManager.onConnected([&](){
        info("[WIFI] CONNECTED  IP=%s  RSSI=%ddBm\n", _networkManager.ipStr().c_str(), _networkManager.rssi());
        _apFallbackArmed = false;
        WiFi.softAPdisconnect(true);
        WiFi.mode(WIFI_STA);
    });
    _networkManager.onDisconnected([&](const String& reason){
        info("[WIFI] DISCONNECTED (%s)\n", reason.c_str());
        _apFallbackArmed = true;
        _apFallbackAtMs = millis() + 60000;
    });

    _networkManager.begin(_cfg.getWiFiOptions());

    // Heure locale France (CET/CEST). Après WiFi.mode() : la pile réseau doit être initialisée.
    // L'heure reçue de la chaudière (radio) met aussi à jour l'horloge.
    configTzTime("CET-1CEST,M3.5.0,M10.5.0/3", "pool.ntp.org", "time.google.com");
}

void App::syncClock() {
  const uint32_t interval = _clockSynced ? 3600000UL : 5000UL;
  if (_lastClockSyncMs && millis() - _lastClockSyncMs < interval) return;
  _lastClockSyncMs = millis();

  time_t t = time(nullptr);
  if (t < 1700000000) return; // NTP pas encore synchronisé

  struct tm lt;
  localtime_r(&t, &lt);
  setTime(lt.tm_hour, lt.tm_min, lt.tm_sec, lt.tm_mday, lt.tm_mon + 1, lt.tm_year + 1900);

  if (!_clockSynced) {
    _clockSynced = true;
    info("[HEURE] Synchronisée via NTP : %02d/%02d/%04d %02d:%02d:%02d",
         lt.tm_mday, lt.tm_mon + 1, lt.tm_year + 1900, lt.tm_hour, lt.tm_min, lt.tm_sec);
  }
}

void App::initMqtt() {
  _mqtt.begin(_cfg.getMQTTOptions());
}

void App::initPortal() {
  _portal = new Portal(_frisquetManager);
  _portal->setBattery(&_battery);
  _portal->begin(/*startApFallbackIfNoWifi=*/true);

  info("[PORTAIL] Portail initialisé.");
}

void App::initOta() {
  _ota.begin(_networkManager.hostname().c_str());
}
