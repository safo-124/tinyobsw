/*
 * ST[5] event reporting.
 *
 * TM[5,1] informative, TM[5,2] low, TM[5,3] medium, TM[5,4] high severity.
 * Data: event ID u16, then up to ST05_MAX_AUX bytes of auxiliary data.
 * Event IDs and their auxiliary data are listed in obsw_internal.h.
 */
#include "bytes.h"
#include "obsw_internal.h"

#define ST05_SERVICE 5u

void st05_report(obsw_t *obsw, uint8_t severity, uint16_t event_id,
                 const uint8_t *aux, size_t aux_len)
{
    uint8_t d[2u + ST05_MAX_AUX];
    if (aux_len > ST05_MAX_AUX) {
        aux_len = ST05_MAX_AUX;
    }
    put_be16(d, event_id);
    copy_bytes(&d[2], aux, aux_len);
    (void)obsw_emit_tm(obsw, ST05_SERVICE, severity, 0u, d, 2u + aux_len);
}
