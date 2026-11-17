/* ST[12] monitoring, ST[5] events and the automatic safe-mode action. */
#include <string.h>

#include "unity.h"
#include "tinyobsw/obsw.h"

#define OUR_APID  0x064u
#define GROUND_ID 0x00Au
#define MAX_TM    256u

typedef struct {
    uint8_t pkt[MAX_TM][OBSW_TM_MAX_LEN];
    size_t  len[MAX_TM];
    size_t  count;
} capture_t;

static capture_t cap;
static obsw_t    obsw;
static uint32_t  now_s;
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

static obsw_status_t send_tc(uint8_t service, uint8_t subtype, const uint8_t *data, size_t len)
{
    pus_tc_sec_hdr_t s;
    uint8_t buf[64];
    size_t n = 0;
    s.ack_flags = 0;
    s.service   = service;
    s.subtype   = subtype;
    s.source_id = GROUND_ID;
    TEST_ASSERT_EQUAL(OBSW_OK, pus_tc_build(OUR_APID, tc_seq++, &s, data, len, buf, sizeof buf, &n));
    return obsw_handle_tc(&obsw, buf, n);
}

static obsw_status_t function(uint16_t id)
{
    const uint8_t d[2] = {(uint8_t)(id >> 8), (uint8_t)id};
    return send_tc(8, 1, d, sizeof d);
}

static obsw_status_t set_mode(instr_mode_t m)
{
    const uint8_t d[3] = {0, 1, (uint8_t)m};
    return send_tc(8, 1, d, sizeof d);
}

