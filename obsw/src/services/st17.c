/*
 * ST[17] test service.
 *
 * TC[17,1] "are you alive?" -> TM[17,2] "alive", sent back to whoever asked.
 * Ground uses it to check the link and that the software is responding.
 */
#include "obsw_internal.h"

#define ST17_SERVICE      17u
#define ST17_ALIVE_REPORT 2u

obsw_status_t st17_are_you_alive(obsw_t *obsw, const pus_tc_t *tc)
{
    return obsw_emit_tm(obsw, ST17_SERVICE, ST17_ALIVE_REPORT,
                        tc->sec.source_id, NULL, 0u);
}
