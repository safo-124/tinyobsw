#ifndef TINYOBSW_OBSW_INTERNAL_H
#define TINYOBSW_OBSW_INTERNAL_H

/* Private interfaces between the core and the service implementations. */

#include "tinyobsw/obsw.h"

#define PUS_REQUEST_ID_LEN 4u /* first 4 bytes of the TC primary header */

/*
 * A TC check runs during acceptance: it validates the application data
 * and may look at state, but must not change anything. Failure -> TM[1,2].
 * A TC handler runs after acceptance and does the work. Failure -> TM[1,8].
 */
typedef obsw_status_t (*obsw_tc_check_fn)(const obsw_t *obsw, const pus_tc_t *tc);
typedef obsw_status_t (*obsw_tc_handler_fn)(obsw_t *obsw, const pus_tc_t *tc);

/*
 * Build a TM packet with the given service data and pass it to the sink.
 * Fails only if data_len does not fit in OBSW_TM_MAX_LEN.
 */
obsw_status_t obsw_emit_tm(obsw_t *obsw, uint8_t service, uint8_t subtype,
                  uint16_t dest_id, const uint8_t *data, size_t data_len);

/* ST[1] request verification (src/services/st01.c). */
void st01_report_success(obsw_t *obsw, uint8_t subtype,
                         const uint8_t request_id[PUS_REQUEST_ID_LEN],
                         uint16_t dest_id);
void st01_report_failure(obsw_t *obsw, uint8_t subtype,
                         const uint8_t request_id[PUS_REQUEST_ID_LEN],
                         uint16_t dest_id, obsw_status_t code);

#define ST01_ACCEPT_OK     1u
#define ST01_ACCEPT_FAIL   2u
#define ST01_START_OK      3u
#define ST01_START_FAIL    4u
#define ST01_COMPLETE_OK   7u
#define ST01_COMPLETE_FAIL 8u

/* ST[3] housekeeping (src/services/st03.c). */
#define ST03_SID_INSTRUMENT 1u
#define ST03_HK_LEN         24u /* bytes of data in TM[3,25] for SID 1 */
obsw_status_t st03_check_sid_list(const obsw_t *obsw, const pus_tc_t *tc);
obsw_status_t st03_check_intervals(const obsw_t *obsw, const pus_tc_t *tc);
obsw_status_t st03_enable(obsw_t *obsw, const pus_tc_t *tc);
obsw_status_t st03_disable(obsw_t *obsw, const pus_tc_t *tc);
obsw_status_t st03_one_shot(obsw_t *obsw, const pus_tc_t *tc);
obsw_status_t st03_set_intervals(obsw_t *obsw, const pus_tc_t *tc);
void          st03_init(obsw_t *obsw);
void          st03_periodic(obsw_t *obsw);

/* ST[8] function management (src/services/st08.c). */
#define ST08_FUNC_SET_MODE       1u /* arg: mode (1 byte) */
#define ST08_FUNC_RESET_SCIENCE  2u /* no args */
#define ST08_FUNC_SIM_HEATER_FAULT 100u /* simulation only: heater stuck on */
#define ST08_FUNC_SIM_CLEAR_FAULT  101u /* simulation only: repair the heater */
obsw_status_t st08_check(const obsw_t *obsw, const pus_tc_t *tc);
obsw_status_t st08_perform(obsw_t *obsw, const pus_tc_t *tc);

/* ST[5] event reporting (src/services/st05.c). Subtype = severity. */
#define ST05_INFO    1u
#define ST05_LOW     2u
#define ST05_MEDIUM  3u
#define ST05_HIGH    4u
#define ST05_MAX_AUX 8u

#define EV_TEMP_WARNING    1u /* LOW:    above warning limit;   aux temperature i16 */
#define EV_TEMP_DANGER     2u /* HIGH:   above danger limit;    aux temperature i16 */
#define EV_TEMP_NOMINAL    3u /* INFO:   back within limits;    aux temperature i16 */
#define EV_AUTONOMOUS_SAFE 4u /* MEDIUM: put in SAFE on board;  aux previous mode u8 */

void st05_report(obsw_t *obsw, uint8_t severity, uint16_t event_id,
                 const uint8_t *aux, size_t aux_len);

/* ST[9] time management (src/services/st09.c). */
#define ST09_TIME_REPORT_LEN 7u /* rate exponent u8 + CUC time 6 bytes */
void          st09_init(obsw_t *obsw);
void          st09_periodic(obsw_t *obsw);
obsw_status_t st09_check_rate(const obsw_t *obsw, const pus_tc_t *tc);
obsw_status_t st09_set_rate(obsw_t *obsw, const pus_tc_t *tc);

/* ST[12] on-board monitoring (src/services/st12.c). */
#define ST12_TRANSITION_LEN 10u
void          st12_init(obsw_t *obsw);
void          st12_check(obsw_t *obsw);
obsw_status_t st12_check_ids(const obsw_t *obsw, const pus_tc_t *tc);
obsw_status_t st12_check_limits(const obsw_t *obsw, const pus_tc_t *tc);
obsw_status_t st12_enable(obsw_t *obsw, const pus_tc_t *tc);
obsw_status_t st12_disable(obsw_t *obsw, const pus_tc_t *tc);
obsw_status_t st12_set_limits(obsw_t *obsw, const pus_tc_t *tc);

/* ST[17] test service (src/services/st17.c). */
obsw_status_t st17_are_you_alive(obsw_t *obsw, const pus_tc_t *tc);

#endif
