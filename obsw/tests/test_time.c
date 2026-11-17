/* ST[9] time reports. */
#include <string.h>

#include "unity.h"
#include "tinyobsw/obsw.h"

#define OUR_APID  0x064u
#define MAX_TM    128u

typedef struct {
    uint8_t pkt[MAX_TM][OBSW_TM_MAX_LEN];
    size_t  len[MAX_TM];
    size_t  count;
} capture_t;

static capture_t cap;
static obsw_t    obsw;
static uint32_t  now_s;
static uint16_t  now_fine;

static void capture_sink(void *ctx, const uint8_t *pkt, size_t len)
{
    capture_t *c = (capture_t *)ctx;
    TEST_ASSERT_TRUE(c->count < MAX_TM);
    memcpy(c->pkt[c->count], pkt, len);
    c->len[c->count] = len;
    c->count++;
}

static pus_cuc_time_t fake_clock(void *ctx)
{
    pus_cuc_time_t t;
    (void)ctx;
    t.coarse = now_s;
    t.fine   = now_fine;
    return t;
}

void setUp(void)
{
    obsw_config_t cfg;
    memset(&cap, 0, sizeof cap);
    now_s = 500u;
    now_fine = 0u;
    cfg.apid     = OUR_APID;
    cfg.tm_sink  = capture_sink;
    cfg.tm_ctx   = &cap;
    cfg.time_now = fake_clock;
    cfg.time_ctx = NULL;
    TEST_ASSERT_EQUAL(OBSW_OK, obsw_init(&obsw, &cfg));
    obsw.hk_enabled = 0; /* only time reports in these tests */
    obsw_tick(&obsw);
}

void tearDown(void) {}

static void run_seconds(unsigned n)
{
    while (n-- > 0u) {
        now_s++;
        obsw_tick(&obsw);
    }
}

static pus_tm_t tm_at(size_t i)
{
    pus_tm_t tm;
    TEST_ASSERT_EQUAL(OBSW_OK, pus_tm_parse(cap.pkt[i], cap.len[i], &tm));
    return tm;
}

static obsw_status_t set_rate(uint8_t exp)
{
    pus_tc_sec_hdr_t s;
    uint8_t buf[32];
    size_t n = 0;
    s.ack_flags = 0;
    s.service   = 9;
    s.subtype   = 1;
    s.source_id = 10;
    TEST_ASSERT_EQUAL(OBSW_OK, pus_tc_build(OUR_APID, 0, &s, &exp, 1, buf, sizeof buf, &n));
    return obsw_handle_tc(&obsw, buf, n);
}

static void test_time_report_every_second_by_default(void)
{
    size_t i;
    run_seconds(5);
    TEST_ASSERT_EQUAL_size_t(5, cap.count);
    for (i = 0; i < cap.count; ++i) {
        pus_tm_t tm = tm_at(i);
        TEST_ASSERT_EQUAL_UINT8(9, tm.sec.service);
        TEST_ASSERT_EQUAL_UINT8(2, tm.sec.subtype);
        TEST_ASSERT_EQUAL_size_t(7, tm.data_len);
        TEST_ASSERT_EQUAL_UINT8(0, tm.data[0]);
    }
}

static void test_report_carries_the_sampled_time(void)
{
    pus_tm_t tm;
    uint32_t coarse;
    now_fine = 0x4000u; /* 0.25 s */
    run_seconds(1);
    tm = tm_at(0);
    coarse = ((uint32_t)tm.data[1] << 24) | ((uint32_t)tm.data[2] << 16) |
             ((uint32_t)tm.data[3] << 8) | tm.data[4];
    TEST_ASSERT_EQUAL_UINT32(501u, coarse);
    TEST_ASSERT_EQUAL_UINT16(0x4000u, (uint16_t)((tm.data[5] << 8) | tm.data[6]));
    /* and it matches the packet's own time stamp */
    TEST_ASSERT_EQUAL_UINT32(coarse, tm.sec.time.coarse);
}

static void test_rate_can_be_slowed_down(void)
{
    TEST_ASSERT_EQUAL(OBSW_OK, set_rate(2)); /* every 4 s */
    run_seconds(16);
    TEST_ASSERT_EQUAL_size_t(4, cap.count);
    TEST_ASSERT_EQUAL_UINT8(2, tm_at(0).data[0]);
}

static void test_rate_out_of_range_rejected(void)
{
    TEST_ASSERT_EQUAL(OBSW_ERR_BAD_APP_DATA, set_rate(OBSW_TIME_RATE_MAX + 1u));
    TEST_ASSERT_EQUAL_UINT8(OBSW_TIME_RATE_DEFAULT, obsw.time_rate_exp);
    TEST_ASSERT_EQUAL(OBSW_OK, set_rate(OBSW_TIME_RATE_MAX));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_time_report_every_second_by_default);
    RUN_TEST(test_report_carries_the_sampled_time);
    RUN_TEST(test_rate_can_be_slowed_down);
    RUN_TEST(test_rate_out_of_range_rejected);
    return UNITY_END();
}
