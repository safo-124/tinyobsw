#ifndef TINYOBSW_CRC16_H
#define TINYOBSW_CRC16_H
#include <stddef.h>
#include <stdint.h>
/* CRC-16-CCITT for ECSS PUS packet error control: poly 0x1021, init 0xFFFF. */
uint16_t obsw_crc16(const uint8_t *data, size_t len);
#endif
