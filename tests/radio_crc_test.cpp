#include "RadioCrc.h"
#include <assert.h>
#include <stdio.h>

using namespace RadioCrc;

static void seal(uint8_t* raw) {
    const uint16_t crc = crc16(raw, raw[0] + 1);
    raw[raw[0] + 1] = crc >> 8;
    raw[raw[0] + 2] = crc;
}

static void slip(const uint8_t* src, uint8_t* dst, size_t at, bool extraZero) {
    memset(dst, 0, RAW_LENGTH);
    for (size_t bit = 0; bit < RAW_LENGTH * 8 - 1; ++bit) {
        bool value = bit < at ? getBit(src, bit)
            : extraZero ? (bit == at ? false : getBit(src, bit - 1)) : getBit(src, bit + 1);
        if (value) dst[bit / 8] |= 1U << (7 - bit % 8);
    }
}

int main() {
    // Vecteur indépendant : CRC-16/SPI-FUJITSU de "123456789" = E5CC,
    // puis complément final demandé par le SX1262 = 1A33.
    assert(crc16(reinterpret_cast<const uint8_t*>("123456789"), 9) == 0x1A33);
    const uint8_t logged[] = {
        63, 0x7E,0x80,0xAA,0xBE,0x81,0x03,0x38,0x00,0xCD,0x04,0xF6,0x04,0xF6,0x01,0x80,
        0x00,0xC6,0x00,0xC6,0x00,0xCD,0,0,0,0,0,0x67,0x02,0x9F,0x02,0xB1,0x02,0x28,
        0x04,0xF6,0x04,0xF6,0x01,0xE7,0,0,0,0,0,0,0,0,0x1E,0,0,0x05,0,0,0,0,0,0,0,0,0,0,0,0
    };
    static_assert(sizeof(logged) == 64, "log fixture");
    assert(crc16(logged, sizeof(logged)) == 0x222F);
    uint8_t raw[RAW_LENGTH] = {}, damaged[RAW_LENGTH], out[MAX_PAYLOAD + 2];
    memcpy(raw, logged, sizeof(logged));
    seal(raw);
    memset(out, 0xA5, sizeof(out));
    assert(decode(raw, sizeof(raw), out + 1).status == Status::Valid);
    assert(memcmp(out + 1, raw + 1, MAX_PAYLOAD) == 0);
    assert(out[0] == 0xA5 && out[MAX_PAYLOAD + 1] == 0xA5);

    // Perte ou ajout n'importe où dans la dernière suite : reconstruction au
    // début de cette suite, CRC compris. Les octets de marge sont du bruit.
    const bool directions[] = {false, true};
    for (bool extra : directions) {
        for (size_t bit = 52 * 8; bit < 64 * 8; ++bit) {
            slip(raw, damaged, bit, extra);
            damaged[67] = 0xAD;
            const Result result = decode(damaged, sizeof(damaged), out + 1);
            assert(result.status == Status::Repaired);
            assert(result.adjustment == (extra ? -1 : 1));
            assert(memcmp(out + 1, raw + 1, MAX_PAYLOAD) == 0);
        }
    }
    // Premier bit de CRC à 1 : le zéro manquant produit bien un 01 final.
    raw[4] = 0xC0;
    raw[31] = 0xA4; raw[33] = 0x1F; raw[39] = 0xDE;
    seal(raw);
    assert(raw[64] & 0x80);
    slip(raw, damaged, 450, false);
    assert(damaged[63] == 1);
    assert(decode(damaged, sizeof(damaged), out).status == Status::Repaired);

    // Deux reconstructions différentes passent le CRC : rejeter sans copier.
    const uint8_t ambiguous[RAW_LENGTH] = {
        0x3F,0xB7,0xCE,0xD6,0xAF,0x03,0x03,0x46,0x97,0xA7,0x5F,0x13,
        0,0,0,0,0,0,0xD9,0x48,0x55,0x02,0xD9,0x54,0,0,0,0,0,0,
        0x98,0x8E,0xFE,0x82,0x44,0x2E,0,0,0,0,0,0,
        0xB5,0x6F,0xCF,0x37,0x43,0x92,0x22,0xD0,0x63,0x27,
        0,0,0,0,0,0,0,0,0,0,0,0x01,0x80,0xF5,0x60,0x08
    };
    memset(out, 0xA5, sizeof(out));
    assert(decode(ambiguous, sizeof(ambiguous), out).status == Status::Ambiguous);
    for (uint8_t byte : out) assert(byte == 0xA5);

    // Glissement au milieu du paquet, vers le bit 256, et sur réponse courte.
    const uint8_t slipLengths[] = {15, 49, 63};
    for (uint8_t length : slipLengths) {
        memset(raw, 0xAD, sizeof(raw));
        raw[0] = length;
        const size_t start = length == 15 ? 9 : 32;
        memset(raw + start, 0, 5);
        seal(raw);
        for (bool extra : directions) {
            slip(raw, damaged, start * 8 + 16, extra);
            assert(decode(damaged, sizeof(damaged), out).status == Status::Repaired);
            assert(memcmp(out, raw + 1, length) == 0);
        }
    }

    // Formats courts et toutes les longueurs supportées : ignorer la marge.
    for (size_t length = HEADER_LENGTH; length <= MAX_PAYLOAD; ++length) {
        memset(raw, 0xAD, sizeof(raw));
        raw[0] = length;
        seal(raw);
        assert(decode(raw, sizeof(raw), out).status == Status::Valid);
    }
    // Ne pas tenter de réparer l'en-tête, les runs courts ou le CRC seul.
    const size_t corruptBits[] = {12, 80, 64 * 8};
    for (size_t bit : corruptBits) {
        memcpy(damaged, raw, sizeof(raw));
        damaged[bit / 8] ^= 1U << (7 - bit % 8);
        memset(out, 0xA5, sizeof(out));
        assert(decode(damaged, sizeof(damaged), out).status == Status::Invalid);
        for (uint8_t byte : out) assert(byte == 0xA5);
    }
    const uint8_t badLengths[] = {0, 5, 64, 255};
    for (uint8_t length : badLengths) {
        raw[0] = length;
        assert(decode(raw, sizeof(raw), out).status == Status::InvalidLength);
    }
    assert(decode(nullptr, 0, out).status == Status::Invalid);
    assert(decode(raw, sizeof(raw), nullptr).status == Status::Invalid);
    // Toutes les tailles de capture tronquées ou excessives restent bornées.
    raw[0] = MAX_PAYLOAD;
    for (size_t length = 0; length < MAX_PAYLOAD + 3; ++length)
        assert(decode(raw, length, out).status != Status::Repaired);
    assert(decode(raw, RAW_LENGTH + 1, out).status == Status::Invalid);
    puts("OK: CRC, valid frames 6..63, both slips, final 01, ambiguity, rejection and bounds");
}
