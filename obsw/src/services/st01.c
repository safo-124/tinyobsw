/*
 * ST[1] request verification.
 *
 * Every report carries the request ID: the first 4 bytes of the TC's
 * primary header (version, type, flag, APID, sequence flags, sequence
 * count). That lets ground match each report to the command it sent.
 * Failure reports add a 16-bit failure code (an obsw_status_t value).
 */
#include "bytes.h"
#include "obsw_internal.h"

#define ST01_SERVICE 1u

void st01_report_success(obsw_t *obsw, uint8_t subtype,
                         const uint8_t request_id[PUS_REQUEST_ID_LEN],
                         uint16_t dest_id)
{
    (void)obsw_emit_tm(obsw, ST01_SERVICE, subtype, dest_id,
                       request_id, PUS_REQUEST_ID_LEN);
}

void st01_report_failure(obsw_t *obsw, uint8_t subtype,
                         const uint8_t request_id[PUS_REQUEST_ID_LEN],
                         uint16_t dest_id, obsw_status_t code)
{
    uint8_t data[PUS_REQUEST_ID_LEN + 2u];
    copy_bytes(data, request_id, PUS_REQUEST_ID_LEN);
    put_be16(&data[PUS_REQUEST_ID_LEN], (uint16_t)code);
    (void)obsw_emit_tm(obsw, ST01_SERVICE, subtype, dest_id, data, sizeof data);
}
