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
constexpr size_t MAX_CORRECTED_BITS = 3;
constexpr size_t MAX_RUNS = (MAX_PAYLOAD - HEADER_LENGTH) * 8 / (MIN_ZERO_RUN + 1) + 1;
// Borner le travail sur une trame très fragmentée ; ne jamais accepter une
// solution si la recherche a été interrompue avant de vérifier son unicité.
constexpr size_t MAX_EXTENDED_CANDIDATES = 4096;

enum class Status { Valid, Repaired, Invalid, Ambiguous, InvalidLength, SearchLimit };
inline const char* statusName(Status status) {
    switch (status) {
        case Status::Valid: return "VALID";
        case Status::Repaired: return "REPAIRED";
        case Status::Ambiguous: return "AMBIGUOUS";
        case Status::InvalidLength: return "INVALID_LENGTH";
        case Status::SearchLimit: return "SEARCH_LIMIT";
        default: return "INVALID";
    }
}

struct Result {
    Status status = Status::Invalid;
    size_t length = 0;
    size_t bit = 0; // Position dans la capture brute, MSB en premier.
    int8_t adjustment = 0; // Somme signée des insertions (+) / suppressions (-).
    uint8_t correctedBits = 0;
    uint16_t bits[MAX_CORRECTED_BITS] = {};
    int8_t zeroAdjustments[MAX_CORRECTED_BITS] = {};
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

namespace Detail {
struct Search {
    const uint8_t* raw;
    size_t rawLength;
    size_t frameLength;
    Result result;
    bool found = false;
    bool ambiguous = false;
    bool exhausted = false;
    size_t tested = 0;
    uint8_t unique[MAX_PAYLOAD + 3];

    Search(const uint8_t* data, size_t size, size_t length)
        : raw(data), rawLength(size), frameLength(length + 3) { result.length = length; }

    void consider(const uint16_t* positions, const int8_t* adjustments, uint8_t count) {
        if (count > 1 && tested++ >= MAX_EXTENDED_CANDIDATES) {
            exhausted = true;
            return;
        }
        uint8_t candidate[MAX_PAYLOAD + 3] = {};
        const size_t frameBits = frameLength * 8;
        size_t source = 0, out = 0;
        // Les positions se réfèrent à la capture d'origine. Regrouper les
        // changements d'une même suite pour éviter de décaler leurs indices.
        for (uint8_t i = 0; i < count;) {
            const size_t position = positions[i];
            int8_t adjustment = 0;
            do { adjustment += adjustments[i++]; }
            while (i < count && positions[i] == position);
            while (source < position && out < frameBits) {
                if (getBit(raw, source)) candidate[out / 8] |= uint8_t(1U << (7 - out % 8));
                ++source;
                ++out;
            }
            if (adjustment > 0) out += adjustment; // Tampon déjà rempli de zéros.
            else source += -adjustment;
        }
        while (out < frameBits) {
            if (source >= rawLength * 8) return;
            if (getBit(raw, source)) candidate[out / 8] |= uint8_t(1U << (7 - out % 8));
            ++source;
            ++out;
        }
        if (!valid(candidate, result.length)) return;
        if (found && memcmp(unique, candidate, frameLength) != 0) {
            ambiguous = true;
            return;
        }
        if (!found) {
            memcpy(unique, candidate, frameLength);
            result.bit = positions[0];
            result.correctedBits = count;
            for (uint8_t i = 0; i < count; ++i) {
                result.bits[i] = positions[i];
                result.zeroAdjustments[i] = adjustments[i];
                result.adjustment += adjustments[i];
            }
            found = true;
        }
    }
};

// Combinaisons ordonnées avec répétition : jusqu'à trois zéros, dans une ou
// plusieurs suites. Pas d'insertion et suppression qui s'annulent dans la même
// suite. La profondeur et le nombre de candidats sont bornés.
inline void combinations(Search& search, const uint16_t* runs, size_t runCount,
                         uint8_t count, uint8_t depth, size_t first,
                         uint16_t* positions, int8_t* adjustments) {
    if (depth == count) {
        search.consider(positions, adjustments, count);
        return;
    }
    for (size_t run = first; run < runCount; ++run) {
        positions[depth] = runs[run];
        for (int8_t adjustment = 1; adjustment >= -1; adjustment -= 2) {
            if (depth && positions[depth - 1] == runs[run]
                && adjustments[depth - 1] != adjustment) continue;
            adjustments[depth] = adjustment;
            combinations(search, runs, runCount, count, depth + 1, run, positions, adjustments);
            if (search.ambiguous || search.exhausted) return;
        }
    }
}
}

// Aucune modification de la capture, de la longueur ou de l'en-tête. La sortie
// reste intacte en cas de rejet. Priorité à la réparation simple ; uniquement
// si elle n'a aucune solution, rechercher deux ET trois changements, dans les
// deux sens, avant de conclure à l'unicité de la seconde passe.
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
    if (rawLength * 8 <= frameLength * 8) return result;

    uint16_t runs[MAX_RUNS];
    size_t runCount = 0;
    const size_t payloadEnd = (result.length + 1) * 8;
    for (size_t bit = (HEADER_LENGTH + 1) * 8; bit < payloadEnd;) {
        if (getBit(raw, bit)) { ++bit; continue; }
        const size_t start = bit;
        while (bit < payloadEnd && !getBit(raw, bit)) ++bit;
        if (bit - start < MIN_ZERO_RUN || !getBit(raw, start - 1)) continue;
        runs[runCount++] = static_cast<uint16_t>(start);
    }

    Detail::Search search(raw, rawLength, result.length);
    uint16_t positions[MAX_CORRECTED_BITS] = {};
    int8_t adjustments[MAX_CORRECTED_BITS] = {};
    Detail::combinations(search, runs, runCount, 1, 0, 0, positions, adjustments);
    if (!search.found && !search.ambiguous) {
        for (uint8_t count = 2; count <= MAX_CORRECTED_BITS; ++count) {
            Detail::combinations(search, runs, runCount, count, 0, 0, positions, adjustments);
            if (search.ambiguous || search.exhausted) break;
        }
    }
    if (search.ambiguous) result.status = Status::Ambiguous;
    else if (search.exhausted) result.status = Status::SearchLimit;
    else if (search.found) {
        result = search.result;
        result.status = Status::Repaired;
        memcpy(payload, search.unique + 1, result.length);
    }
    return result;
}
}
