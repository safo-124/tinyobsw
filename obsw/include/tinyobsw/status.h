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
    OBSW_ERR_CRC,             /* packet error control check failed */
    OBSW_ERR_WRONG_TYPE,      /* got TM where TC expected, or the reverse */
    OBSW_ERR_NO_SEC_HDR,      /* PUS packet without the secondary header flag */
    OBSW_ERR_BAD_PUS_VERSION, /* PUS version number is not 2 (PUS-C) */
    OBSW_ERR_UNSUPPORTED      /* valid CCSDS, but a feature we do not support */
} obsw_status_t;

#endif
