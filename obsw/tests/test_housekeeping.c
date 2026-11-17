/* ST[3] housekeeping, ST[8] mode commands and the 1-second scheduler. */
#include <string.h>

#include "unity.h"
#include "tinyobsw/obsw.h"

#define OUR_APID  0x064u
#define GROUND_ID 0x00Au
#define MAX_TM    64u

typedef struct {
    uint8_t pkt[MAX_TM][OBSW_TM_MAX_LEN];
    size_t  len[MAX_TM];
    size_t  count;
} capture_t;

static capture_t cap;
static obsw_t    obsw;
static uint32_t  now_s;   /* the fake on-board clock */
static uint16_t  tc_seq;

static void capture_sink(void *ctx, const uint8_t *pkt, size_t len)
{
    capture_t *c = (capture_t *)ctx;
    TEST_ASSERT_TRUE_MESSAGE(c->count < MAX_TM, "too many TM packets");
    memcpy(c->pkt[c->count], pkt, len);
    c->len[c->count] = len;
    c->count++;
}

static pus_cuc_time_t fake_clock(void *ctx)
{
    pus_cuc_time_t t;
    (void)ctx;
    t.coarse = now_s;
    t.fine   = 0u;
    return t;
}

void setUp(void)
{
    obsw_config_t cfg;
    memset(&cap, 0, sizeof cap);
    now_s  = 100u;
    tc_seq = 0u;
    cfg.apid     = OUR_APID;
    cfg.tm_sink  = capture_sink;
    cfg.tm_ctx   = &cap;
    cfg.time_now = fake_clock;
    cfg.time_ctx = NULL;
    TEST_ASSERT_EQUAL(OBSW_OK, obsw_init(&obsw, &cfg));
    obsw_tick(&obsw); /* starts the scheduler at t = 100 s */
}

void tearDown(void) {}

/* Move the clock forward one second at a time, ticking like the simulator does. */
static void run_seconds(unsigned n)
{
    while (n-- > 0u) {
        now_s++;
        obsw_tick(&obsw);
        obsw_tick(&obsw); /* extra ticks within a second must do nothing */
    }
}

static obsw_status_t send_tc(uint8_t service, uint8_t subtype, uint8_t ack,
                             const uint8_t *data, size_t data_len)
{
    pus_tc_sec_hdr_t s;
    uint8_t buf[64];
    size_t n = 0;
    s.ack_flags = ack;
    s.service   = service;
    s.subtype   = subtype;
    s.source_id = GROUND_ID;
    TEST_ASSERT_EQUAL(OBSW_OK, pus_tc_build(OUR_APID, tc_seq++, &s, data, data_len,
                                            buf, sizeof buf, &n));
    return obsw_handle_tc(&obsw, buf, n);
}

static obsw_status_t set_mode(instr_mode_t mode, uint8_t ack)
{
    const uint8_t data[3] = {0x00, 0x01, (uint8_t)mode}; /* function 1, mode */
    return send_tc(8, 1, ack, data, sizeof data);
}

static pus_tm_t tm_at(size_t i)
{
    pus_tm_t tm;
    TEST_ASSERT_TRUE_MESSAGE(i < cap.count, "expected more TM packets");
    TEST_ASSERT_EQUAL(OBSW_OK, pus_tm_parse(cap.pkt[i], cap.len[i], &tm));
    return tm;
}

static size_t count_tm(uint8_t service, uint8_t subtype)
{
    size_t i;
    size_t n = 0;
    for (i = 0; i < cap.count; ++i) {
        pus_tm_t tm = tm_at(i);
        if (tm.sec.service == service && tm.sec.subtype == subtype) {
            n++;
        }
    }
    return n;
}

static void assert_tm(size_t i, uint8_t service, uint8_t subtype)
{
    pus_tm_t tm = tm_at(i);
    TEST_ASSERT_EQUAL_UINT8(service, tm.sec.service);
    TEST_ASSERT_EQUAL_UINT8(subtype, tm.sec.subtype);
}

static uint16_t failure_code(size_t i)
{
    pus_tm_t tm = tm_at(i);
    TEST_ASSERT_EQUAL_size_t(6, tm.data_len);
    return (uint16_t)((tm.data[4] << 8) | tm.data[5]);
}

typedef struct {
    uint8_t  sid;
    uint8_t  mode;
    int16_t  temp_cdeg;
    uint16_t current_ma;
    uint16_t voltage_mv;
    uint32_t science;
    uint16_t mode_changes;
    uint16_t tc_accepted;
    uint16_t tc_rejected;
} hk_t;

