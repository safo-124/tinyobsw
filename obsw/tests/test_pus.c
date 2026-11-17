#include <string.h>

#include "unity.h"
#include "tinyobsw/crc16.h"
#include "tinyobsw/pus.h"

void setUp(void) {}
void tearDown(void) {}

/*
 * Reference packets. The CRC bytes were computed independently with
 * Python's binascii.crc_hqx(data, 0xFFFF), not with our own code.
 */

/* TC ST[17,1] "are you alive?": APID 0x123, seq 0, ack accept+complete, source 1. */
static const uint8_t KNOWN_TC[13] = {
    0x19, 0x23, 0xC0, 0x00, 0x00, 0x06,   /* CCSDS primary header */
    0x29, 0x11, 0x01, 0x00, 0x01,         /* PUS-C TC secondary header */
    0xF4, 0x77                            /* CRC */
};

/* TM ST[17,2] "alive": APID 0x123, seq 7, counter 3, dest 1, time 0x01020304 + 0x8000/65536 s. */
static const uint8_t KNOWN_TM[21] = {
    0x09, 0x23, 0xC0, 0x07, 0x00, 0x0E,               /* CCSDS primary header */
    0x20, 0x11, 0x02, 0x00, 0x03, 0x00, 0x01,         /* PUS-C TM secondary header */
    0x01, 0x02, 0x03, 0x04, 0x80, 0x00,               /*   ... CUC time */
    0x79, 0x0E                                        /* CRC */
};

static pus_tc_sec_hdr_t ping_tc_hdr(void)
{
    pus_tc_sec_hdr_t s;
    s.ack_flags = PUS_ACK_ACCEPTANCE | PUS_ACK_COMPLETION;
    s.service   = 17;
    s.subtype   = 1;
    s.source_id = 1;
    return s;
}

static pus_tm_sec_hdr_t alive_tm_hdr(void)
{
    pus_tm_sec_hdr_t s;
    s.time_ref_status = 0;
    s.service         = 17;
    s.subtype         = 2;
    s.msg_counter     = 3;
    s.dest_id         = 1;
    s.time.coarse     = 0x01020304u;
    s.time.fine       = 0x8000u;
    return s;
}

/* After editing a test packet on purpose, give it a valid CRC again. */
static void fix_crc(uint8_t *pkt, size_t len)
{
    uint16_t c = obsw_crc16(pkt, len - 2u);
    pkt[len - 2u] = (uint8_t)(c >> 8);
    pkt[len - 1u] = (uint8_t)(c & 0xFFu);
}

/* --- building --------------------------------------------------------- */

