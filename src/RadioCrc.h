#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

// Capture après le sync word : longueur, données, CRC MSB/LSB, puis marge.
// Le CRC couvre aussi l'octet de longueur (format GFSK variable SX1262).
namespace RadioCrc {
constexpr size_t MAX_PAYLOAD = 63;
constexpr size_t RAW_LENGTH = 68;
constexpr size_t HEADER_LENGTH = 6;
constexpr size_t MIN_ZERO_RUN = 16;

enum class Status { Valid, Repaired, Invalid, Ambiguous, InvalidLength };
inline const char* statusName(Status status) {
    switch (status) {
        case Status::Valid: return "VALID";
        case Status::Repaired: return "REPAIRED";
        case Status::Ambiguous: return "AMBIGUOUS";
        case Status::InvalidLength: return "INVALID_LENGTH";
        default: return "INVALID";
    }
}

struct Result {
    Status status = Status::Invalid;
    size_t length = 0;
    size_t bit = 0; // Position dans la capture brute, MSB en premier.
    int8_t adjustment = 0; // +1 : insérer un zéro, -1 : supprimer un zéro.
    uint8_t correctedBits = 0; // Toujours 0 (aucune correction) ou 1.
};

inline uint16_t crc16(const uint8_t* data, size_t length) {
    uint16_t crc = 0x1D0F;
    for (size_t i = 0; i < length; ++i) {
        crc ^= static_cast<uint16_t>(data[i]) << 8;
        for (uint8_t bit = 0; bit < 8; ++bit) {
            crc = static_cast<uint16_t>((crc << 1) ^ ((crc & 0x8000) ? 0x1021 : 0));
        }
    }
    return crc ^ 0xFFFF;
}

inline bool getBit(const uint8_t* data, size_t bit) {
    return (data[bit / 8] >> (7 - bit % 8)) & 1;
}

inline bool valid(const uint8_t* raw, size_t length) {
    const size_t crcOffset = length + 1;
    const uint16_t received = (static_cast<uint16_t>(raw[crcOffset]) << 8) | raw[crcOffset + 1];
    return crc16(raw, crcOffset) == received;
}

// Ne modifie jamais la capture d'origine, ni la sortie en cas de rejet.
// Une seule insertion OU suppression de zéro, dans une seule suite après
// l'en-tête. Aucune recherche de corrections à plusieurs bits.
// Les reconstructions identiques comptent une seule fois ; on examine toutes
// les candidates avant d'accepter pour détecter une éventuelle ambiguïté.
inline Result decode(const uint8_t* raw, size_t rawLength, uint8_t* payload) {
    Result result;
    if (!raw || !payload || rawLength == 0 || rawLength > RAW_LENGTH) return result;
    result.length = raw[0];
    if (result.length < HEADER_LENGTH || result.length > MAX_PAYLOAD) {
        result.status = Status::InvalidLength;
        result.length = 0;
        return result;
    }
    const size_t frameLength = result.length + 3;
    if (rawLength < frameLength) return result;
    if (valid(raw, result.length)) {
        memcpy(payload, raw + 1, result.length);
        result.status = Status::Valid;
        return result;
    }
    // Un bit supplémentaire est nécessaire pour annuler un zéro en trop.
    if (rawLength * 8 <= frameLength * 8) return result;

    uint8_t unique[MAX_PAYLOAD + 3];
    uint8_t candidate[MAX_PAYLOAD + 3];
    bool found = false;
    const size_t payloadEnd = (result.length + 1) * 8;
    for (size_t bit = (HEADER_LENGTH + 1) * 8; bit < payloadEnd;) {
        if (getBit(raw, bit)) { ++bit; continue; }
        const size_t start = bit;
        while (bit < payloadEnd && !getBit(raw, bit)) ++bit;
        if (bit - start < MIN_ZERO_RUN) continue;
        // Ne pas démarrer au milieu d'une suite commencée dans l'en-tête.
        if (!getBit(raw, start - 1)) continue;
        for (int8_t adjustment = 1; adjustment >= -1; adjustment -= 2) {
            memset(candidate, 0, frameLength);
            for (size_t out = 0; out < frameLength * 8; ++out) {
                bool value;
                if (out < start) value = getBit(raw, out);
                else if (adjustment == 1) value = out == start ? false : getBit(raw, out - 1);
                else value = getBit(raw, out + 1);
                if (value) candidate[out / 8] |= uint8_t(1U << (7 - out % 8));
            }
            if (!valid(candidate, result.length)) continue;
            if (found && memcmp(unique, candidate, frameLength) != 0) {
                result.status = Status::Ambiguous;
                return result;
            }
            if (!found) {
                memcpy(unique, candidate, frameLength);
                result.bit = start;
                result.adjustment = adjustment;
                result.correctedBits = 1;
                found = true;
            }
        }
    }
    if (found) {
        memcpy(payload, unique + 1, result.length);
        result.status = Status::Repaired;
    }
    return result;
}
}