static uint16_t be16(const uint8_t *p) { return (uint16_t)((p[0] << 8) | p[1]); }

static hk_t hk_at(size_t i)
{
    hk_t h;
    pus_tm_t tm = tm_at(i);
    TEST_ASSERT_EQUAL_UINT8(3, tm.sec.service);
    TEST_ASSERT_EQUAL_UINT8(25, tm.sec.subtype);
    TEST_ASSERT_EQUAL_size_t(24, tm.data_len);
    h.sid          = tm.data[0];
    h.mode         = tm.data[1];
    h.temp_cdeg    = (int16_t)be16(&tm.data[2]);
    h.current_ma   = be16(&tm.data[4]);
    h.voltage_mv   = be16(&tm.data[6]);
    h.science      = ((uint32_t)be16(&tm.data[8]) << 16) | be16(&tm.data[10]);
    h.mode_changes = be16(&tm.data[12]);
    h.tc_accepted  = be16(&tm.data[14]);
    h.tc_rejected  = be16(&tm.data[16]);
    return h;
}

static hk_t last_hk(void)
{
    size_t i = cap.count;
    while (i-- > 0u) {
        pus_tm_t tm = tm_at(i);
        if (tm.sec.service == 3 && tm.sec.subtype == 25) {
            return hk_at(i);
        }
    }
    TEST_FAIL_MESSAGE("no housekeeping report captured");
    return hk_at(0);
}

/* ---- periodic housekeeping ------------------------------------------- */

static void test_periodic_hk_once_per_second(void)
{
    hk_t h;
    TEST_ASSERT_EQUAL_size_t(0, cap.count); /* nothing at start-up */
    run_seconds(1);
    TEST_ASSERT_EQUAL_size_t(1, cap.count);
    h = hk_at(0);
    TEST_ASSERT_EQUAL_UINT8(1, h.sid);
    TEST_ASSERT_EQUAL_UINT8(INSTR_OFF, h.mode);
    TEST_ASSERT_INT_WITHIN(3, 2000, h.temp_cdeg);
    TEST_ASSERT_EQUAL_UINT16(0, h.current_ma);
    TEST_ASSERT_UINT_WITHIN(20, 28000, h.voltage_mv);
    TEST_ASSERT_EQUAL_UINT16(0, tm_at(0).sec.dest_id);
    TEST_ASSERT_EQUAL_UINT32(101, tm_at(0).sec.time.coarse);

    run_seconds(3);
    TEST_ASSERT_EQUAL_size_t(4, count_tm(3, 25));
}

static void test_long_stall_is_capped(void)
{
    now_s += 100u;
    obsw_tick(&obsw);
    TEST_ASSERT_EQUAL_size_t(OBSW_MAX_CATCHUP, count_tm(3, 25));
    run_seconds(1);
    TEST_ASSERT_EQUAL_size_t(OBSW_MAX_CATCHUP + 1u, count_tm(3, 25));
}

static void test_clock_going_backwards_resyncs(void)
{
    now_s -= 50u;
    obsw_tick(&obsw);
    TEST_ASSERT_EQUAL_size_t(0, cap.count);
    run_seconds(1);
    TEST_ASSERT_EQUAL_size_t(1, count_tm(3, 25));
}

static void test_tick_without_clock_does_nothing(void)
{
    obsw.cfg.time_now = NULL;
    obsw_tick(&obsw);
    obsw_tick(NULL);
    TEST_ASSERT_EQUAL_size_t(0, cap.count);
}

static void test_disable_and_enable(void)
{
    const uint8_t sid1[2] = {1, 1};
    TEST_ASSERT_EQUAL(OBSW_OK, send_tc(3, 6, 0, sid1, sizeof sid1));
    run_seconds(5);
    TEST_ASSERT_EQUAL_size_t(0, count_tm(3, 25));
    TEST_ASSERT_EQUAL(OBSW_OK, send_tc(3, 5, 0, sid1, sizeof sid1));
    run_seconds(2);
    TEST_ASSERT_EQUAL_size_t(2, count_tm(3, 25));
}

static void test_change_interval(void)
{
    const uint8_t every5[4] = {1, 1, 0x00, 0x05}; /* N=1, SID 1, 5 s */
    TEST_ASSERT_EQUAL(OBSW_OK, send_tc(3, 31, 0, every5, sizeof every5));
    run_seconds(10);
    TEST_ASSERT_EQUAL_size_t(2, count_tm(3, 25));
    TEST_ASSERT_EQUAL_UINT16(5, obsw.hk_interval_s);
}

