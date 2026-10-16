#pragma once

namespace RadioSettings {
constexpr float DEFAULT_RX_BANDWIDTH = 234.3f;
constexpr bool DEFAULT_LNA_BYPASS = true;

// Valeurs discrètes acceptées par RadioLib pour le modem GFSK du SX1262 (kHz).
static constexpr float RX_BANDWIDTHS[] = {
    4.8f, 5.8f, 7.3f, 9.7f, 11.7f, 14.6f, 19.5f, 23.4f, 29.3f,
    39.0f, 46.9f, 58.6f, 78.2f, 93.8f, 117.3f, 156.2f, 187.2f,
    234.3f, 312.0f, 373.6f, 467.0f
};

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