static void test_build_known_tc(void)
{
    pus_tc_sec_hdr_t s = ping_tc_hdr();
    uint8_t buf[32];
    size_t n = 0;
    TEST_ASSERT_EQUAL(OBSW_OK, pus_tc_build(0x123, 0, &s, NULL, 0, buf, sizeof buf, &n));
    TEST_ASSERT_EQUAL_size_t(sizeof KNOWN_TC, n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(KNOWN_TC, buf, sizeof KNOWN_TC);
}

static void test_build_known_tm(void)
{
    pus_tm_sec_hdr_t s = alive_tm_hdr();
    uint8_t buf[32];
    size_t n = 0;
    TEST_ASSERT_EQUAL(OBSW_OK, pus_tm_build(0x123, 7, &s, NULL, 0, buf, sizeof buf, &n));
    TEST_ASSERT_EQUAL_size_t(sizeof KNOWN_TM, n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(KNOWN_TM, buf, sizeof KNOWN_TM);
}

static void test_crc_residue_is_zero(void)
{
    /* Property of this CRC: running it over data + its own CRC gives 0. */
    TEST_ASSERT_EQUAL_HEX16(0x0000, obsw_crc16(KNOWN_TC, sizeof KNOWN_TC));
    TEST_ASSERT_EQUAL_HEX16(0x0000, obsw_crc16(KNOWN_TM, sizeof KNOWN_TM));
}

static void test_build_rejects_bad_arguments(void)
{
    pus_tc_sec_hdr_t s = ping_tc_hdr();
    pus_tm_sec_hdr_t t = alive_tm_hdr();
    uint8_t buf[64];
    uint8_t data[4] = {1, 2, 3, 4};
    size_t n = 0;

    TEST_ASSERT_EQUAL(OBSW_ERR_NULL, pus_tc_build(1, 0, NULL, NULL, 0, buf, sizeof buf, &n));
    TEST_ASSERT_EQUAL(OBSW_ERR_NULL, pus_tc_build(1, 0, &s, NULL, 4, buf, sizeof buf, &n));
    TEST_ASSERT_EQUAL(OBSW_ERR_NULL, pus_tc_build(1, 0, &s, NULL, 0, NULL, sizeof buf, &n));
    TEST_ASSERT_EQUAL(OBSW_ERR_NULL, pus_tc_build(1, 0, &s, NULL, 0, buf, sizeof buf, NULL));

    TEST_ASSERT_EQUAL(OBSW_ERR_FIELD_RANGE, pus_tc_build(0x800, 0, &s, NULL, 0, buf, sizeof buf, &n));
    TEST_ASSERT_EQUAL(OBSW_ERR_FIELD_RANGE, pus_tc_build(1, 0x4000, &s, NULL, 0, buf, sizeof buf, &n));
    s.ack_flags = 0x10;
    TEST_ASSERT_EQUAL(OBSW_ERR_FIELD_RANGE, pus_tc_build(1, 0, &s, NULL, 0, buf, sizeof buf, &n));
    t.time_ref_status = 0x10;
    TEST_ASSERT_EQUAL(OBSW_ERR_FIELD_RANGE, pus_tm_build(1, 0, &t, NULL, 0, buf, sizeof buf, &n));

    s = ping_tc_hdr();
    TEST_ASSERT_EQUAL(OBSW_ERR_SHORT_BUFFER, pus_tc_build(1, 0, &s, data, 4, buf, 16, &n));
}

static void test_build_rejects_data_too_large_for_ccsds(void)
{
    /* Data field max is 65536 bytes: 5 secondary + 65529 data + 2 CRC fits, one more does not. */
    pus_tc_sec_hdr_t s = ping_tc_hdr();
    uint8_t buf[16];
    uint8_t dummy = 0;
    size_t n = 0;
    TEST_ASSERT_EQUAL(OBSW_ERR_FIELD_RANGE, pus_tc_build(1, 0, &s, &dummy, 65530u, buf, sizeof buf, &n));
    TEST_ASSERT_EQUAL(OBSW_ERR_SHORT_BUFFER, pus_tc_build(1, 0, &s, &dummy, 65529u, buf, sizeof buf, &n));
}

static void test_build_leaves_buffer_untouched_on_error(void)
{
    pus_tc_sec_hdr_t s = ping_tc_hdr();
    uint8_t buf[32];
    uint8_t before[32];
    size_t n = 1234;
    memset(buf, 0xAA, sizeof buf);
    memcpy(before, buf, sizeof buf);
    s.ack_flags = 0xFF;
    TEST_ASSERT_NOT_EQUAL(OBSW_OK, pus_tc_build(1, 0, &s, NULL, 0, buf, sizeof buf, &n));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(before, buf, sizeof buf);
    TEST_ASSERT_EQUAL_size_t(1234, n);
}

/* --- parsing ---------------------------------------------------------- */

static void test_parse_known_tc(void)
{
    pus_tc_t tc;
    TEST_ASSERT_EQUAL(OBSW_OK, pus_tc_parse(KNOWN_TC, sizeof KNOWN_TC, &tc));
    TEST_ASSERT_EQUAL_HEX16(0x123, tc.primary.apid);
    TEST_ASSERT_EQUAL_UINT16(0, tc.primary.seq_count);
    TEST_ASSERT_EQUAL_HEX8(0x9, tc.sec.ack_flags);
    TEST_ASSERT_EQUAL_UINT8(17, tc.sec.service);
    TEST_ASSERT_EQUAL_UINT8(1, tc.sec.subtype);
    TEST_ASSERT_EQUAL_UINT16(1, tc.sec.source_id);
    TEST_ASSERT_EQUAL_size_t(0, tc.data_len);
}

static void test_parse_known_tm(void)
{
    pus_tm_t tm;
    TEST_ASSERT_EQUAL(OBSW_OK, pus_tm_parse(KNOWN_TM, sizeof KNOWN_TM, &tm));
    TEST_ASSERT_EQUAL_HEX16(0x123, tm.primary.apid);
    TEST_ASSERT_EQUAL_UINT16(7, tm.primary.seq_count);
    TEST_ASSERT_EQUAL_UINT8(17, tm.sec.service);
    TEST_ASSERT_EQUAL_UINT8(2, tm.sec.subtype);
    TEST_ASSERT_EQUAL_UINT16(3, tm.sec.msg_counter);
    TEST_ASSERT_EQUAL_UINT16(1, tm.sec.dest_id);
    TEST_ASSERT_EQUAL_HEX32(0x01020304u, tm.sec.time.coarse);
    TEST_ASSERT_EQUAL_HEX16(0x8000u, tm.sec.time.fine);
    TEST_ASSERT_EQUAL_size_t(0, tm.data_len);
}

static void test_round_trip_with_data_points_into_buffer(void)
{
    pus_tc_sec_hdr_t s = ping_tc_hdr();
    const uint8_t data[5] = {0xDE, 0xAD, 0xBE, 0xEF, 0x42};
    uint8_t buf[64];
    size_t n = 0;
    pus_tc_t tc;
    s.service = 8;
    s.subtype = 1;
    TEST_ASSERT_EQUAL(OBSW_OK, pus_tc_build(0x7FF, 0x3FFF, &s, data, sizeof data, buf, sizeof buf, &n));
    TEST_ASSERT_EQUAL_size_t(PUS_TC_OVERHEAD + sizeof data, n);
    TEST_ASSERT_EQUAL(OBSW_OK, pus_tc_parse(buf, n, &tc));
    TEST_ASSERT_EQUAL_HEX16(0x7FF, tc.primary.apid);
    TEST_ASSERT_EQUAL_UINT16(0x3FFF, tc.primary.seq_count);
    TEST_ASSERT_EQUAL_size_t(sizeof data, tc.data_len);
    TEST_ASSERT_EQUAL_PTR(&buf[CCSDS_PRIMARY_HDR_LEN + PUS_TC_SEC_HDR_LEN], tc.data);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(data, tc.data, sizeof data);
}

static void test_every_single_byte_corruption_is_rejected(void)
{
    /* Change each byte in turn: the parser must never accept the result. */
    size_t i;
    for (i = 0; i < sizeof KNOWN_TM; ++i) {
        uint8_t pkt[sizeof KNOWN_TM];
        pus_tm_t tm;
        memcpy(pkt, KNOWN_TM, sizeof pkt);
        pkt[i] ^= 0x5A;
        TEST_ASSERT_NOT_EQUAL_MESSAGE(OBSW_OK, pus_tm_parse(pkt, sizeof pkt, &tm),
                                      "corrupted packet was accepted");
    }
}

static void test_payload_bit_flip_reports_crc(void)
{
    uint8_t pkt[sizeof KNOWN_TC];
    pus_tc_t tc;
    memcpy(pkt, KNOWN_TC, sizeof pkt);
    pkt[8] ^= 0x01; /* subtype 1 -> 0 */
    TEST_ASSERT_EQUAL(OBSW_ERR_CRC, pus_tc_parse(pkt, sizeof pkt, &tc));
}

static void test_parse_rejects_wrong_type(void)
{
    pus_tc_t tc;
    pus_tm_t tm;
    TEST_ASSERT_EQUAL(OBSW_ERR_WRONG_TYPE, pus_tc_parse(KNOWN_TM, sizeof KNOWN_TM, &tc));
    TEST_ASSERT_EQUAL(OBSW_ERR_WRONG_TYPE, pus_tm_parse(KNOWN_TC, sizeof KNOWN_TC, &tm));
}

static void test_parse_rejects_missing_secondary_header(void)
{
    uint8_t pkt[sizeof KNOWN_TC];
    pus_tc_t tc;
    memcpy(pkt, KNOWN_TC, sizeof pkt);
    pkt[0] &= (uint8_t)~0x08u; /* clear secondary header flag */
    fix_crc(pkt, sizeof pkt);
    TEST_ASSERT_EQUAL(OBSW_ERR_NO_SEC_HDR, pus_tc_parse(pkt, sizeof pkt, &tc));
}

static void test_parse_rejects_segmented_packets(void)
{
    uint8_t pkt[sizeof KNOWN_TC];
    pus_tc_t tc;
    memcpy(pkt, KNOWN_TC, sizeof pkt);
    pkt[2] = (uint8_t)(pkt[2] & 0x3Fu); /* sequence flags = continuation segment */
    fix_crc(pkt, sizeof pkt);
    TEST_ASSERT_EQUAL(OBSW_ERR_UNSUPPORTED, pus_tc_parse(pkt, sizeof pkt, &tc));
}

static void test_parse_rejects_wrong_pus_version(void)
{
    uint8_t pkt[sizeof KNOWN_TC];
    pus_tc_t tc;
    memcpy(pkt, KNOWN_TC, sizeof pkt);
    pkt[6] = (uint8_t)((1u << 4) | (pkt[6] & 0x0Fu)); /* PUS-A/B style version 1 */
    fix_crc(pkt, sizeof pkt);
    TEST_ASSERT_EQUAL(OBSW_ERR_BAD_PUS_VERSION, pus_tc_parse(pkt, sizeof pkt, &tc));
}

static void test_parse_rejects_too_short_for_pus(void)
{
    /* A valid 9-byte CCSDS packet with a good CRC is still too short for a PUS TC. */
    uint8_t pkt[9] = {0x19, 0x23, 0xC0, 0x00, 0x00, 0x02, 0x29, 0x00, 0x00};
    /* An 8-byte packet cannot even hold header + CRC + one byte. */
    const uint8_t tiny[8] = {0x19, 0x23, 0xC0, 0x00, 0x00, 0x01, 0x00, 0x00};
    pus_tc_t tc;
    fix_crc(pkt, sizeof pkt);
    TEST_ASSERT_EQUAL(OBSW_ERR_SHORT_BUFFER, pus_tc_parse(pkt, sizeof pkt, &tc));
    TEST_ASSERT_EQUAL(OBSW_ERR_SHORT_BUFFER, pus_tc_parse(tiny, sizeof tiny, &tc));
}

static void test_parse_rejects_truncated_and_null(void)
{
    pus_tc_t tc;
    TEST_ASSERT_EQUAL(OBSW_ERR_LENGTH_MISMATCH, pus_tc_parse(KNOWN_TC, sizeof KNOWN_TC - 1u, &tc));
    TEST_ASSERT_EQUAL(OBSW_ERR_NULL, pus_tc_parse(NULL, sizeof KNOWN_TC, &tc));
    TEST_ASSERT_EQUAL(OBSW_ERR_NULL, pus_tc_parse(KNOWN_TC, sizeof KNOWN_TC, NULL));
}

static void test_parse_leaves_output_untouched_on_error(void)
{
    uint8_t pkt[sizeof KNOWN_TC];
    pus_tc_t tc;
    pus_tc_t before;
    memset(&tc, 0x5C, sizeof tc);
    memcpy(&before, &tc, sizeof tc);
    memcpy(pkt, KNOWN_TC, sizeof pkt);
    pkt[12] ^= 0xFF; /* break the CRC */
    TEST_ASSERT_EQUAL(OBSW_ERR_CRC, pus_tc_parse(pkt, sizeof pkt, &tc));
    TEST_ASSERT_EQUAL_MEMORY(&before, &tc, sizeof tc);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_build_known_tc);
    RUN_TEST(test_build_known_tm);
    RUN_TEST(test_crc_residue_is_zero);
    RUN_TEST(test_build_rejects_bad_arguments);
    RUN_TEST(test_build_rejects_data_too_large_for_ccsds);
    RUN_TEST(test_build_leaves_buffer_untouched_on_error);
    RUN_TEST(test_parse_known_tc);
    RUN_TEST(test_parse_known_tm);
    RUN_TEST(test_round_trip_with_data_points_into_buffer);
    RUN_TEST(test_every_single_byte_corruption_is_rejected);
    RUN_TEST(test_payload_bit_flip_reports_crc);
    RUN_TEST(test_parse_rejects_wrong_type);
    RUN_TEST(test_parse_rejects_missing_secondary_header);
    RUN_TEST(test_parse_rejects_segmented_packets);
    RUN_TEST(test_parse_rejects_wrong_pus_version);
    RUN_TEST(test_parse_rejects_too_short_for_pus);
    RUN_TEST(test_parse_rejects_truncated_and_null);
    RUN_TEST(test_parse_leaves_output_untouched_on_error);
    return UNITY_END();
}
