#ifndef TINYOBSW_PUS_H
#define TINYOBSW_PUS_H

/*
 * ECSS PUS-C (ECSS-E-ST-70-41C) telecommand and telemetry packets.
 *
 * Telecommand (ground -> spacecraft):
 *   [ CCSDS primary 6 ][ TC secondary 5 ][ application data N ][ CRC 2 ]
 *
 *   TC secondary header:
 *     byte 0   : PUS version (4 bits, =2) | acknowledgement flags (4 bits)
 *     byte 1   : service type ID
 *     byte 2   : message subtype ID
 *     byte 3-4 : source ID
 *
 * Telemetry (spacecraft -> ground):
 *   [ CCSDS primary 6 ][ TM secondary 13 ][ source data N ][ CRC 2 ]
 *
 *   TM secondary header:
 *     byte 0     : PUS version (4 bits, =2) | spacecraft time reference status (4 bits)
 *     byte 1     : service type ID
 *     byte 2     : message subtype ID
 *     byte 3-4   : message type counter
 *     byte 5-6   : destination ID
 *     byte 7-12  : time, CCSDS Unsegmented Code: 4 bytes coarse + 2 bytes fine
 *
 * The CRC is CRC-16-CCITT over every byte before it, stored big-endian.
 * Segmented packets are not supported: we always build and expect
 * "unsegmented" sequence flags.
 */

#include <stddef.h>
#include <stdint.h>

#include "tinyobsw/ccsds.h"
#include "tinyobsw/status.h"

#define PUS_VERSION_C        2u
#define PUS_TC_SEC_HDR_LEN   5u
#define PUS_TM_SEC_HDR_LEN   13u
#define PUS_CRC_LEN          2u

#define PUS_TC_OVERHEAD (CCSDS_PRIMARY_HDR_LEN + PUS_TC_SEC_HDR_LEN + PUS_CRC_LEN) /* 13 */
#define PUS_TM_OVERHEAD (CCSDS_PRIMARY_HDR_LEN + PUS_TM_SEC_HDR_LEN + PUS_CRC_LEN) /* 21 */

/* Acknowledgement flags in a TC: which ST[1] verification reports ground wants back. */
#define PUS_ACK_ACCEPTANCE  0x1u
#define PUS_ACK_START       0x2u
#define PUS_ACK_PROGRESS    0x4u
#define PUS_ACK_COMPLETION  0x8u
#define PUS_ACK_ALL         0xFu

/* CCSDS Unsegmented time Code: seconds plus 1/65536-second fractions. */
typedef struct {
    uint32_t coarse;
    uint16_t fine;
} pus_cuc_time_t;

typedef struct {
    uint8_t  ack_flags;   /* 4 bits */
    uint8_t  service;     /* e.g. 17 for the test service */
    uint8_t  subtype;     /* e.g. 1 for "are you alive?" */
    uint16_t source_id;   /* who sent the command */
} pus_tc_sec_hdr_t;

typedef struct {
    uint8_t        time_ref_status; /* 4 bits */
    uint8_t        service;
    uint8_t        subtype;
    uint16_t       msg_counter;     /* per service/subtype, wraps */
    uint16_t       dest_id;         /* who the report is for */
    pus_cuc_time_t time;            /* on-board time when generated */
} pus_tm_sec_hdr_t;

/*
 * A parsed packet. data points INTO the buffer passed to the parse
 * function (no copying), so it is only valid while that buffer is.
 */
typedef struct {
    ccsds_primary_hdr_t primary;
    pus_tc_sec_hdr_t    sec;
    const uint8_t      *data;
    size_t              data_len;
} pus_tc_t;

typedef struct {
    ccsds_primary_hdr_t primary;
    pus_tm_sec_hdr_t    sec;
    const uint8_t      *data;
    size_t              data_len;
} pus_tm_t;

/*
 * Build a complete packet into buf and set *out_len to its size.
 * data may be NULL only when data_len is 0. Nothing is written to buf
 * unless the call succeeds.
 */
obsw_status_t pus_tc_build(uint16_t apid, uint16_t seq_count,
                           const pus_tc_sec_hdr_t *sec,
                           const uint8_t *data, size_t data_len,
                           uint8_t *buf, size_t buf_len, size_t *out_len);

obsw_status_t pus_tm_build(uint16_t apid, uint16_t seq_count,
                           const pus_tm_sec_hdr_t *sec,
                           const uint8_t *data, size_t data_len,
                           uint8_t *buf, size_t buf_len, size_t *out_len);

/*
 * Validate and parse one complete packet of exactly len bytes:
 * CCSDS header, length field, packet type, secondary header flag,
 * segmentation, PUS version and CRC are all checked.
 */
obsw_status_t pus_tc_parse(const uint8_t *buf, size_t len, pus_tc_t *out);
obsw_status_t pus_tm_parse(const uint8_t *buf, size_t len, pus_tm_t *out);

#endif
