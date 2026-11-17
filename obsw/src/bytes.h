#ifndef TINYOBSW_BYTES_H
#define TINYOBSW_BYTES_H

/* Private big-endian helpers. Space protocols send the most significant byte first. */

#include <stddef.h>
#include <stdint.h>

static inline void put_be16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)(v & 0xFFu);
}

static inline uint16_t get_be16(const uint8_t *p)
{
    return (uint16_t)(((unsigned)p[0] << 8) | (unsigned)p[1]);
}

static inline void put_be32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)((v >> 16) & 0xFFu);
    p[2] = (uint8_t)((v >> 8) & 0xFFu);
    p[3] = (uint8_t)(v & 0xFFu);
}

static inline uint32_t get_be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

/* Plain byte copy, so the library needs no C library at all (not even memcpy). */
static inline void copy_bytes(uint8_t *dst, const uint8_t *src, size_t n)
{
    size_t i;
    for (i = 0; i < n; ++i) {
        dst[i] = src[i];
    }
}

#endif
