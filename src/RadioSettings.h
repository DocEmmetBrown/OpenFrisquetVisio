#pragma once

namespace RadioSettings {
#if defined(BOARD_TTGO_LORA32)
constexpr float DEFAULT_RX_BANDWIDTH = 250.0f;
static constexpr float RX_BANDWIDTHS[] = {
    2.6f, 3.1f, 3.9f, 5.2f, 6.3f, 7.8f, 10.4f, 12.5f, 15.6f,
    20.8f, 25.0f, 31.3f, 41.7f, 50.0f, 62.5f, 83.3f, 100.0f,
    125.0f, 166.7f, 200.0f, 250.0f
};
#else
constexpr float DEFAULT_RX_BANDWIDTH = 234.3f;

// Valeurs discrètes acceptées par RadioLib pour le modem GFSK du SX1262 (kHz).
static constexpr float RX_BANDWIDTHS[] = {
    4.8f, 5.8f, 7.3f, 9.7f, 11.7f, 14.6f, 19.5f, 23.4f, 29.3f,
    39.0f, 46.9f, 58.6f, 78.2f, 93.8f, 117.3f, 156.2f, 187.2f,
    234.3f, 312.0f, 373.6f, 467.0f
};

#endif
constexpr bool DEFAULT_LNA_BYPASS = true;

inline bool validRxBandwidth(float value) {
    for (float allowed : RX_BANDWIDTHS) {
        if (value >= allowed - 0.001f && value <= allowed + 0.001f) return true;
    }
    return false;
}

inline bool supportsLnaBypass() {
#if defined(WIFI_LORA_32_V4) && defined(USE_KCT8103L_PA)
    return true;
#else
    return false;
#endif
}
}
