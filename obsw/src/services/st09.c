/*
 * ST[9] time management.
 *
 * TM[9,2] CUC time report, sent every 2^exp seconds of on-board time
 * (default every second). Data, 7 bytes:
 *   rate exponent u8 | on-board time, CUC 4 bytes coarse + 2 bytes fine
 *
 * The time field is sampled when the report is generated. On the
 * ground, each report's arrival time in UTC paired with this on-board
 * time gives one measurement of how the on-board clock relates to UTC:
 * that is time correlation (see ground/tinyground/timecorr.py).
 *
 * TC[9,1] set the time report generation rate: exponent u8, 0..6.
 */
#include "bytes.h"
#include "obsw_internal.h"

#define ST09_SERVICE     9u
#define ST09_TIME_REPORT 2u

static uint16_t period_s(uint8_t exp)
{
    return (uint16_t)(1u << exp);
}

void st09_init(obsw_t *obsw)
{
    obsw->time_rate_exp  = OBSW_TIME_RATE_DEFAULT;
    obsw->time_countdown = period_s(OBSW_TIME_RATE_DEFAULT);
}

void st09_periodic(obsw_t *obsw)
{
    uint8_t d[ST09_TIME_REPORT_LEN];
    pus_cuc_time_t now;

    obsw->time_countdown = (uint16_t)(obsw->time_countdown - 1u);
    if (obsw->time_countdown != 0u) {
        return;
    }
    obsw->time_countdown = period_s(obsw->time_rate_exp);

    /* obsw_tick only runs with a clock, so time_now is always set here. */
    now = obsw->cfg.time_now(obsw->cfg.time_ctx);
    d[0] = obsw->time_rate_exp;
    put_be32(&d[1], now.coarse);
    put_be16(&d[5], now.fine);
    (void)obsw_emit_tm(obsw, ST09_SERVICE, ST09_TIME_REPORT, 0u, d, sizeof d);
}

obsw_status_t st09_check_rate(const obsw_t *obsw, const pus_tc_t *tc)
{
    (void)obsw;
    return tc->data[0] <= OBSW_TIME_RATE_MAX ? OBSW_OK : OBSW_ERR_BAD_APP_DATA;
}

obsw_status_t st09_set_rate(obsw_t *obsw, const pus_tc_t *tc)
{
    obsw->time_rate_exp  = tc->data[0];
    obsw->time_countdown = period_s(tc->data[0]);
    return OBSW_OK;
}
