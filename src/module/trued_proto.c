#include "trued_proto.h"

uint16_t trued_crc16_xmodem(const uint8_t *buf, size_t len) {
    uint16_t crc = 0;
    for (size_t i = 0; i < len; i++) {
        crc ^= (uint16_t)buf[i] << 8;
        for (int k = 0; k < 8; k++)
            crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
    }
    return crc;
}

void trued_build_channel_cmd(uint8_t channel, uint8_t out[TRUED_CMD_LEN]) {
    static const uint8_t header[7] = {0xFD, 0x02, 0x02, 0x05, 0x41, 0xEE, 0x00};

    for (int i = 0; i < 7; i++)
        out[i] = header[i];
    out[7] = channel;

    uint16_t crc = trued_crc16_xmodem(out, 8);
    out[8] = crc >> 8;
    out[9] = crc & 0xFF;
}
