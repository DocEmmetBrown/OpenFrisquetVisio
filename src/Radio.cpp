#include "Radio.h"
#include "Logs.h"

int16_t Radio::PacketRadio::readData(uint8_t* data, size_t len) {
    // Stabiliser le paquet et ses métadonnées pendant la copie. Le réarmement
    // reste à la charge de l'appelant, comme pour receive() bloquant.
    lastRxLength = 0;
    lastRxOffset = 0;
    int16_t result = standby();
    if (result != RADIOLIB_ERR_NONE) return result;
    lastRxIrq = getIrqStatus();
    if (!(lastRxIrq & (RADIOLIB_SX126X_IRQ_RX_DONE | RADIOLIB_SX126X_IRQ_CRC_ERR
                       | RADIOLIB_SX126X_IRQ_HEADER_ERR))) {
        return RADIOLIB_ERR_RX_TIMEOUT;
    }

    // RadioLib 6.6.0 lit depuis zéro. En RX continu, le SX1262 peut ranger
    // plusieurs paquets successifs à des offsets différents dans son tampon.
    uint8_t status[2] = {0, 0};
    result = getMod()->SPIreadStream(RADIOLIB_SX126X_CMD_GET_RX_BUFFER_STATUS, status, 2);
    if (result != RADIOLIB_ERR_NONE) return result;
    lastRxLength = status[0];
    lastRxOffset = status[1];
    const size_t capturedLength = len != 0 && len < lastRxLength ? len : lastRxLength;
    result = readBuffer(data, static_cast<uint8_t>(capturedLength), lastRxOffset);
    if (result != RADIOLIB_ERR_NONE) return result;
    result = clearIrqStatus();
    if (result != RADIOLIB_ERR_NONE) return result;
    if (lastRxIrq & (RADIOLIB_SX126X_IRQ_CRC_ERR | RADIOLIB_SX126X_IRQ_HEADER_ERR)) {
        return RADIOLIB_ERR_CRC_MISMATCH;
    }
    return RADIOLIB_ERR_NONE;
}

int16_t Radio::receive(uint8_t data[], size_t len) {
    _radio.lastRxIrq = 0;
    _radio.lastRxLength = 0;
    _radio.lastRxOffset = 0;
    return finishReceive(_radio.receive(data, len), "receive", data, len);
}

int16_t Radio::readData(uint8_t data[], size_t len) {
    _radio.lastRxIrq = 0;
    _radio.lastRxLength = 0;
    _radio.lastRxOffset = 0;
    // Le contenu du tampon survit à sa lecture, contrairement aux IRQ. Sans
    // nouvel événement RX, le relire pourrait valider un ancien paquet CRC.
    if (!hasReceivedPacket()) {
        _lastPacketRSSI = NAN;
        return RADIOLIB_ERR_RX_TIMEOUT;
    }
    return finishReceive(_radio.readData(data, len), "readData", data, len);
}

bool Radio::hasReceivedPacket() {
    const uint16_t irq = _radio.getIrqStatus();
    return (irq & (RADIOLIB_SX126X_IRQ_RX_DONE | RADIOLIB_SX126X_IRQ_CRC_ERR
                   | RADIOLIB_SX126X_IRQ_HEADER_ERR)) != 0;
}

int16_t Radio::finishReceive(int16_t result, const char* operation, const uint8_t* data, size_t requestedLength) {
    // RadioLib a lu le paquet même lorsque son CRC est invalide. Relever le
    // RSSI avant le prochain réarmement, mais conserver l'erreur pour le rejeter.
    const bool packetRead = result == RADIOLIB_ERR_NONE || result == RADIOLIB_ERR_CRC_MISMATCH;
    _lastPacketRSSI = packetRead ? _radio.getRSSI() : NAN;
    if (result == RADIOLIB_ERR_CRC_MISMATCH) {
        const size_t packetLength = _radio.lastRxLength;
        const size_t capturedLength = requestedLength != 0 && requestedLength < packetLength
            ? requestedLength : packetLength;
        const uint16_t irq = _radio.lastRxIrq;
        logs.addLogf("RADIO", "[RX][CRC_ERROR][code=%d][len=%u][captured=%u][RSSI=%.1f dBm][via=%s][IRQ=0x%04X][RX_DONE=%u][CRC_ERR=%u][HEADER_ERR=%u][RX_OFFSET=%u]",
                     result, static_cast<unsigned>(packetLength), static_cast<unsigned>(capturedLength),
                     _lastPacketRSSI, operation, static_cast<unsigned>(irq),
                     (irq & RADIOLIB_SX126X_IRQ_RX_DONE) ? 1U : 0U,
                     (irq & RADIOLIB_SX126X_IRQ_CRC_ERR) ? 1U : 0U,
                     (irq & RADIOLIB_SX126X_IRQ_HEADER_ERR) ? 1U : 0U,
                     static_cast<unsigned>(_radio.lastRxOffset));
        logRadioCrcData(data, capturedLength);
    }
    return result;
}

#if defined(WIFI_LORA_32_V4) && defined(USE_KCT8103L_PA)
namespace {
constexpr uint8_t KCT8103L_POWER_PIN = GPIO_NUM_7;
constexpr uint8_t KCT8103L_ENABLE_PIN = GPIO_NUM_2;
constexpr uint8_t KCT8103L_CTX_PIN = GPIO_NUM_5;
}
#endif

Radio::Radio() : _radio(new Module(SS, DIO0, RST_LoRa, BUSY_LoRa)) {}

void Radio::init() {
#if defined(WIFI_LORA_32_V4) && defined(USE_KCT8103L_PA)
    // Heltec WiFi LoRa 32 V4.3: power and enable the KCT8103L FEM.
    pinMode(KCT8103L_POWER_PIN, OUTPUT);
    digitalWrite(KCT8103L_POWER_PIN, HIGH);
    pinMode(KCT8103L_ENABLE_PIN, OUTPUT);
    digitalWrite(KCT8103L_ENABLE_PIN, HIGH);

    // PA_CTX high selects RX bypass when DIO2 is low, and TX when DIO2 is
    // high. beginFSK() configures the SX1262 to control DIO2 automatically.
    const uint32_t rfSwitchPins[Module::RFSWITCH_MAX_PINS] = {
        KCT8103L_CTX_PIN, RADIOLIB_NC, RADIOLIB_NC, RADIOLIB_NC, RADIOLIB_NC
    };
    // RadioLib keeps a reference to this table: it must outlive init().
    static const Module::RfSwitchMode_t rfSwitchTable[] = {
        { Module::MODE_IDLE, { LOW } },
        { Module::MODE_RX,   { HIGH } },
        { Module::MODE_TX,   { HIGH } },
        END_OF_MODE_TABLE,
    };
    _radio.setRfSwitchTable(rfSwitchPins, rfSwitchTable);
    info("[RADIO] KCT8103L : bypass du LNA configuré en réception (GPIO5 HIGH).");
#endif

    _radio.beginFSK();
    _radio.setFrequency(868.96);
    _radio.setBitRate(25.0);
    _radio.setFrequencyDeviation(50.0);
    const int16_t bandwidthResult = _radio.setRxBandwidth(234.3f);
    if (bandwidthResult != RADIOLIB_ERR_NONE) {
        error("[RADIO] Échec configuration bande RX 234.3 kHz : %d", bandwidthResult);
    }
    _radio.setPreambleLength(4);
}