static void test_one_shot_goes_to_requester(void)
{
    const uint8_t sid1[2] = {1, 1};
    TEST_ASSERT_EQUAL(OBSW_OK, send_tc(3, 27, 0, sid1, sizeof sid1));
    TEST_ASSERT_EQUAL_size_t(1, cap.count);
    hk_at(0);
    TEST_ASSERT_EQUAL_UINT16(GROUND_ID, tm_at(0).sec.dest_id);
}

static void test_hk_tc_argument_errors(void)
{
    const uint8_t sid2[2]      = {1, 2};          /* SID 2 does not exist */
    const uint8_t n_wrong[3]   = {1, 1, 1};       /* says 1 SID, carries 2 */
    const uint8_t n_zero[2]    = {0, 1};
    const uint8_t zero_s[4]    = {1, 1, 0, 0};    /* period 0 s */
    const uint8_t too_slow[4]  = {1, 1, 0x0E, 0x11}; /* 3601 s */
    const uint8_t bad_sid_i[4] = {1, 9, 0, 5};
    const uint8_t n_short_i[4] = {2, 1, 0, 5};    /* says 2 entries, carries 1 */

    TEST_ASSERT_EQUAL(OBSW_ERR_UNKNOWN_SID, send_tc(3, 27, 0, sid2, sizeof sid2));
    TEST_ASSERT_EQUAL(OBSW_ERR_BAD_APP_DATA, send_tc(3, 5, 0, n_wrong, sizeof n_wrong));
    TEST_ASSERT_EQUAL(OBSW_ERR_BAD_APP_DATA, send_tc(3, 6, 0, n_zero, sizeof n_zero));
    TEST_ASSERT_EQUAL(OBSW_ERR_BAD_APP_DATA, send_tc(3, 31, 0, zero_s, sizeof zero_s));
    TEST_ASSERT_EQUAL(OBSW_ERR_BAD_APP_DATA, send_tc(3, 31, 0, too_slow, sizeof too_slow));
    TEST_ASSERT_EQUAL(OBSW_ERR_UNKNOWN_SID, send_tc(3, 31, 0, bad_sid_i, sizeof bad_sid_i));
    TEST_ASSERT_EQUAL(OBSW_ERR_BAD_APP_DATA, send_tc(3, 31, 0, n_short_i, sizeof n_short_i));
    /* every one was refused at acceptance with TM[1,2] */
    TEST_ASSERT_EQUAL_size_t(7, count_tm(1, 2));
    TEST_ASSERT_EQUAL_UINT16(1, obsw.hk_interval_s);
}

/* ---- ST[8] mode commands ---------------------------------------------- */

static void test_set_mode_full_chain_and_hk_follows(void)
{
    hk_t h;
    TEST_ASSERT_EQUAL(OBSW_OK, set_mode(INSTR_STANDBY, PUS_ACK_ALL));
    TEST_ASSERT_EQUAL_size_t(3, cap.count);
    assert_tm(0, 1, 1);
    assert_tm(1, 1, 3);
    assert_tm(2, 1, 7);

    run_seconds(20);
    h = last_hk();
    TEST_ASSERT_EQUAL_UINT8(INSTR_STANDBY, h.mode);
    TEST_ASSERT_EQUAL_UINT16(1, h.mode_changes);
    TEST_ASSERT_UINT_WITHIN(4, 120, h.current_ma);
    TEST_ASSERT_TRUE(h.temp_cdeg > 2400); /* warmed up towards 25 degC */
}

static void test_illegal_transition_fails_at_execution(void)
{
    /* OFF -> MEASURE is a valid command for a valid mode, so it is accepted,
     * but the instrument refuses it: TM[1,8] with the reason. */
    TEST_ASSERT_EQUAL(OBSW_ERR_ILLEGAL_TRANSITION, set_mode(INSTR_MEASURE, PUS_ACK_ALL));
    TEST_ASSERT_EQUAL_size_t(3, cap.count);
    assert_tm(0, 1, 1);
    assert_tm(1, 1, 3);
    assert_tm(2, 1, 8);
    TEST_ASSERT_EQUAL_UINT16(OBSW_ERR_ILLEGAL_TRANSITION, failure_code(2));
    TEST_ASSERT_EQUAL(INSTR_OFF, obsw.instr.mode);
}

