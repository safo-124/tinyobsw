#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "tinyobsw/ccsds.h"
#include "tinyobsw/crc16.h"

/*
 * Property: any header that decodes successfully must re-encode to exactly
 * the same 6 bytes. The fuzzer feeds random bytes; ASan/UBSan catch memory
 * and undefined-behaviour bugs, and abort() flags a broken round trip.
 */
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    ccsds_primary_hdr_t hdr;
    uint8_t again[CCSDS_PRIMARY_HDR_LEN];

    (void)obsw_crc16(data, size);
    (void)ccsds_check_packet(data, size, &hdr);

    if (ccsds_decode_primary(data, size, &hdr) == OBSW_OK) {
        if (ccsds_encode_primary(&hdr, again, sizeof again) != OBSW_OK) {
            abort();
        }
        if (memcmp(again, data, CCSDS_PRIMARY_HDR_LEN) != 0) {
            abort();
        }
    }
    return 0;
}
