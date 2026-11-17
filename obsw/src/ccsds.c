#include "tinyobsw/ccsds.h"

/* Big-endian helpers. Kept static so they vanish into the callers. */
static void put_be16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)(v & 0xFFu);
}

static uint16_t get_be16(const uint8_t *p)
{
    return (uint16_t)(((unsigned)p[0] << 8) | (unsigned)p[1]);
}

obsw_status_t ccsds_encode_primary(const ccsds_primary_hdr_t *hdr,
                                   uint8_t *buf, size_t buf_len)
{
    unsigned word0;
    unsigned word1;

    if (hdr == NULL || buf == NULL) {
        return OBSW_ERR_NULL;
    }
    if (buf_len < CCSDS_PRIMARY_HDR_LEN) {
        return OBSW_ERR_SHORT_BUFFER;
    }
    if (hdr->version != 0u ||
        (unsigned)hdr->type > 1u ||
        hdr->apid > CCSDS_APID_MAX ||
        (unsigned)hdr->seq_flags > 3u ||
        hdr->seq_count > CCSDS_SEQ_COUNT_MAX) {
        return OBSW_ERR_FIELD_RANGE;
    }

    word0 = ((unsigned)hdr->version << 13) |
            ((unsigned)hdr->type << 12) |
            ((hdr->sec_hdr_flag ? 1u : 0u) << 11) |
            (unsigned)hdr->apid;
    word1 = ((unsigned)hdr->seq_flags << 14) |
            (unsigned)hdr->seq_count;

    put_be16(&buf[0], (uint16_t)word0);
    put_be16(&buf[2], (uint16_t)word1);
    put_be16(&buf[4], hdr->data_len);
    return OBSW_OK;
}

obsw_status_t ccsds_decode_primary(const uint8_t *buf, size_t buf_len,
                                   ccsds_primary_hdr_t *hdr)
{
    unsigned word0;
    unsigned word1;

    if (buf == NULL || hdr == NULL) {
        return OBSW_ERR_NULL;
    }
    if (buf_len < CCSDS_PRIMARY_HDR_LEN) {
        return OBSW_ERR_SHORT_BUFFER;
    }

    word0 = get_be16(&buf[0]);
    word1 = get_be16(&buf[2]);

    if ((word0 >> 13) != 0u) {
        return OBSW_ERR_BAD_VERSION;
    }

    hdr->version      = 0u;
    hdr->type         = ((word0 >> 12) & 1u) ? CCSDS_TYPE_TC : CCSDS_TYPE_TM;
    hdr->sec_hdr_flag = ((word0 >> 11) & 1u) != 0u;
    hdr->apid         = (uint16_t)(word0 & CCSDS_APID_MAX);
    hdr->seq_flags    = (ccsds_seq_flags_t)(word1 >> 14);
    hdr->seq_count    = (uint16_t)(word1 & CCSDS_SEQ_COUNT_MAX);
    hdr->data_len     = get_be16(&buf[4]);
    return OBSW_OK;
}

size_t ccsds_packet_len(const ccsds_primary_hdr_t *hdr)
{
    return (size_t)CCSDS_PRIMARY_HDR_LEN + (size_t)hdr->data_len + 1u;
}

obsw_status_t ccsds_check_packet(const uint8_t *buf, size_t len,
                                 ccsds_primary_hdr_t *hdr)
{
    obsw_status_t st = ccsds_decode_primary(buf, len, hdr);
    if (st != OBSW_OK) {
        return st;
    }
    if (ccsds_packet_len(hdr) != len) {
        return OBSW_ERR_LENGTH_MISMATCH;
    }
    return OBSW_OK;
}

uint16_t ccsds_next_seq_count(uint16_t count)
{
    return (uint16_t)(((unsigned)count + 1u) & CCSDS_SEQ_COUNT_MAX);
}
