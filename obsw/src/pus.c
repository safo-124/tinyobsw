#include "tinyobsw/pus.h"

#include "tinyobsw/crc16.h"
#include "bytes.h"

/* ------------------------------------------------------------------ */
/* Building                                                           */
/* ------------------------------------------------------------------ */

/*
 * Checks shared by TC and TM building. Touches nothing; on success
 * *total is the size of the finished packet.
 */
static obsw_status_t check_build_args(uint16_t apid, uint16_t seq_count,
                                      const void *sec,
                                      const uint8_t *data, size_t data_len,
                                      const uint8_t *buf, size_t buf_len,
                                      const size_t *out_len,
                                      size_t overhead, size_t *total)
{
    /* The CCSDS data field (everything after the primary header) is at most 65536 bytes. */
    const size_t max_data = (size_t)0x10000u - (overhead - CCSDS_PRIMARY_HDR_LEN);

    if (sec == NULL || buf == NULL || out_len == NULL ||
        (data == NULL && data_len != 0u)) {
        return OBSW_ERR_NULL;
    }
    if (apid > CCSDS_APID_MAX || seq_count > CCSDS_SEQ_COUNT_MAX ||
        data_len > max_data) {
        return OBSW_ERR_FIELD_RANGE;
    }
    *total = overhead + data_len;
    if (buf_len < *total) {
        return OBSW_ERR_SHORT_BUFFER;
    }
    return OBSW_OK;
}

static obsw_status_t write_primary(ccsds_type_t type, uint16_t apid,
                                   uint16_t seq_count, size_t total,
                                   uint8_t *buf)
{
    ccsds_primary_hdr_t h;
    h.version      = 0u;
    h.type         = type;
    h.sec_hdr_flag = true;
    h.apid         = apid;
    h.seq_flags    = CCSDS_SEQ_UNSEGMENTED;
    h.seq_count    = seq_count;
    h.data_len     = (uint16_t)(total - CCSDS_PRIMARY_HDR_LEN - 1u);
    return ccsds_encode_primary(&h, buf, CCSDS_PRIMARY_HDR_LEN);
}

static void append_crc(uint8_t *buf, size_t total)
{
    put_be16(&buf[total - PUS_CRC_LEN], obsw_crc16(buf, total - PUS_CRC_LEN));
}

obsw_status_t pus_tc_build(uint16_t apid, uint16_t seq_count,
                           const pus_tc_sec_hdr_t *sec,
                           const uint8_t *data, size_t data_len,
                           uint8_t *buf, size_t buf_len, size_t *out_len)
{
    size_t total = 0u;
    uint8_t *p;
    obsw_status_t st = check_build_args(apid, seq_count, sec, data, data_len,
                                        buf, buf_len, out_len,
                                        PUS_TC_OVERHEAD, &total);
    if (st != OBSW_OK) {
        return st;
    }
    if (sec->ack_flags > 0xFu) {
        return OBSW_ERR_FIELD_RANGE;
    }
    /* All checks passed: from here on, writing cannot fail halfway. */
    st = write_primary(CCSDS_TYPE_TC, apid, seq_count, total, buf);
    if (st != OBSW_OK) {
        return st;
    }

    p = &buf[CCSDS_PRIMARY_HDR_LEN];
    p[0] = (uint8_t)((PUS_VERSION_C << 4) | (unsigned)sec->ack_flags);
    p[1] = sec->service;
    p[2] = sec->subtype;
    put_be16(&p[3], sec->source_id);
    copy_bytes(&p[PUS_TC_SEC_HDR_LEN], data, data_len);
    append_crc(buf, total);

    *out_len = total;
    return OBSW_OK;
}

obsw_status_t pus_tm_build(uint16_t apid, uint16_t seq_count,
                           const pus_tm_sec_hdr_t *sec,
                           const uint8_t *data, size_t data_len,
                           uint8_t *buf, size_t buf_len, size_t *out_len)
{
    size_t total = 0u;
    uint8_t *p;
    obsw_status_t st = check_build_args(apid, seq_count, sec, data, data_len,
                                        buf, buf_len, out_len,
                                        PUS_TM_OVERHEAD, &total);
    if (st != OBSW_OK) {
        return st;
    }
    if (sec->time_ref_status > 0xFu) {
        return OBSW_ERR_FIELD_RANGE;
    }
    st = write_primary(CCSDS_TYPE_TM, apid, seq_count, total, buf);
    if (st != OBSW_OK) {
        return st;
    }

    p = &buf[CCSDS_PRIMARY_HDR_LEN];
    p[0] = (uint8_t)((PUS_VERSION_C << 4) | (unsigned)sec->time_ref_status);
    p[1] = sec->service;
    p[2] = sec->subtype;
    put_be16(&p[3], sec->msg_counter);
    put_be16(&p[5], sec->dest_id);
    put_be32(&p[7], sec->time.coarse);
    put_be16(&p[11], sec->time.fine);
    copy_bytes(&p[PUS_TM_SEC_HDR_LEN], data, data_len);
    append_crc(buf, total);

    *out_len = total;
    return OBSW_OK;
}

