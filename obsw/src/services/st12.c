/*
 * ST[12] on-board monitoring, and the safe-mode action it can trigger.
 *
 * One parameter monitoring definition, PMON 1, checks the detector
 * temperature once per second against two upper limits:
 *
 *      danger  45.00 degC  -----------------  above: DANGER -> instrument to SAFE
 *      warning 40.00 degC  -----------------  above: WARNING
 *                                             below: within limits
 *
 * A new status only counts once it has been seen on
 * OBSW_PMON_REPETITIONS samples in a row, so sensor noise near a limit
 * cannot make the status flap.
 *
 * Every confirmed change produces TM[12,12], a check transition report
 * (10 bytes, big-endian):
 *   PMON ID u16 | parameter ID u16 | value i16 | limit crossed i16
 *   | previous status u8 | new status u8
 * and an ST[5] event:
 *   into WARNING (from within)  -> TM[5,2] EV_TEMP_WARNING
 *   into DANGER                 -> TM[5,4] EV_TEMP_DANGER, then the action:
 *                                  instrument to SAFE, TM[5,3] EV_AUTONOMOUS_SAFE
 *   back within limits          -> TM[5,1] EV_TEMP_NOMINAL
 * In full PUS the link from the DANGER event to the action is an ST[19]
 * event-action definition; here it is fixed in code.
 *
 * Telecommands:
 *   TC[12,1] enable   N u8, then N x PMON ID u16
 *   TC[12,2] disable  N u8, then N x PMON ID u16
 *   TC[12,7] limits   N u8, then N x (PMON ID u16, warning i16, danger i16)
 */
#include "bytes.h"
#include "obsw_internal.h"

#define ST12_SERVICE          12u
#define ST12_TRANSITION       12u
#define PARAM_ID_TEMPERATURE  1u
#define LIMIT_MIN_CDEG        (-5000)  /* -50 degC */
#define LIMIT_MAX_CDEG        15000    /* 150 degC */

void st12_init(obsw_t *obsw)
{
    obsw->pmon_enabled     = 1u;
    obsw->pmon_status      = PMON_WITHIN_LIMITS;
    obsw->pmon_candidate   = PMON_WITHIN_LIMITS;
    obsw->pmon_count       = 0u;
    obsw->pmon_warn_cdeg   = OBSW_PMON_WARN_CDEG;
    obsw->pmon_danger_cdeg = OBSW_PMON_DANGER_CDEG;
}

static uint8_t classify(const obsw_t *obsw, int16_t temp)
{
    if (temp > obsw->pmon_danger_cdeg) {
        return PMON_ABOVE_DANGER;
    }
    if (temp > obsw->pmon_warn_cdeg) {
        return PMON_ABOVE_WARNING;
    }
    return PMON_WITHIN_LIMITS;
}

static void report_transition(obsw_t *obsw, int16_t value, uint8_t from, uint8_t to)
{
    uint8_t d[ST12_TRANSITION_LEN];
    uint8_t higher = from > to ? from : to;
    int16_t limit  = higher == PMON_ABOVE_DANGER ? obsw->pmon_danger_cdeg : obsw->pmon_warn_cdeg;

    put_be16(&d[0], OBSW_PMON_TEMP_ID);
    put_be16(&d[2], PARAM_ID_TEMPERATURE);
    put_be16(&d[4], (uint16_t)value);
    put_be16(&d[6], (uint16_t)limit);
    d[8] = from;
    d[9] = to;
    (void)obsw_emit_tm(obsw, ST12_SERVICE, ST12_TRANSITION, 0u, d, sizeof d);
}

/* The on-board reaction to DANGER: cut heater power by entering SAFE. */
static void safe_mode_action(obsw_t *obsw)
{
    instr_mode_t before = obsw->instr.mode;
    uint8_t aux[1];

    if (before == INSTR_SAFE) {
        return; /* already safe: nothing to do */
    }
    if (instrument_set_mode(&obsw->instr, INSTR_SAFE) == OBSW_OK) {
        aux[0] = (uint8_t)before;
        st05_report(obsw, ST05_MEDIUM, EV_AUTONOMOUS_SAFE, aux, sizeof aux);
    }
}

