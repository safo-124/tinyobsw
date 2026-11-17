#include <stddef.h>
#include <stdint.h>
#include "tinyobsw/crc16.h"

/* Swap in your packet decoder once it exists. */
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    (void)obsw_crc16(data, size);
    return 0;
}
