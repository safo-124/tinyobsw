#include <string.h>

#include "unity.h"
#include "tinyobsw/ccsds.h"

void setUp(void) {}
void tearDown(void) {}

/* TC, secondary header present, APID 0x123, unsegmented, count 5, 11 data bytes. */
static const uint8_t KNOWN_BYTES[6] = {0x19, 0x23, 0xC0, 0x05, 0x00, 0x0A};

static ccsds_primary_hdr_t known_hdr(void)
{
    ccsds_primary_hdr_t h;
    h.version      = 0;
    h.type         = CCSDS_TYPE_TC;
    h.sec_hdr_flag = true;
    h.apid         = 0x123;
    h.seq_flags    = CCSDS_SEQ_UNSEGMENTED;
    h.seq_count    = 5;
    h.data_len     = 10;
    return h;
}

static void test_encode_known_header(void)
{
    ccsds_primary_hdr_t h = known_hdr();
    uint8_t buf[6];
    TEST_ASSERT_EQUAL(OBSW_OK, ccsds_encode_primary(&h, buf, sizeof buf));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(KNOWN_BYTES, buf, 6);
}

static void test_decode_known_header(void)
{
    ccsds_primary_hdr_t h;
    TEST_ASSERT_EQUAL(OBSW_OK, ccsds_decode_primary(KNOWN_BYTES, 6, &h));
    TEST_ASSERT_EQUAL_UINT8(0, h.version);
    TEST_ASSERT_EQUAL(CCSDS_TYPE_TC, h.type);
    TEST_ASSERT_TRUE(h.sec_hdr_flag);
    TEST_ASSERT_EQUAL_HEX16(0x123, h.apid);
    TEST_ASSERT_EQUAL(CCSDS_SEQ_UNSEGMENTED, h.seq_flags);
    TEST_ASSERT_EQUAL_UINT16(5, h.seq_count);
    TEST_ASSERT_EQUAL_UINT16(10, h.data_len);
}

static void test_round_trip_every_apid(void)
{
    unsigned apid;
    for (apid = 0; apid <= CCSDS_APID_MAX; ++apid) {
        ccsds_primary_hdr_t in = known_hdr();
        ccsds_primary_hdr_t out;
        uint8_t buf[6];
        in.apid = (uint16_t)apid;
        in.type = (apid & 1u) ? CCSDS_TYPE_TC : CCSDS_TYPE_TM;
        in.sec_hdr_flag = (apid & 2u) != 0u;
        in.seq_count = (uint16_t)((apid * 7u) & CCSDS_SEQ_COUNT_MAX);
        TEST_ASSERT_EQUAL(OBSW_OK, ccsds_encode_primary(&in, buf, sizeof buf));
        TEST_ASSERT_EQUAL(OBSW_OK, ccsds_decode_primary(buf, sizeof buf, &out));
        TEST_ASSERT_EQUAL_HEX16(in.apid, out.apid);
        TEST_ASSERT_EQUAL(in.type, out.type);
        TEST_ASSERT_EQUAL(in.sec_hdr_flag, out.sec_hdr_flag);
        TEST_ASSERT_EQUAL_UINT16(in.seq_count, out.seq_count);
    }
}

static void test_extreme_values_round_trip(void)
{
    ccsds_primary_hdr_t in = known_hdr();
    ccsds_primary_hdr_t out;
    uint8_t buf[6];
    in.apid      = CCSDS_APID_MAX;
    in.seq_count = CCSDS_SEQ_COUNT_MAX;
    in.data_len  = 0xFFFF;
    in.seq_flags = CCSDS_SEQ_CONTINUATION;
    TEST_ASSERT_EQUAL(OBSW_OK, ccsds_encode_primary(&in, buf, sizeof buf));
    TEST_ASSERT_EQUAL(OBSW_OK, ccsds_decode_primary(buf, sizeof buf, &out));
    TEST_ASSERT_EQUAL_HEX16(CCSDS_APID_MAX, out.apid);
    TEST_ASSERT_EQUAL_UINT16(CCSDS_SEQ_COUNT_MAX, out.seq_count);
    TEST_ASSERT_EQUAL_UINT16(0xFFFF, out.data_len);
    TEST_ASSERT_EQUAL(CCSDS_SEQ_CONTINUATION, out.seq_flags);
}