void st12_check(obsw_t *obsw)
{
    instr_sample_t s;
    uint8_t seen;
    uint8_t before;
    uint8_t aux[2];

    if (!obsw->pmon_enabled) {
        return;
    }
    instrument_sample(&obsw->instr, &s);
    seen = classify(obsw, s.temp_cdeg);

    if (seen == obsw->pmon_status) {
        obsw->pmon_candidate = seen;
        obsw->pmon_count     = 0u;
        return;
    }
    if (seen != obsw->pmon_candidate) {
        obsw->pmon_candidate = seen;
        obsw->pmon_count     = 0u;
    }
    obsw->pmon_count++;
    if (obsw->pmon_count < OBSW_PMON_REPETITIONS) {
        return;
    }

    before            = obsw->pmon_status;
    obsw->pmon_status = seen;
    obsw->pmon_count  = 0u;
    report_transition(obsw, s.temp_cdeg, before, seen);

    put_be16(aux, (uint16_t)s.temp_cdeg);
    if (seen == PMON_ABOVE_DANGER) {
        st05_report(obsw, ST05_HIGH, EV_TEMP_DANGER, aux, sizeof aux);
        safe_mode_action(obsw);
    } else if (seen == PMON_ABOVE_WARNING && before == PMON_WITHIN_LIMITS) {
        st05_report(obsw, ST05_LOW, EV_TEMP_WARNING, aux, sizeof aux);
    } else if (seen == PMON_WITHIN_LIMITS) {
        st05_report(obsw, ST05_INFO, EV_TEMP_NOMINAL, aux, sizeof aux);
    }
}

/* ---- acceptance checks -------------------------------------------------- */

obsw_status_t st12_check_ids(const obsw_t *obsw, const pus_tc_t *tc)
{
    size_t i;
    size_t n = tc->data[0];
    (void)obsw;

    if (n == 0u || tc->data_len != 1u + 2u * n) {
        return OBSW_ERR_BAD_APP_DATA;
    }
    for (i = 0; i < n; ++i) {
        if (get_be16(&tc->data[1u + 2u * i]) != OBSW_PMON_TEMP_ID) {
            return OBSW_ERR_UNKNOWN_PMON;
        }
    }
    return OBSW_OK;
}

obsw_status_t st12_check_limits(const obsw_t *obsw, const pus_tc_t *tc)
{
    size_t i;
    size_t n = tc->data[0];
    (void)obsw;

    if (n == 0u || tc->data_len != 1u + 6u * n) {
        return OBSW_ERR_BAD_APP_DATA;
    }
    for (i = 0; i < n; ++i) {
        const uint8_t *e = &tc->data[1u + 6u * i];
        int16_t warn   = (int16_t)get_be16(&e[2]);
        int16_t danger = (int16_t)get_be16(&e[4]);
        if (get_be16(e) != OBSW_PMON_TEMP_ID) {
            return OBSW_ERR_UNKNOWN_PMON;
        }
        if (warn >= danger || warn < LIMIT_MIN_CDEG || danger > LIMIT_MAX_CDEG) {
            return OBSW_ERR_BAD_APP_DATA;
        }
    }
    return OBSW_OK;
}

/* ---- execution ---------------------------------------------------------- */

obsw_status_t st12_enable(obsw_t *obsw, const pus_tc_t *tc)
{
    (void)tc;
    /* Start from "within limits": if the temperature is already high,
     * the transition is reported after the usual repetitions. */
    obsw->pmon_enabled   = 1u;
    obsw->pmon_status    = PMON_WITHIN_LIMITS;
    obsw->pmon_candidate = PMON_WITHIN_LIMITS;
    obsw->pmon_count     = 0u;
    return OBSW_OK;
}

obsw_status_t st12_disable(obsw_t *obsw, const pus_tc_t *tc)
{
    (void)tc;
    obsw->pmon_enabled = 0u;
    return OBSW_OK;
}

obsw_status_t st12_set_limits(obsw_t *obsw, const pus_tc_t *tc)
{
    size_t i;
    for (i = 0; i < tc->data[0]; ++i) {
        const uint8_t *e = &tc->data[1u + 6u * i];
        obsw->pmon_warn_cdeg   = (int16_t)get_be16(&e[2]);
        obsw->pmon_danger_cdeg = (int16_t)get_be16(&e[4]);
    }
    return OBSW_OK;
}
