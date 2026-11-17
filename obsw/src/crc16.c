#include "tinyobsw/crc16.h"

uint16_t obsw_crc16(const uint8_t *data, size_t len)
{
    uint16_t crc = 0xFFFFu;
    size_t i;
    for (i = 0; i < len; ++i) {
        int bit;
        crc = (uint16_t)(crc ^ ((uint16_t)data[i] << 8));
        for (bit = 0; bit < 8; ++bit) {
            if (crc & 0x8000u) {
                crc = (uint16_t)((crc << 1) ^ 0x1021u);
            } else {
                crc = (uint16_t)(crc << 1);
            }
        }
    }
    return crc;
}
