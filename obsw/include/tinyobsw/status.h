#ifndef TINYOBSW_STATUS_H
#define TINYOBSW_STATUS_H

/* Return codes shared by every tinyobsw module. OBSW_OK is always 0. */
typedef enum {
    OBSW_OK = 0,
    OBSW_ERR_NULL,            /* a required pointer was NULL */
    OBSW_ERR_SHORT_BUFFER,    /* buffer too small to read or write */
    OBSW_ERR_BAD_VERSION,     /* CCSDS version number is not 0 */
    OBSW_ERR_FIELD_RANGE,     /* a field does not fit its bit width */
    OBSW_ERR_LENGTH_MISMATCH, /* length field disagrees with bytes received */
    OBSW_ERR_CRC              /* packet error control check failed */
} obsw_status_t;

#endif
