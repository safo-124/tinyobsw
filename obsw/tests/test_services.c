#include <string.h>

#include "unity.h"
#include "tinyobsw/obsw.h"

#define OUR_APID   0x064u
#define GROUND_ID  0x00Au
#define MAX_TM     16u

/* ---- test harness: capture every TM the core emits ------------------ */

typedef struct {
    uint8_t pkt[MAX_TM][OBSW_TM_MAX_LEN];
    size_t  len[MAX_TM];
    size_t  count;
} capture_t;

static capture_t cap;
static obsw_t    obsw;

static void capture_sink(void *ctx, const uint8_t *pkt, size_t len)
{
    capture_t *c = (capture_t *)ctx;
    TEST_ASSERT_TRUE_MESSAGE(c->count < MAX_TM, "too many TM packets");
    TEST_ASSERT_TRUE(len <= OBSW_TM_MAX_LEN);
    memcpy(c->pkt[c->count], pkt, len);
    c->len[c->count] = len;
    c->count++;
}

static pus_cuc_time_t fake_clock(void *ctx)
{
    pus_cuc_time_t t;
    (void)ctx;
    t.coarse = 1000u;
    t.fine   = 0x4000u; /* 1000.25 s */
    return t;
}

void setUp(void)
{
    obsw_config_t cfg;
    memset(&cap, 0, sizeof cap);
    cfg.apid     = OUR_APID;
    cfg.tm_sink  = capture_sink;
    cfg.tm_ctx   = &cap;
    cfg.time_now = fake_clock;
    cfg.time_ctx = NULL;
    TEST_ASSERT_EQUAL(OBSW_OK, obsw_init(&obsw, &cfg));
}

void tearDown(void) {}

/* Build a TC from ground to us. Returns its length. */
static size_t make_tc(uint8_t service, uint8_t subtype, uint8_t ack,
                      const uint8_t *data, size_t data_len,
                      uint16_t seq, uint8_t *buf, size_t buf_len)
{
    pus_tc_sec_hdr_t s;
    size_t n = 0;
    s.ack_flags = ack;
    s.service   = service;
    s.subtype   = subtype;
    s.source_id = GROUND_ID;
    TEST_ASSERT_EQUAL(OBSW_OK, pus_tc_build(OUR_APID, seq, &s, data, data_len, buf, buf_len, &n));
    return n;
}

/* Parse captured TM number i; it must be a valid PUS-C packet from us. */
static pus_tm_t tm_at(size_t i)
{
    pus_tm_t tm;
    TEST_ASSERT_TRUE_MESSAGE(i < cap.count, "expected more TM packets");
    TEST_ASSERT_EQUAL(OBSW_OK, pus_tm_parse(cap.pkt[i], cap.len[i], &tm));
    TEST_ASSERT_EQUAL_HEX16(OUR_APID, tm.primary.apid);
    return tm;
}

static void assert_tm_type(size_t i, uint8_t service, uint8_t subtype)
{
    pus_tm_t tm = tm_at(i);
    TEST_ASSERT_EQUAL_UINT8(service, tm.sec.service);
    TEST_ASSERT_EQUAL_UINT8(subtype, tm.sec.subtype);
}

/* TM i must be ST[1,subtype] quoting the request ID of tc and addressed to dest. */
static void assert_verification(size_t i, uint8_t subtype, const uint8_t *tc, uint16_t dest)
{
    pus_tm_t tm = tm_at(i);
    TEST_ASSERT_EQUAL_UINT8(1, tm.sec.service);
    TEST_ASSERT_EQUAL_UINT8(subtype, tm.sec.subtype);
    TEST_ASSERT_EQUAL_UINT16(dest, tm.sec.dest_id);
    TEST_ASSERT_TRUE(tm.data_len >= 4u);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(tc, tm.data, 4); /* request ID = first 4 TC bytes */
}

static void assert_failure_code(size_t i, obsw_status_t code)
{
    pus_tm_t tm = tm_at(i);
    TEST_ASSERT_EQUAL_size_t(6, tm.data_len);
    TEST_ASSERT_EQUAL_UINT16((uint16_t)code, (uint16_t)((tm.data[4] << 8) | tm.data[5]));
}

/* ---- init ----------------------------------------------------------- */

static void test_init_rejects_bad_config(void)
{
    obsw_t o;
    obsw_config_t cfg;
    memset(&cfg, 0, sizeof cfg);
    TEST_ASSERT_EQUAL(OBSW_ERR_NULL, obsw_init(NULL, &cfg));
    TEST_ASSERT_EQUAL(OBSW_ERR_NULL, obsw_init(&o, NULL));
    cfg.apid = CCSDS_APID_IDLE;
    TEST_ASSERT_EQUAL(OBSW_ERR_FIELD_RANGE, obsw_init(&o, &cfg));
}

