#pragma once

#include <heltec.h>
#include <RadioLib.h>
#include <math.h>
#include "RadioSettings.h"
#include "RadioCrc.h"

#define SS GPIO_NUM_8
#define RST_LoRa GPIO_NUM_12
#define BUSY_LoRa GPIO_NUM_13
#define DIO0 GPIO_NUM_14

class Radio {

    public:
        static constexpr uint8_t MAX_RX_PACKET_LENGTH = RadioCrc::MAX_PAYLOAD;
        Radio();
        void init(float rxBandwidth = RadioSettings::DEFAULT_RX_BANDWIDTH,
                  bool lnaBypass = RadioSettings::DEFAULT_LNA_BYPASS);

        int16_t startReceive() { return _radio.startReceive(); }
        int16_t receive(uint8_t data[], size_t len);
        int16_t readData(uint8_t data[], size_t len);
        bool hasReceivedPacket();
        float getLastPacketRSSI() const { return _lastPacketRSSI; }
        int16_t transmit(byte data[], size_t len);
        size_t getPacketLength() const { return _radio.lastRxLength; }
        void onReceive(void (*func)()) { _radio.setPacketReceivedAction(func);}
        void setSyncWord(uint8_t* syncWord, size_t len) { _radio.setSyncWord(syncWord, len); }

    private:
        int16_t finishReceive(int16_t result, const char* operation, const uint8_t* data, size_t requestedLength);
        // Les chemins bloquant et asynchrone partagent la lecture brute et la
        // validation logicielle ; les métadonnées survivent à l'effacement IRQ.
        struct PacketRadio : public SX1262 {
            using SX1262::SX1262;
            uint16_t lastRxIrq = 0;
            size_t lastRxLength = 0;
            uint8_t lastRxOffset = 0;
            uint8_t raw[RadioCrc::RAW_LENGTH] = {};
            size_t rawLength = 0;
            RadioCrc::Result crcResult;
            int16_t startReceive() override;
            int16_t receive(uint8_t* data, size_t len, RadioLibTime_t timeout = 0) override;
            int16_t readData(uint8_t* data, size_t len) override;
            int16_t startReceiveWithLimit(uint32_t timeout);
        };
        PacketRadio _radio;
        float _lastPacketRSSI = NAN;
};
