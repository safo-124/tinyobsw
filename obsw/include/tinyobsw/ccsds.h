#ifndef TINYOBSW_CCSDS_H
#define TINYOBSW_CCSDS_H

/*
 * CCSDS Space Packet primary header (CCSDS 133.0-B-2).
 *
 *  Byte 0          Byte 1     Byte 2           Byte 3     Bytes 4-5
 * +---+-+-+-------+----------+---+------------+----------+-----------------+
 * |ver|T|S|APID hi| APID lo  |SF | count hi   | count lo | data length     |
 * | 3 |1|1|   3   |    8     | 2 |     6      |    8     |      16         |
 * +---+-+-+-------+----------+---+------------+----------+-----------------+
 *
 * All fields are big-endian. "data length" holds the number of bytes in the
 * packet data field MINUS ONE, so a whole packet is 6 + data_len + 1 bytes.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "tinyobsw/status.h"

#define CCSDS_PRIMARY_HDR_LEN 6u
#define CCSDS_APID_MAX        0x07FFu
#define CCSDS_APID_IDLE       0x07FFu /* reserved for idle packets */
#define CCSDS_SEQ_COUNT_MAX   0x3FFFu

typedef enum {
    CCSDS_TYPE_TM = 0, /* telemetry: spacecraft -> ground */
    CCSDS_TYPE_TC = 1  /* telecommand: ground -> spacecraft */
} ccsds_type_t;

typedef enum {
    CCSDS_SEQ_CONTINUATION = 0,
    CCSDS_SEQ_FIRST        = 1,
    CCSDS_SEQ_LAST         = 2,
    CCSDS_SEQ_UNSEGMENTED  = 3
} ccsds_seq_flags_t;

typedef struct {
    uint8_t           version;       /* 3 bits, always 0 */
    ccsds_type_t      type;          /* 1 bit */
    bool              sec_hdr_flag;  /* 1 bit: PUS secondary header present */
    uint16_t          apid;          /* 11 bits: application process ID */
    ccsds_seq_flags_t seq_flags;     /* 2 bits */
    uint16_t          seq_count;     /* 14 bits */
    uint16_t          data_len;      /* 16 bits: data field length - 1 */
} ccsds_primary_hdr_t;

/* Write the 6-byte header into buf. Rejects fields that do not fit. */
obsw_status_t ccsds_encode_primary(const ccsds_primary_hdr_t *hdr,
                                   uint8_t *buf, size_t buf_len);

/* Read the 6-byte header from buf. Rejects version != 0. */
obsw_status_t ccsds_decode_primary(const uint8_t *buf, size_t buf_len,
                                   ccsds_primary_hdr_t *hdr);

/* Decode the header and check the length field matches len exactly. */
obsw_status_t ccsds_check_packet(const uint8_t *buf, size_t len,
                                 ccsds_primary_hdr_t *hdr);

/* Total packet size in bytes described by hdr: 6 + data_len + 1. */
size_t ccsds_packet_len(const ccsds_primary_hdr_t *hdr);

/* Next sequence count, wrapping from 16383 back to 0. */
uint16_t ccsds_next_seq_count(uint16_t count);

#endif
