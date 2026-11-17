/*
 * ST[3] housekeeping.
 *
 * One housekeeping structure, SID 1, describes the instrument. It is
 * sent as TM[3,25] every hk_interval_s seconds while enabled, and on
 * request. Report data (24 bytes, big-endian):
 *
 *   SID u8 | mode u8 | temperature i16 (0.01 degC) | current u16 (mA)
 *   | bus voltage u16 (mV) | science count u32 | mode changes u16
 *   | TCs accepted u16 | TCs rejected u16
 *   | monitoring status u8 (0 within, 1 above warning, 2 above danger)
 *   | flags u8 (bit 0 heater fault, bit 1 monitoring enabled)
 *   | warning limit i16 | danger limit i16 (0.01 degC)
 *
 * Telecommands (application data formats):
 *   TC[3,5]  enable periodic reports    N u8, then N SIDs (u8)
 *   TC[3,6]  disable periodic reports   N u8, then N SIDs (u8)
 *   TC[3,27] send one report now        N u8, then N SIDs (u8)
 *   TC[3,31] change report period       N u8, then N x (SID u8, seconds u16)
 */
#include "bytes.h"
#include "obsw_internal.h"

#define ST03_SERVICE       3u
#define ST03_HK_REPORT     25u
#define ST03_MAX_INTERVAL  3600u

static obsw_status_t emit_report(obsw_t *obsw, uint16_t dest_id)
{
    instr_sample_t s;
    uint8_t d[ST03_HK_LEN];

    instrument_sample(&obsw->instr, &s);
    d[0] = (uint8_t)ST03_SID_INSTRUMENT;
    d[1] = (uint8_t)s.mode;
    put_be16(&d[2], (uint16_t)s.temp_cdeg); /* two's complement on the wire */
    put_be16(&d[4], s.current_ma);
    put_be16(&d[6], s.voltage_mv);
    put_be32(&d[8], s.science_count);
    put_be16(&d[12], s.mode_changes);
    put_be16(&d[14], obsw->tc_accepted);
    put_be16(&d[16], obsw->tc_rejected);
    d[18] = obsw->pmon_status;
    d[19] = (uint8_t)((obsw->instr.heater_fault ? 0x01u : 0u) | (obsw->pmon_enabled ? 0x02u : 0u));
    put_be16(&d[20], (uint16_t)obsw->pmon_warn_cdeg);
    put_be16(&d[22], (uint16_t)obsw->pmon_danger_cdeg);
    return obsw_emit_tm(obsw, ST03_SERVICE, ST03_HK_REPORT, dest_id, d, sizeof d);
}

void st03_init(obsw_t *obsw)
{
    obsw->hk_enabled    = 1u;
    obsw->hk_interval_s = OBSW_HK_DEFAULT_S;
    obsw->hk_countdown  = OBSW_HK_DEFAULT_S;
}

void st03_periodic(obsw_t *obsw)
{
    if (!obsw->hk_enabled) {
        return;
    }
    obsw->hk_countdown = (uint16_t)(obsw->hk_countdown - 1u);
    if (obsw->hk_countdown == 0u) {
        obsw->hk_countdown = obsw->hk_interval_s;
        (void)emit_report(obsw, 0u); /* periodic reports go to the default ground ID 0 */
    }
}

/* ---- acceptance checks -------------------------------------------------- */

obsw_status_t st03_check_sid_list(const obsw_t *obsw, const pus_tc_t *tc)
{
    size_t i;
    size_t n;
    (void)obsw;

    n = tc->data[0];
    if (n == 0u || tc->data_len != 1u + n) {
        return OBSW_ERR_BAD_APP_DATA;
    }
    for (i = 0; i < n; ++i) {
        if (tc->data[1u + i] != ST03_SID_INSTRUMENT) {
            return OBSW_ERR_UNKNOWN_SID;
        }
    }
    return OBSW_OK;
}

obsw_status_t st03_check_intervals(const obsw_t *obsw, const pus_tc_t *tc)
{
    size_t i;
    size_t n;
    (void)obsw;

    n = tc->data[0];
    if (n == 0u || tc->data_len != 1u + 3u * n) {
        return OBSW_ERR_BAD_APP_DATA;
    }
    for (i = 0; i < n; ++i) {
        const uint8_t *entry = &tc->data[1u + 3u * i];
        uint16_t seconds = get_be16(&entry[1]);
        if (entry[0] != ST03_SID_INSTRUMENT) {
            return OBSW_ERR_UNKNOWN_SID;
        }
        if (seconds == 0u || seconds > ST03_MAX_INTERVAL) {
            return OBSW_ERR_BAD_APP_DATA;
        }
    }
    return OBSW_OK;
}

/* ---- execution (only reached after the checks above passed) ------------- */

obsw_status_t st03_enable(obsw_t *obsw, const pus_tc_t *tc)
{
    (void)tc;
    obsw->hk_enabled   = 1u;
    obsw->hk_countdown = obsw->hk_interval_s;
    return OBSW_OK;
}

obsw_status_t st03_disable(obsw_t *obsw, const pus_tc_t *tc)
{
    (void)tc;
    obsw->hk_enabled = 0u;
    return OBSW_OK;
}

obsw_status_t st03_one_shot(obsw_t *obsw, const pus_tc_t *tc)
{
    /* One report per SID listed; only SID 1 exists. */
    size_t i;
    for (i = 0; i < tc->data[0]; ++i) {
        obsw_status_t st = emit_report(obsw, tc->sec.source_id);
        if (st != OBSW_OK) {
            return st;
        }
    }
    return OBSW_OK;
}

obsw_status_t st03_set_intervals(obsw_t *obsw, const pus_tc_t *tc)
{
    size_t i;
    for (i = 0; i < tc->data[0]; ++i) {
        obsw->hk_interval_s = get_be16(&tc->data[1u + 3u * i + 1u]);
    }
    obsw->hk_countdown = obsw->hk_interval_s;
    return OBSW_OK;
}
