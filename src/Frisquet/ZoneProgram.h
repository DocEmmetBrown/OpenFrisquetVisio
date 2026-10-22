#pragma once
#include <stdint.h>

// Programme local : dimanche en premier, 48 demi-heures par jour, bits LSB-first.
namespace ZoneProgram {
inline bool comfort(const uint8_t* program, unsigned slot) {
    slot %= 336;
    return (program[slot / 8] & (1U << (slot % 8))) != 0;
}

// Première transition réelle de régime, et non première limite de demi-heure.
// Zéro indique un programme constant sur toute la semaine.
inline uint32_t nextTransition(const uint8_t* program, unsigned slot, unsigned secondsInSlot) {
    const bool current = comfort(program, slot);
    for (unsigned distance = 1; distance <= 336; ++distance) {
        if (comfort(program, slot + distance) != current) {
            return distance * 1800UL - secondsInSlot;
        }
    }
    return 0;
}
}
