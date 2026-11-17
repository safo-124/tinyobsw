#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "tinyobsw/ccsds.h"
#include "tinyobsw/crc16.h"
#include "tinyobsw/pus.h"

/*
 * Feeds random bytes to every parser. ASan/UBSan catch memory and
 * undefined-behaviour bugs; abort() flags a broken round-trip property:
 * anything that parses must rebuild to exactly the same bytes.
 */
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    ccsds_primary_hdr_t hdr;
    uint8_t hdr_again[CCSDS_PRIMARY_HDR_LEN];
    pus_tc_t tc;
    pus_tm_t tm;
    static uint8_t rebuilt[CCSDS_PRIMARY_HDR_LEN + 0x10000u];
    size_t n = 0;

    (void)obsw_crc16(data, size);
    (void)ccsds_check_packet(data, size, &hdr);

    if (ccsds_decode_primary(data, size, &hdr) == OBSW_OK) {
        if (ccsds_encode_primary(&hdr, hdr_again, sizeof hdr_again) != OBSW_OK ||
            memcmp(hdr_again, data, CCSDS_PRIMARY_HDR_LEN) != 0) {
            abort();
        }
    }

    if (pus_tc_parse(data, size, &tc) == OBSW_OK) {
        if (pus_tc_build(tc.primary.apid, tc.primary.seq_count, &tc.sec,
                         tc.data, tc.data_len, rebuilt, sizeof rebuilt, &n) != OBSW_OK ||
            n != size || memcmp(rebuilt, data, size) != 0) {
            abort();
        }
    }

    if (pus_tm_parse(data, size, &tm) == OBSW_OK) {
        if (pus_tm_build(tm.primary.apid, tm.primary.seq_count, &tm.sec,
                         tm.data, tm.data_len, rebuilt, sizeof rebuilt, &n) != OBSW_OK ||
            n != size || memcmp(rebuilt, data, size) != 0) {
            abort();
        }
    }
    return 0;
}
