#ifndef TINYOBSW_STATUS_H
#define TINYOBSW_STATUS_H

/*
 * Return codes shared by every tinyobsw module. OBSW_OK is always 0.
 *
 * These values are also sent to the ground as the failure code inside
 * ST[1] failure reports, so they are part of the interface: give new
 * codes new numbers and never renumber existing ones.
 */
typedef enum {
    OBSW_OK                  = 0,
    OBSW_ERR_NULL            = 1,  /* a required pointer was NULL */
    OBSW_ERR_SHORT_BUFFER    = 2,  /* buffer too small to read or write */
    OBSW_ERR_BAD_VERSION     = 3,  /* CCSDS version number is not 0 */
    OBSW_ERR_FIELD_RANGE     = 4,  /* a field does not fit its bit width */
    OBSW_ERR_LENGTH_MISMATCH = 5,  /* length field disagrees with bytes received */
    OBSW_ERR_CRC             = 6,  /* packet error control check failed */
    OBSW_ERR_WRONG_TYPE      = 7,  /* got TM where TC expected, or the reverse */
    OBSW_ERR_NO_SEC_HDR      = 8,  /* PUS packet without the secondary header flag */
    OBSW_ERR_BAD_PUS_VERSION = 9,  /* PUS version number is not 2 (PUS-C) */
    OBSW_ERR_UNSUPPORTED     = 10, /* valid CCSDS, but a feature we do not support */
    OBSW_ERR_WRONG_APID      = 11, /* TC addressed to another application */
    OBSW_ERR_ILLEGAL_SERVICE = 12, /* service type not implemented */
    OBSW_ERR_ILLEGAL_SUBTYPE = 13, /* service known, message subtype not implemented */
    OBSW_ERR_BAD_APP_DATA       = 14, /* application data has the wrong length or content */
    OBSW_ERR_ILLEGAL_TRANSITION = 15, /* instrument cannot go to that mode from this one */
    OBSW_ERR_UNKNOWN_FUNCTION   = 16, /* ST[8] function ID not defined */
    OBSW_ERR_UNKNOWN_SID        = 17  /* ST[3] housekeeping structure ID not defined */
} obsw_status_t;

#endif