static void test_encode_rejects_out_of_range_fields(void)
{
    uint8_t buf[6];
    ccsds_primary_hdr_t h;

    h = known_hdr(); h.apid = CCSDS_APID_MAX + 1u;
    TEST_ASSERT_EQUAL(OBSW_ERR_FIELD_RANGE, ccsds_encode_primary(&h, buf, sizeof buf));

    h = known_hdr(); h.seq_count = CCSDS_SEQ_COUNT_MAX + 1u;
    TEST_ASSERT_EQUAL(OBSW_ERR_FIELD_RANGE, ccsds_encode_primary(&h, buf, sizeof buf));

    h = known_hdr(); h.version = 1;
    TEST_ASSERT_EQUAL(OBSW_ERR_FIELD_RANGE, ccsds_encode_primary(&h, buf, sizeof buf));
}

static void test_encode_does_not_write_on_error(void)
{
    uint8_t buf[6];
    uint8_t before[6];
    ccsds_primary_hdr_t h = known_hdr();
    h.apid = 0xFFFF;
    memset(buf, 0xAA, sizeof buf);
    memcpy(before, buf, sizeof buf);
    (void)ccsds_encode_primary(&h, buf, sizeof buf);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(before, buf, 6);
}

static void test_short_buffers_rejected(void)
{
    uint8_t buf[5];
    ccsds_primary_hdr_t h = known_hdr();
    TEST_ASSERT_EQUAL(OBSW_ERR_SHORT_BUFFER, ccsds_encode_primary(&h, buf, sizeof buf));
    TEST_ASSERT_EQUAL(OBSW_ERR_SHORT_BUFFER, ccsds_decode_primary(KNOWN_BYTES, 5, &h));
    TEST_ASSERT_EQUAL(OBSW_ERR_SHORT_BUFFER, ccsds_decode_primary(KNOWN_BYTES, 0, &h));
}

static void test_null_pointers_rejected(void)
{
    uint8_t buf[6];
    ccsds_primary_hdr_t h = known_hdr();
    TEST_ASSERT_EQUAL(OBSW_ERR_NULL, ccsds_encode_primary(NULL, buf, sizeof buf));
    TEST_ASSERT_EQUAL(OBSW_ERR_NULL, ccsds_encode_primary(&h, NULL, 6));
    TEST_ASSERT_EQUAL(OBSW_ERR_NULL, ccsds_decode_primary(NULL, 6, &h));
    TEST_ASSERT_EQUAL(OBSW_ERR_NULL, ccsds_decode_primary(KNOWN_BYTES, 6, NULL));
}

static void test_decode_rejects_nonzero_version(void)
{
    uint8_t bad[6];
    ccsds_primary_hdr_t h;
    memcpy(bad, KNOWN_BYTES, sizeof bad);
    bad[0] |= 0x20; /* version = 1 */
    TEST_ASSERT_EQUAL(OBSW_ERR_BAD_VERSION, ccsds_decode_primary(bad, sizeof bad, &h));
}

static void test_packet_len_and_check(void)
{
    uint8_t pkt[17];
    ccsds_primary_hdr_t h = known_hdr(); /* data_len 10 -> 6 + 11 = 17 bytes */
    TEST_ASSERT_EQUAL_size_t(17, ccsds_packet_len(&h));

    memset(pkt, 0, sizeof pkt);
    TEST_ASSERT_EQUAL(OBSW_OK, ccsds_encode_primary(&h, pkt, sizeof pkt));
    TEST_ASSERT_EQUAL(OBSW_OK, ccsds_check_packet(pkt, 17, &h));
    TEST_ASSERT_EQUAL(OBSW_ERR_LENGTH_MISMATCH, ccsds_check_packet(pkt, 16, &h));
    TEST_ASSERT_EQUAL(OBSW_ERR_LENGTH_MISMATCH, ccsds_check_packet(pkt, 18, &h));
}

static void test_seq_count_wraps(void)
{
    TEST_ASSERT_EQUAL_UINT16(1, ccsds_next_seq_count(0));
    TEST_ASSERT_EQUAL_UINT16(CCSDS_SEQ_COUNT_MAX, ccsds_next_seq_count(CCSDS_SEQ_COUNT_MAX - 1u));
    TEST_ASSERT_EQUAL_UINT16(0, ccsds_next_seq_count(CCSDS_SEQ_COUNT_MAX));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_encode_known_header);
    RUN_TEST(test_decode_known_header);
    RUN_TEST(test_round_trip_every_apid);
    RUN_TEST(test_extreme_values_round_trip);
    RUN_TEST(test_encode_rejects_out_of_range_fields);
    RUN_TEST(test_encode_does_not_write_on_error);
    RUN_TEST(test_short_buffers_rejected);
    RUN_TEST(test_null_pointers_rejected);
    RUN_TEST(test_decode_rejects_nonzero_version);
    RUN_TEST(test_packet_len_and_check);
    RUN_TEST(test_seq_count_wraps);
    return UNITY_END();
}