static void test_execution_failure_reported_even_without_acks(void)
{
    TEST_ASSERT_EQUAL(OBSW_ERR_ILLEGAL_TRANSITION, set_mode(INSTR_MEASURE, 0));
    TEST_ASSERT_EQUAL_size_t(1, cap.count);
    assert_tm(0, 1, 8);
}

static void test_function_argument_errors_fail_acceptance(void)
{
    const uint8_t unknown_fn[2] = {0x00, 0x63};
    const uint8_t bad_mode[3]   = {0x00, 0x01, 9};
    const uint8_t extra_arg[4]  = {0x00, 0x01, 1, 0};
    const uint8_t reset_arg[3]  = {0x00, 0x02, 0};

    TEST_ASSERT_EQUAL(OBSW_ERR_UNKNOWN_FUNCTION, send_tc(8, 1, 0, unknown_fn, sizeof unknown_fn));
    TEST_ASSERT_EQUAL(OBSW_ERR_BAD_APP_DATA, send_tc(8, 1, 0, bad_mode, sizeof bad_mode));
    TEST_ASSERT_EQUAL(OBSW_ERR_BAD_APP_DATA, send_tc(8, 1, 0, extra_arg, sizeof extra_arg));
    TEST_ASSERT_EQUAL(OBSW_ERR_BAD_APP_DATA, send_tc(8, 1, 0, reset_arg, sizeof reset_arg));
    TEST_ASSERT_EQUAL(OBSW_ERR_BAD_APP_DATA, send_tc(8, 1, 0, unknown_fn, 1)); /* too short */
    TEST_ASSERT_EQUAL_size_t(5, count_tm(1, 2));
    TEST_ASSERT_EQUAL_UINT16(OBSW_ERR_UNKNOWN_FUNCTION, failure_code(0));
}

static void test_measure_then_reset_science(void)
{
    const uint8_t reset[2] = {0x00, 0x02};
    TEST_ASSERT_EQUAL(OBSW_OK, set_mode(INSTR_STANDBY, 0));
    TEST_ASSERT_EQUAL(OBSW_OK, set_mode(INSTR_MEASURE, 0));
    run_seconds(3);
    TEST_ASSERT_TRUE(last_hk().science >= 3000u);
    TEST_ASSERT_UINT_WITHIN(4, 450, last_hk().current_ma);
    TEST_ASSERT_EQUAL(OBSW_OK, send_tc(8, 1, 0, reset, sizeof reset));
    TEST_ASSERT_EQUAL(OBSW_OK, set_mode(INSTR_STANDBY, 0));
    run_seconds(1);
    TEST_ASSERT_EQUAL_UINT32(0, last_hk().science);
}

static void test_hk_reports_tc_counters(void)
{
    const uint8_t sid2[2] = {1, 2};
    TEST_ASSERT_EQUAL(OBSW_OK, set_mode(INSTR_STANDBY, 0));
    TEST_ASSERT_EQUAL(OBSW_ERR_UNKNOWN_SID, send_tc(3, 27, 0, sid2, sizeof sid2));
    TEST_ASSERT_EQUAL(OBSW_OK, set_mode(INSTR_OFF, 0));
    TEST_ASSERT_EQUAL(OBSW_ERR_ILLEGAL_TRANSITION, set_mode(INSTR_MEASURE, 0));
    run_seconds(1);
    /* accepted: STANDBY, OFF, MEASURE (accepted, then failed executing) */
    TEST_ASSERT_EQUAL_UINT16(3, last_hk().tc_accepted);
    TEST_ASSERT_EQUAL_UINT16(1, last_hk().tc_rejected);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_periodic_hk_once_per_second);
    RUN_TEST(test_long_stall_is_capped);
    RUN_TEST(test_clock_going_backwards_resyncs);
    RUN_TEST(test_tick_without_clock_does_nothing);
    RUN_TEST(test_disable_and_enable);
    RUN_TEST(test_change_interval);
    RUN_TEST(test_one_shot_goes_to_requester);
    RUN_TEST(test_hk_tc_argument_errors);
    RUN_TEST(test_set_mode_full_chain_and_hk_follows);
    RUN_TEST(test_illegal_transition_fails_at_execution);
    RUN_TEST(test_execution_failure_reported_even_without_acks);
    RUN_TEST(test_function_argument_errors_fail_acceptance);
    RUN_TEST(test_measure_then_reset_science);
    RUN_TEST(test_hk_reports_tc_counters);
    return UNITY_END();
}
