/*
 * ST[8] function management.
 *
 * TC[8,1] "perform a function". Application data:
 *   function ID u16, then that function's arguments.
 *
 *   ID 1  SET_MODE       mode u8 (0 OFF, 1 STANDBY, 2 MEASURE, 3 SAFE)
 *   ID 2  RESET_SCIENCE  (no arguments)
 *
 * Acceptance checks the ID, the argument length and that a requested
 * mode exists. Whether the transition is allowed depends on the mode
 * the instrument is in when the command executes, so that is checked
 * at execution: an illegal transition is reported as TM[1,8].
 */
#include "bytes.h"
#include "obsw_internal.h"

obsw_status_t st08_check(const obsw_t *obsw, const pus_tc_t *tc)
{
    uint16_t func = get_be16(tc->data);
    size_t n_args = tc->data_len - 2u;
    (void)obsw;

    switch (func) {
    case ST08_FUNC_SET_MODE:
        if (n_args != 1u || !instrument_mode_valid(tc->data[2])) {
            return OBSW_ERR_BAD_APP_DATA;
        }
        return OBSW_OK;
    case ST08_FUNC_RESET_SCIENCE:
        return n_args == 0u ? OBSW_OK : OBSW_ERR_BAD_APP_DATA;
    default:
        return OBSW_ERR_UNKNOWN_FUNCTION;
    }
}

obsw_status_t st08_perform(obsw_t *obsw, const pus_tc_t *tc)
{
    switch (get_be16(tc->data)) {
    case ST08_FUNC_SET_MODE:
        return instrument_set_mode(&obsw->instr, (instr_mode_t)tc->data[2]);
    case ST08_FUNC_RESET_SCIENCE:
        instrument_reset_science(&obsw->instr);
        return OBSW_OK;
    default:
        return OBSW_ERR_UNKNOWN_FUNCTION; /* unreachable: st08_check ran first */
    }
}
