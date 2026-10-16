#pragma once
#include <TimeLib.h>
#include <Arduino.h>

// Créneaux basés sur l'heure chaudière, ou sur le démarrage avant synchronisation.
// Une tentative par créneau, même en cas d'échec ; pas de rattrapage en rafale.
class RadioSchedule {
public:
    static uint32_t clockSeconds() {
        return timeStatus() == timeNotSet ? millis() / 1000UL : ::now();
    }

    static bool quietWindow(uint32_t seconds) {
        const uint32_t phase = seconds % 600;
        // Satellites Z1 à :00, Z2 à :01 et Z3 à :02, avec ±30 secondes.
        return phase < 150 || phase >= 570;
    }

    bool due(uint32_t period, uint32_t offset) {
        return dueAt(clockSeconds(), millis(), period, offset);
    }

    bool dueAt(uint32_t seconds, uint32_t uptime, uint32_t period, uint32_t offset) {
        if (quietWindow(seconds) || period == 0 || offset >= period) return false;
        const uint32_t phase = seconds % period;
        if (phase < offset || phase - offset >= 10) return false;
        // Empêcher une deuxième tentative dans le créneau et les doublons lors
        // d'une correction de l'heure. La soustraction résiste au wrap de millis().
        if (_attempted && uptime - _lastAttempt < (period - 10) * 1000UL) return false;
        _attempted = true;
        _lastAttempt = uptime;
        return true;
    }

private:
    bool _attempted = false;
    uint32_t _lastAttempt = 0;
};