void setUp(void)
{
    obsw_config_t cfg;
    const uint8_t hk_off[2] = {1, 1};
    memset(&cap, 0, sizeof cap);
    now_s  = 0u;
    tc_seq = 0u;
    cfg.apid     = OUR_APID;
    cfg.tm_sink  = capture_sink;
    cfg.tm_ctx   = &cap;
    cfg.time_now = fake_clock;
    cfg.time_ctx = NULL;
    TEST_ASSERT_EQUAL(OBSW_OK, obsw_init(&obsw, &cfg));
    obsw.time_rate_exp  = 6; /* time reports only every 64 s */
    obsw.time_countdown = 64;
    obsw_tick(&obsw);
    TEST_ASSERT_EQUAL(OBSW_OK, send_tc(3, 6, hk_off, sizeof hk_off)); /* quiet: no periodic HK */
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

static uint16_t be16(const uint8_t *p) { return (uint16_t)((p[0] << 8) | p[1]); }

/* Index of the first TM[service,subtype] at or after `from`, or -1. */
static int find(uint8_t service, uint8_t subtype, size_t from)
{
    size_t i;
    for (i = from; i < cap.count; ++i) {
        pus_tm_t tm = tm_at(i);
        if (tm.sec.service == service && tm.sec.subtype == subtype) {
            return (int)i;
        }
    }
    return -1;
}

static size_t count(uint8_t service, uint8_t subtype)
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

/* Run until the instrument reaches `mode` (or give up after `limit` s). Returns seconds taken. */
static unsigned run_until_mode(instr_mode_t mode, unsigned limit)
{
    unsigned s;
    for (s = 0; s < limit; ++s) {
        if (obsw.instr.mode == mode) {
            return s;
        }
        run_seconds(1);
    }
    TEST_FAIL_MESSAGE("instrument never reached the expected mode");
    return limit;
}

/* ---- nominal behaviour ------------------------------------------------- */

static void test_normal_measuring_stays_within_limits(void)
{
    TEST_ASSERT_EQUAL(OBSW_OK, set_mode(INSTR_STANDBY));
    TEST_ASSERT_EQUAL(OBSW_OK, set_mode(INSTR_MEASURE));
    run_seconds(120); /* settles at 38 degC, below the 40 degC warning */
    TEST_ASSERT_EQUAL_size_t(0, count(12, 12));
    TEST_ASSERT_EQUAL_size_t(0, count(5, 2) + count(5, 4));
    TEST_ASSERT_EQUAL(INSTR_MEASURE, obsw.instr.mode);
    TEST_ASSERT_EQUAL_UINT8(PMON_WITHIN_LIMITS, obsw.pmon_status);
}

static void test_heater_fault_has_no_effect_while_off(void)
{
    TEST_ASSERT_EQUAL(OBSW_OK, function(100));
    run_seconds(120);
    TEST_ASSERT_EQUAL_size_t(0, count(12, 12));
    TEST_ASSERT_INT_WITHIN(5, 2000, obsw.instr.temp_cdeg);
}

/* ---- the fault scenario ---------------------------------------------------- */

static void test_heater_fault_while_measuring_triggers_safe_mode(void)
{
    int warn_tr;
    int warn_ev;
    int danger_tr;
    int danger_ev;
    int safe_ev;
    pus_tm_t tm;
    unsigned took;

    TEST_ASSERT_EQUAL(OBSW_OK, set_mode(INSTR_STANDBY));
    TEST_ASSERT_EQUAL(OBSW_OK, set_mode(INSTR_MEASURE));
    run_seconds(120);
    cap.count = 0;

    TEST_ASSERT_EQUAL(OBSW_OK, function(100));          /* heater stuck on */
    took = run_until_mode(INSTR_SAFE, 60);
    TEST_ASSERT_TRUE_MESSAGE(took <= 10, "safe mode took too long");

    /* The order tells the story: warning, then danger, then the action. */
    warn_tr   = find(12, 12, 0);
    warn_ev   = find(5, 2, 0);
    danger_tr = find(12, 12, (size_t)warn_tr + 1u);
    danger_ev = find(5, 4, 0);
    safe_ev   = find(5, 3, 0);
    TEST_ASSERT_TRUE(warn_tr >= 0 && warn_ev > warn_tr);
    TEST_ASSERT_TRUE(danger_tr > warn_ev && danger_ev > danger_tr && safe_ev > danger_ev);

    tm = tm_at((size_t)warn_tr);
    TEST_ASSERT_EQUAL_size_t(10, tm.data_len);
    TEST_ASSERT_EQUAL_UINT16(1, be16(&tm.data[0]));             /* PMON 1 */
    TEST_ASSERT_TRUE((int16_t)be16(&tm.data[4]) > 4000);        /* value above warning */
    TEST_ASSERT_EQUAL_INT16(4000, (int16_t)be16(&tm.data[6]));  /* limit crossed */
    TEST_ASSERT_EQUAL_UINT8(PMON_WITHIN_LIMITS, tm.data[8]);
    TEST_ASSERT_EQUAL_UINT8(PMON_ABOVE_WARNING, tm.data[9]);

    tm = tm_at((size_t)danger_tr);
    TEST_ASSERT_EQUAL_INT16(4500, (int16_t)be16(&tm.data[6]));
    TEST_ASSERT_EQUAL_UINT8(PMON_ABOVE_WARNING, tm.data[8]);
    TEST_ASSERT_EQUAL_UINT8(PMON_ABOVE_DANGER, tm.data[9]);

    tm = tm_at((size_t)danger_ev);
    TEST_ASSERT_EQUAL_UINT16(2, be16(&tm.data[0]));             /* EV_TEMP_DANGER */
    TEST_ASSERT_TRUE((int16_t)be16(&tm.data[2]) > 4500);

    tm = tm_at((size_t)safe_ev);
    TEST_ASSERT_EQUAL_size_t(3, tm.data_len);
    TEST_ASSERT_EQUAL_UINT16(4, be16(&tm.data[0]));             /* EV_AUTONOMOUS_SAFE */
    TEST_ASSERT_EQUAL_UINT8(INSTR_MEASURE, tm.data[2]);         /* came from MEASURE */
}

static void test_safe_mode_cools_the_instrument_back_to_nominal(void)
{
    int nominal;
    TEST_ASSERT_EQUAL(OBSW_OK, set_mode(INSTR_STANDBY));
    TEST_ASSERT_EQUAL(OBSW_OK, set_mode(INSTR_MEASURE));
    TEST_ASSERT_EQUAL(OBSW_OK, function(100));
    run_until_mode(INSTR_SAFE, 200);
    cap.count = 0;

    run_seconds(60); /* heater unpowered in SAFE: cools towards 22 degC */
    TEST_ASSERT_EQUAL_UINT8(PMON_WITHIN_LIMITS, obsw.pmon_status);
    /* danger -> warning is reported but raises no new warning event... */
    TEST_ASSERT_EQUAL_size_t(2, count(12, 12));
    TEST_ASSERT_EQUAL_size_t(0, count(5, 2));
    /* ...and getting back within limits is announced */
    nominal = find(5, 1, 0);
    TEST_ASSERT_TRUE(nominal >= 0);
    TEST_ASSERT_EQUAL_UINT16(3, be16(&tm_at((size_t)nominal).data[0])); /* EV_TEMP_NOMINAL */
    TEST_ASSERT_EQUAL(INSTR_SAFE, obsw.instr.mode); /* stays safe until ground acts */
}

static void test_unrepaired_heater_trips_again_from_standby(void)
{
    TEST_ASSERT_EQUAL(OBSW_OK, set_mode(INSTR_STANDBY));
    TEST_ASSERT_EQUAL(OBSW_OK, function(100));
    run_until_mode(INSTR_SAFE, 120); /* 25 + 22 degC overshoots the danger limit */
    run_seconds(60);
    TEST_ASSERT_EQUAL(OBSW_OK, set_mode(INSTR_STANDBY)); /* ground recovers without repairing */
    run_until_mode(INSTR_SAFE, 120);
    TEST_ASSERT_EQUAL_size_t(2, count(5, 3)); /* two autonomous safings */
}

static void test_repaired_heater_allows_normal_operation(void)
{
    TEST_ASSERT_EQUAL(OBSW_OK, set_mode(INSTR_STANDBY));
    TEST_ASSERT_EQUAL(OBSW_OK, set_mode(INSTR_MEASURE));
    TEST_ASSERT_EQUAL(OBSW_OK, function(100));
    run_until_mode(INSTR_SAFE, 60);
    run_seconds(60);
    TEST_ASSERT_EQUAL(OBSW_OK, function(101)); /* repair */
    TEST_ASSERT_EQUAL(OBSW_OK, set_mode(INSTR_STANDBY));
    TEST_ASSERT_EQUAL(OBSW_OK, set_mode(INSTR_MEASURE));
    cap.count = 0;
    run_seconds(180);
    TEST_ASSERT_EQUAL(INSTR_MEASURE, obsw.instr.mode);
    TEST_ASSERT_EQUAL_size_t(0, count(12, 12));
}

/* ---- noise and repetition --------------------------------------------------- */

static void test_single_sample_over_the_limit_is_ignored(void)
{
    /* Put the limit right at the steady STANDBY temperature: noise makes
     * single samples cross it, but a change needs two in a row. */
    obsw.instr.mode = INSTR_STANDBY;
    obsw.instr.temp_cdeg = 2500;
    obsw.pmon_warn_cdeg = 2502;
    run_seconds(1);
    TEST_ASSERT_TRUE(obsw.pmon_count <= 1u);
    obsw.pmon_count = 0;
    obsw.pmon_candidate = PMON_WITHIN_LIMITS;
    /* Measured over these 300 noisy samples: 78 transitions if every
     * sample counted, 10 with two-in-a-row. The bound fails if the
     * repetition rule is removed. */
    run_seconds(300);
    TEST_ASSERT_TRUE(count(12, 12) < 30u);
}

/* ---- telecommands ------------------------------------------------------------ */

static void test_disabled_monitoring_does_not_act(void)
{
    const uint8_t pmon1[3] = {1, 0, 1};
    TEST_ASSERT_EQUAL(OBSW_OK, send_tc(12, 2, pmon1, sizeof pmon1));
    TEST_ASSERT_EQUAL(OBSW_OK, set_mode(INSTR_STANDBY));
    TEST_ASSERT_EQUAL(OBSW_OK, set_mode(INSTR_MEASURE));
    TEST_ASSERT_EQUAL(OBSW_OK, function(100));
    run_seconds(60);
    TEST_ASSERT_EQUAL(INSTR_MEASURE, obsw.instr.mode); /* nobody watching: it overheats */
    TEST_ASSERT_TRUE(obsw.instr.temp_cdeg > 5500);
    TEST_ASSERT_EQUAL_size_t(0, count(12, 12));

    TEST_ASSERT_EQUAL(OBSW_OK, send_tc(12, 1, pmon1, sizeof pmon1)); /* re-enable */
    run_until_mode(INSTR_SAFE, 10);
}

static void test_ground_can_change_limits(void)
{
    /* Lower the warning to 30 degC, danger to 35 degC: normal MEASURE (38) now trips. */
    const uint8_t limits[7] = {1, 0, 1, 0x0B, 0xB8, 0x0D, 0xAC};
    TEST_ASSERT_EQUAL(OBSW_OK, send_tc(12, 7, limits, sizeof limits));
    TEST_ASSERT_EQUAL_INT16(3000, obsw.pmon_warn_cdeg);
    TEST_ASSERT_EQUAL_INT16(3500, obsw.pmon_danger_cdeg);
    TEST_ASSERT_EQUAL(OBSW_OK, set_mode(INSTR_STANDBY));
    TEST_ASSERT_EQUAL(OBSW_OK, set_mode(INSTR_MEASURE));
    run_until_mode(INSTR_SAFE, 120);
}

static void test_danger_while_already_safe_takes_no_action(void)
{
    /* Limits below SAFE's 22 degC: DANGER is reported, but there is no
     * safer mode to go to, so no autonomous-safe event. */
    const uint8_t limits[7] = {1, 0, 1, 0x05, 0xDC, 0x07, 0xD0}; /* 15 / 20 degC */
    TEST_ASSERT_EQUAL(OBSW_OK, set_mode(INSTR_SAFE));
    run_seconds(60);
    TEST_ASSERT_EQUAL(OBSW_OK, send_tc(12, 7, limits, sizeof limits));
    run_seconds(10);
    TEST_ASSERT_EQUAL_UINT8(PMON_ABOVE_DANGER, obsw.pmon_status);
    TEST_ASSERT_EQUAL_size_t(1, count(5, 4));
    TEST_ASSERT_EQUAL_size_t(0, count(5, 3));
}

static void test_monitoring_tc_argument_errors(void)
{
    const uint8_t pmon2[3]      = {1, 0, 2};
    const uint8_t n_wrong[5]    = {1, 0, 1, 0, 1};
    const uint8_t inverted[7]   = {1, 0, 1, 0x11, 0x94, 0x0F, 0xA0}; /* 45 then 40 */
    const uint8_t too_hot[7]    = {1, 0, 1, 0x0F, 0xA0, 0x7F, 0xFF}; /* danger 327 degC */
    const uint8_t limits_id2[7] = {1, 0, 2, 0x0F, 0xA0, 0x11, 0x94};
    const uint8_t n_short[7]    = {2, 0, 1, 0x0F, 0xA0, 0x11, 0x94}; /* says 2 entries, has 1 */
    TEST_ASSERT_EQUAL(OBSW_ERR_UNKNOWN_PMON, send_tc(12, 1, pmon2, sizeof pmon2));
    TEST_ASSERT_EQUAL(OBSW_ERR_BAD_APP_DATA, send_tc(12, 2, n_wrong, sizeof n_wrong));
    TEST_ASSERT_EQUAL(OBSW_ERR_BAD_APP_DATA, send_tc(12, 7, inverted, sizeof inverted));
    TEST_ASSERT_EQUAL(OBSW_ERR_BAD_APP_DATA, send_tc(12, 7, too_hot, sizeof too_hot));
    TEST_ASSERT_EQUAL(OBSW_ERR_UNKNOWN_PMON, send_tc(12, 7, limits_id2, sizeof limits_id2));
    TEST_ASSERT_EQUAL(OBSW_ERR_BAD_APP_DATA, send_tc(12, 7, n_short, sizeof n_short));
    TEST_ASSERT_EQUAL_INT16(4000, obsw.pmon_warn_cdeg); /* untouched */
}

static void test_sim_function_arguments_checked(void)
{
    const uint8_t extra[3] = {0, 100, 1};
    TEST_ASSERT_EQUAL(OBSW_ERR_BAD_APP_DATA, send_tc(8, 1, extra, sizeof extra));
    TEST_ASSERT_EQUAL(OBSW_OK, function(100));
    TEST_ASSERT_EQUAL_UINT8(1, obsw.instr.heater_fault);
    TEST_ASSERT_EQUAL(OBSW_OK, function(101));
    TEST_ASSERT_EQUAL_UINT8(0, obsw.instr.heater_fault);
}

static void test_housekeeping_reports_monitoring_state(void)
{
    const uint8_t sid1[2] = {1, 1};
    pus_tm_t tm;
    int hk;
    TEST_ASSERT_EQUAL(OBSW_OK, function(100));
    cap.count = 0;
    TEST_ASSERT_EQUAL(OBSW_OK, send_tc(3, 27, sid1, sizeof sid1));
    hk = find(3, 25, 0);
    TEST_ASSERT_TRUE(hk >= 0);
    tm = tm_at((size_t)hk);
    TEST_ASSERT_EQUAL_size_t(24, tm.data_len);
    TEST_ASSERT_EQUAL_UINT8(PMON_WITHIN_LIMITS, tm.data[18]);
    TEST_ASSERT_EQUAL_HEX8(0x03, tm.data[19]); /* heater fault + monitoring enabled */
    TEST_ASSERT_EQUAL_INT16(4000, (int16_t)be16(&tm.data[20]));
    TEST_ASSERT_EQUAL_INT16(4500, (int16_t)be16(&tm.data[22]));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_normal_measuring_stays_within_limits);
    RUN_TEST(test_heater_fault_has_no_effect_while_off);
    RUN_TEST(test_heater_fault_while_measuring_triggers_safe_mode);
    RUN_TEST(test_safe_mode_cools_the_instrument_back_to_nominal);
    RUN_TEST(test_unrepaired_heater_trips_again_from_standby);
    RUN_TEST(test_repaired_heater_allows_normal_operation);
    RUN_TEST(test_single_sample_over_the_limit_is_ignored);
    RUN_TEST(test_disabled_monitoring_does_not_act);
    RUN_TEST(test_ground_can_change_limits);
    RUN_TEST(test_danger_while_already_safe_takes_no_action);
    RUN_TEST(test_monitoring_tc_argument_errors);
    RUN_TEST(test_sim_function_arguments_checked);
    RUN_TEST(test_housekeeping_reports_monitoring_state);
    return UNITY_END();
}