/* ------------------------------------------------------------------ */
/* Parsing                                                            */
/* ------------------------------------------------------------------ */

/*
 * Checks shared by TC and TM parsing. The CRC is checked straight after
 * the length: a corrupted packet should be reported as corrupted, not as
 * whatever its damaged bits now happen to say.
 */
static obsw_status_t check_packet(const uint8_t *buf, size_t len,
                                  ccsds_type_t want, size_t overhead,
                                  ccsds_primary_hdr_t *primary)
{
    /* Smallest packet worth inspecting: primary header, one byte, CRC. */
    const size_t min_len = CCSDS_PRIMARY_HDR_LEN + 1u + PUS_CRC_LEN;

    obsw_status_t st = ccsds_check_packet(buf, len, primary);
    if (st != OBSW_OK) {
        return st;
    }
    if (len < min_len) {
        return OBSW_ERR_SHORT_BUFFER;
    }
    if (obsw_crc16(buf, len - PUS_CRC_LEN) != get_be16(&buf[len - PUS_CRC_LEN])) {
        return OBSW_ERR_CRC;
    }
    if (primary->type != want) {
        return OBSW_ERR_WRONG_TYPE;
    }
    if (!primary->sec_hdr_flag) {
        return OBSW_ERR_NO_SEC_HDR;
    }
    if (primary->seq_flags != CCSDS_SEQ_UNSEGMENTED) {
        return OBSW_ERR_UNSUPPORTED;
    }
    if (len < overhead) {
        return OBSW_ERR_SHORT_BUFFER;
    }
    if (((unsigned)buf[CCSDS_PRIMARY_HDR_LEN] >> 4) != PUS_VERSION_C) {
        return OBSW_ERR_BAD_PUS_VERSION;
    }
    return OBSW_OK;
}

obsw_status_t pus_tc_parse(const uint8_t *buf, size_t len, pus_tc_t *out)
{
    pus_tc_t tc;
    const uint8_t *p;
    obsw_status_t st;

    if (buf == NULL || out == NULL) {
        return OBSW_ERR_NULL;
    }
    st = check_packet(buf, len, CCSDS_TYPE_TC, PUS_TC_OVERHEAD, &tc.primary);
    if (st != OBSW_OK) {
        return st;
    }

    p = &buf[CCSDS_PRIMARY_HDR_LEN];
    tc.sec.ack_flags = (uint8_t)(p[0] & 0x0Fu);
    tc.sec.service   = p[1];
    tc.sec.subtype   = p[2];
    tc.sec.source_id = get_be16(&p[3]);
    tc.data          = &p[PUS_TC_SEC_HDR_LEN];
    tc.data_len      = len - PUS_TC_OVERHEAD;

    *out = tc; /* only written on success */
    return OBSW_OK;
}

obsw_status_t pus_tm_parse(const uint8_t *buf, size_t len, pus_tm_t *out)
{
    pus_tm_t tm;
    const uint8_t *p;
    obsw_status_t st;

    if (buf == NULL || out == NULL) {
        return OBSW_ERR_NULL;
    }
    st = check_packet(buf, len, CCSDS_TYPE_TM, PUS_TM_OVERHEAD, &tm.primary);
    if (st != OBSW_OK) {
        return st;
    }

    p = &buf[CCSDS_PRIMARY_HDR_LEN];
    tm.sec.time_ref_status = (uint8_t)(p[0] & 0x0Fu);
    tm.sec.service         = p[1];
    tm.sec.subtype         = p[2];
    tm.sec.msg_counter     = get_be16(&p[3]);
    tm.sec.dest_id         = get_be16(&p[5]);
    tm.sec.time.coarse     = get_be32(&p[7]);
    tm.sec.time.fine       = get_be16(&p[11]);
    tm.data                = &p[PUS_TM_SEC_HDR_LEN];
    tm.data_len            = len - PUS_TM_OVERHEAD;

    *out = tm;
    return OBSW_OK;
}
