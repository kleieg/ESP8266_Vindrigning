#include <SoftwareSerial.h>
#include "log.h"

namespace VindriktningPM25
{
    constexpr static const uint8_t PIN_UART_RX = 0; // GPIO16 = U2_RXD (ESP32)
    constexpr static const uint8_t PIN_UART_TX = D4; // internal LED

    SoftwareSerial sensorSerial(PIN_UART_RX, PIN_UART_TX);

    uint8_t serialRxBuf[255];
    uint8_t rxBufIdx = 0;

    struct SensorState
    {
        uint16_t avgPM25 = 0;
        uint16_t measurements[5] = {0, 0, 0, 0, 0};
        uint8_t measurementIdx = 0;
        boolean valid = false;
    };

    void setup()
    {
        sensorSerial.begin(9600);
    }

    void clearRxBuf()
    {
        // Clear everything for the next message
        memset(serialRxBuf, 0, sizeof(serialRxBuf));
        rxBufIdx = 0;
    }

    bool parseState(SensorState& state)
    {
        /**
         *         MSB  DF 3     DF 4  LSB
         * uint16_t = xxxxxxxx xxxxxxxx
         */
        const uint16_t pm25 = (serialRxBuf[5] << 8) | serialRxBuf[6];

        LOG_PRINTF("Received PM 2.5 reading: %d\n", pm25);
        state.measurements[state.measurementIdx] = pm25;
        state.measurementIdx = (state.measurementIdx + 1) % 5;

        clearRxBuf();
        
        if (state.measurementIdx == 0) {
            float avgPM25 = 0.0f;
            for (uint8_t i = 0; i < 5; ++i) {
                avgPM25 += state.measurements[i] / 5.0f;
            }
            state.avgPM25 = avgPM25;
            state.valid = true;
            LOG_PRINTF("New Avg PM25: %d\n", state.avgPM25);
            return true;
        }
        return false;
    }

    bool isValidHeader()
    {
        bool headerValid = serialRxBuf[0] == 0x16 && serialRxBuf[1] == 0x11 && serialRxBuf[2] == 0x0B;
        if (!headerValid) {
            LOG_PRINTLN("Received message with invalid header.");
        }
        return headerValid;
    }

    bool isValidChecksum()
    {
        uint8_t checksum = 0;
        for (uint8_t i = 0; i < 20; i++) {
            checksum += serialRxBuf[i];
        }
        if (checksum != 0) {
            LOG_PRINTF("Received message with invalid checksum. Expected: 0. Actual: %d\n", checksum);
        }
        return checksum == 0;
    }

    bool handleUart(SensorState& state)
    {
        if (!sensorSerial.available()) {
            return false;
        }

        // @TODO: sometimes multiple messages are received at the same time, only the first is handled
        // not really an issue for now..
        LOG_PRINT("Receiving:");
        while (sensorSerial.available()) {
            serialRxBuf[rxBufIdx++] = sensorSerial.read();
            LOG_PRINT(".");

            // Without this delay, receiving data breaks for reasons that are beyond me
            delay(15);

            if (rxBufIdx >= 64) {
                clearRxBuf();
            }
        }

        LOG_PRINTLN("Done.");

        if (isValidHeader() && isValidChecksum()) {
            return parseState(state);
        } else {
            clearRxBuf();
            return false;
        }
    }
}