/* ---- ST[17] + ST[1] success paths ------------------------------------ */

static void test_ping_with_all_acks_gives_full_report_chain(void)
{
    uint8_t tc[32];
    size_t n = make_tc(17, 1, PUS_ACK_ALL, NULL, 0, 42, tc, sizeof tc);

    TEST_ASSERT_EQUAL(OBSW_OK, obsw_handle_tc(&obsw, tc, n));
    TEST_ASSERT_EQUAL_size_t(4, cap.count);
    assert_verification(0, 1, tc, GROUND_ID);  /* accepted */
    assert_verification(1, 3, tc, GROUND_ID);  /* started */
    assert_tm_type(2, 17, 2);                  /* alive */
    assert_verification(3, 7, tc, GROUND_ID);  /* completed */
    TEST_ASSERT_EQUAL_UINT16(GROUND_ID, tm_at(2).sec.dest_id);
}

static void test_ping_without_acks_only_answers(void)
{
    uint8_t tc[32];
    size_t n = make_tc(17, 1, 0, NULL, 0, 0, tc, sizeof tc);
    TEST_ASSERT_EQUAL(OBSW_OK, obsw_handle_tc(&obsw, tc, n));
    TEST_ASSERT_EQUAL_size_t(1, cap.count);
    assert_tm_type(0, 17, 2);
}

static void test_ack_flags_select_reports(void)
{
    uint8_t tc[32];
    size_t n = make_tc(17, 1, PUS_ACK_ACCEPTANCE | PUS_ACK_COMPLETION, NULL, 0, 0, tc, sizeof tc);
    TEST_ASSERT_EQUAL(OBSW_OK, obsw_handle_tc(&obsw, tc, n));
    TEST_ASSERT_EQUAL_size_t(3, cap.count);
    assert_verification(0, 1, tc, GROUND_ID);
    assert_tm_type(1, 17, 2);
    assert_verification(2, 7, tc, GROUND_ID);
}

static void test_tm_sequence_counters_and_time(void)
{
    uint8_t tc[32];
    size_t n = make_tc(17, 1, PUS_ACK_ALL, NULL, 0, 0, tc, sizeof tc);
    size_t i;

    TEST_ASSERT_EQUAL(OBSW_OK, obsw_handle_tc(&obsw, tc, n));
    TEST_ASSERT_EQUAL(OBSW_OK, obsw_handle_tc(&obsw, tc, n));
    TEST_ASSERT_EQUAL_size_t(8, cap.count);

    for (i = 0; i < cap.count; ++i) {
        pus_tm_t tm = tm_at(i);
        /* one CCSDS sequence count across all our TM */
        TEST_ASSERT_EQUAL_UINT16(i, tm.primary.seq_count);
        TEST_ASSERT_EQUAL_UINT32(1000u, tm.sec.time.coarse);
        TEST_ASSERT_EQUAL_UINT16(0x4000u, tm.sec.time.fine);
    }
    /* message type counters run per service: ST[17] got 0 then 1 */
    TEST_ASSERT_EQUAL_UINT16(0, tm_at(2).sec.msg_counter);
    TEST_ASSERT_EQUAL_UINT16(1, tm_at(6).sec.msg_counter);
    /* ST[1] reports: 3 per ping, so the 2nd ping's first report is number 3 */
    TEST_ASSERT_EQUAL_UINT16(3, tm_at(4).sec.msg_counter);
}

/* ---- acceptance failures -> TM[1,2] ---------------------------------- */

static void test_bad_crc_rejected_with_failure_report(void)
{
    uint8_t tc[32];
    size_t n = make_tc(17, 1, PUS_ACK_ALL, NULL, 0, 0, tc, sizeof tc);
    tc[n - 1] ^= 0xFF;

    TEST_ASSERT_EQUAL(OBSW_ERR_CRC, obsw_handle_tc(&obsw, tc, n));
    TEST_ASSERT_EQUAL_size_t(1, cap.count);
    /* secondary header cannot be trusted, so the reply goes to ID 0 */
    assert_verification(0, 2, tc, 0);
    assert_failure_code(0, OBSW_ERR_CRC);
}

static void test_unknown_service_and_subtype_rejected(void)
{
    uint8_t tc[32];
    size_t n = make_tc(99, 1, PUS_ACK_ALL, NULL, 0, 0, tc, sizeof tc);
    TEST_ASSERT_EQUAL(OBSW_ERR_ILLEGAL_SERVICE, obsw_handle_tc(&obsw, tc, n));
    TEST_ASSERT_EQUAL_size_t(1, cap.count);
    assert_verification(0, 2, tc, GROUND_ID);
    assert_failure_code(0, OBSW_ERR_ILLEGAL_SERVICE);

    cap.count = 0;
    n = make_tc(17, 9, PUS_ACK_ALL, NULL, 0, 1, tc, sizeof tc);
    TEST_ASSERT_EQUAL(OBSW_ERR_ILLEGAL_SUBTYPE, obsw_handle_tc(&obsw, tc, n));
    TEST_ASSERT_EQUAL_size_t(1, cap.count);
    assert_failure_code(0, OBSW_ERR_ILLEGAL_SUBTYPE);
}

