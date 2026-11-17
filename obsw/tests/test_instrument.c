#include "unity.h"
#include "tinyobsw/instrument.h"

static instrument_t instr;

void setUp(void) { instrument_init(&instr); }
void tearDown(void) {}

static void goto_mode(instr_mode_t m)
{
    TEST_ASSERT_EQUAL(OBSW_OK, instrument_set_mode(&instr, m));
}

static void test_starts_off_at_room_temperature(void)
{
    TEST_ASSERT_EQUAL(INSTR_OFF, instr.mode);
    TEST_ASSERT_EQUAL_INT16(2000, instr.temp_cdeg);
    TEST_ASSERT_EQUAL_UINT32(0, instr.science_count);
    TEST_ASSERT_EQUAL_UINT16(0, instr.mode_changes);
}

static void test_transition_table(void)
{
    /* EXPECTED[from][to]: 1 allowed, 0 refused. Mirrors the diagram in instrument.h. */
    static const int EXPECTED[4][4] = {
        { 1, 1, 0, 1 },
        { 1, 1, 1, 1 },
        { 0, 1, 1, 1 },
        { 0, 1, 0, 1 },
    };
    /* A legal path into each starting mode. */
    static const instr_mode_t PATH[4][2] = {
        { INSTR_OFF, INSTR_OFF },
        { INSTR_STANDBY, INSTR_STANDBY },
        { INSTR_STANDBY, INSTR_MEASURE },
        { INSTR_SAFE, INSTR_SAFE },
    };
    unsigned from;
    unsigned to;
    for (from = 0; from < 4; ++from) {
        for (to = 0; to < 4; ++to) {
            obsw_status_t st;
            instrument_init(&instr);
            goto_mode(PATH[from][0]);
            goto_mode(PATH[from][1]);
            TEST_ASSERT_EQUAL(from, instr.mode);
            st = instrument_set_mode(&instr, (instr_mode_t)to);
            if (EXPECTED[from][to]) {
                TEST_ASSERT_EQUAL(OBSW_OK, st);
                TEST_ASSERT_EQUAL(to, instr.mode);
            } else {
                TEST_ASSERT_EQUAL(OBSW_ERR_ILLEGAL_TRANSITION, st);
                TEST_ASSERT_EQUAL(from, instr.mode); /* refused: nothing changed */
            }
        }
    }
}

static void test_mode_changes_counted_only_on_real_change(void)
{
    goto_mode(INSTR_STANDBY);
    goto_mode(INSTR_STANDBY);
    goto_mode(INSTR_MEASURE);
    TEST_ASSERT_EQUAL(OBSW_ERR_ILLEGAL_TRANSITION, instrument_set_mode(&instr, INSTR_OFF));
    TEST_ASSERT_EQUAL_UINT16(2, instr.mode_changes);
}

static void test_undefined_mode_rejected(void)
{
    TEST_ASSERT_FALSE(instrument_mode_valid(4));
    TEST_ASSERT_TRUE(instrument_mode_valid(3));
    TEST_ASSERT_EQUAL(OBSW_ERR_BAD_APP_DATA, instrument_set_mode(&instr, (instr_mode_t)7));
}

static void test_temperature_settles_exactly_and_monotonically(void)
{
    int i;
    int16_t prev = instr.temp_cdeg;
    goto_mode(INSTR_STANDBY);
    for (i = 0; i < 60; ++i) {
        instrument_step(&instr);
        TEST_ASSERT_TRUE(instr.temp_cdeg >= prev);
        prev = instr.temp_cdeg;
    }
    TEST_ASSERT_EQUAL_INT16(2500, instr.temp_cdeg);

    goto_mode(INSTR_OFF); /* cooling back down */
    for (i = 0; i < 60; ++i) {
        instrument_step(&instr);
        TEST_ASSERT_TRUE(instr.temp_cdeg <= prev);
        prev = instr.temp_cdeg;
    }
    TEST_ASSERT_EQUAL_INT16(2000, instr.temp_cdeg);
}

static void test_science_counts_only_while_measuring(void)
{
    uint32_t before;
    instrument_step(&instr);
    TEST_ASSERT_EQUAL_UINT32(0, instr.science_count);

    goto_mode(INSTR_STANDBY);
    goto_mode(INSTR_MEASURE);
    instrument_step(&instr);
    TEST_ASSERT_TRUE(instr.science_count >= 1000u && instr.science_count < 1064u);

    goto_mode(INSTR_STANDBY);
    before = instr.science_count;
    instrument_step(&instr);
    TEST_ASSERT_EQUAL_UINT32(before, instr.science_count);

    instrument_reset_science(&instr);
    TEST_ASSERT_EQUAL_UINT32(0, instr.science_count);
}

static void test_sensor_noise_stays_in_bounds(void)
{
    int i;
    instr_sample_t s;
    for (i = 0; i < 1000; ++i) {
        instrument_sample(&instr, &s);
        TEST_ASSERT_INT_WITHIN(3, instr.temp_cdeg, s.temp_cdeg);
        TEST_ASSERT_EQUAL_UINT16(0, s.current_ma); /* OFF draws nothing */
        TEST_ASSERT_UINT_WITHIN(20, 28000, s.voltage_mv);
    }
    goto_mode(INSTR_STANDBY);
    goto_mode(INSTR_MEASURE);
    for (i = 0; i < 1000; ++i) {
        instrument_sample(&instr, &s);
        TEST_ASSERT_UINT_WITHIN(4, 450, s.current_ma);
        TEST_ASSERT_EQUAL(INSTR_MEASURE, s.mode);
    }
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_starts_off_at_room_temperature);
    RUN_TEST(test_transition_table);
    RUN_TEST(test_mode_changes_counted_only_on_real_change);
    RUN_TEST(test_undefined_mode_rejected);
    RUN_TEST(test_temperature_settles_exactly_and_monotonically);
    RUN_TEST(test_science_counts_only_while_measuring);
    RUN_TEST(test_sensor_noise_stays_in_bounds);
    return UNITY_END();
}