static void test_ping_with_unexpected_data_rejected(void)
{
    uint8_t tc[32];
    const uint8_t junk[2] = {0xAB, 0xCD};
    size_t n = make_tc(17, 1, 0, junk, sizeof junk, 0, tc, sizeof tc);
    TEST_ASSERT_EQUAL(OBSW_ERR_BAD_APP_DATA, obsw_handle_tc(&obsw, tc, n));
    /* failure reports are sent even though no ack flags were set */
    TEST_ASSERT_EQUAL_size_t(1, cap.count);
    assert_verification(0, 2, tc, GROUND_ID);
    assert_failure_code(0, OBSW_ERR_BAD_APP_DATA);
}

static void test_truncated_tc_rejected(void)
{
    uint8_t tc[32];
    size_t n = make_tc(17, 1, PUS_ACK_ALL, NULL, 0, 0, tc, sizeof tc);
    TEST_ASSERT_EQUAL(OBSW_ERR_LENGTH_MISMATCH, obsw_handle_tc(&obsw, tc, n - 1));
    TEST_ASSERT_EQUAL_size_t(1, cap.count);
    assert_failure_code(0, OBSW_ERR_LENGTH_MISMATCH);
}

/* ---- dropped without any report ---------------------------------------- */

static void test_other_apid_is_ignored(void)
{
    uint8_t tc[32];
    size_t n = make_tc(17, 1, PUS_ACK_ALL, NULL, 0, 0, tc, sizeof tc);
    obsw.cfg.apid = OUR_APID + 1u; /* pretend we are another application */
    TEST_ASSERT_EQUAL(OBSW_ERR_WRONG_APID, obsw_handle_tc(&obsw, tc, n));
    TEST_ASSERT_EQUAL_size_t(0, cap.count);
}

static void test_unreadable_header_is_dropped(void)
{
    const uint8_t garbage[3] = {0x18, 0x64, 0xC0};
    uint8_t tc[32];
    size_t n = make_tc(17, 1, PUS_ACK_ALL, NULL, 0, 0, tc, sizeof tc);

    TEST_ASSERT_EQUAL(OBSW_ERR_SHORT_BUFFER, obsw_handle_tc(&obsw, garbage, sizeof garbage));
    tc[0] |= 0xE0; /* CCSDS version 7 */
    TEST_ASSERT_EQUAL(OBSW_ERR_BAD_VERSION, obsw_handle_tc(&obsw, tc, n));
    TEST_ASSERT_EQUAL(OBSW_ERR_NULL, obsw_handle_tc(&obsw, NULL, 10));
    TEST_ASSERT_EQUAL(OBSW_ERR_NULL, obsw_handle_tc(NULL, tc, n));
    TEST_ASSERT_EQUAL_size_t(0, cap.count);
}

static void test_runs_without_sink_or_clock(void)
{
    obsw_t o;
    obsw_config_t cfg;
    uint8_t tc[32];
    size_t n = make_tc(17, 1, PUS_ACK_ALL, NULL, 0, 0, tc, sizeof tc);
    memset(&cfg, 0, sizeof cfg);
    cfg.apid = OUR_APID;
    TEST_ASSERT_EQUAL(OBSW_OK, obsw_init(&o, &cfg));
    TEST_ASSERT_EQUAL(OBSW_OK, obsw_handle_tc(&o, tc, n));
    TEST_ASSERT_EQUAL_UINT16(4, o.tm_seq_count); /* TM was still generated */
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_init_rejects_bad_config);
    RUN_TEST(test_ping_with_all_acks_gives_full_report_chain);
    RUN_TEST(test_ping_without_acks_only_answers);
    RUN_TEST(test_ack_flags_select_reports);
    RUN_TEST(test_tm_sequence_counters_and_time);
    RUN_TEST(test_bad_crc_rejected_with_failure_report);
    RUN_TEST(test_unknown_service_and_subtype_rejected);
    RUN_TEST(test_ping_with_unexpected_data_rejected);
    RUN_TEST(test_truncated_tc_rejected);
    RUN_TEST(test_other_apid_is_ignored);
    RUN_TEST(test_unreadable_header_is_dropped);
    RUN_TEST(test_runs_without_sink_or_clock);
    return UNITY_END();
}
