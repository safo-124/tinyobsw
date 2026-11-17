#ifndef TINYOBSW_OBSW_H
#define TINYOBSW_OBSW_H

/*
 * The on-board software core: receives telecommands, routes them to
 * services and emits telemetry.
 *
 * It is transport-agnostic. It never touches sockets, UARTs or clocks
 * itself; the integrator passes in two callbacks:
 *   - tm_sink:  called once per finished telemetry packet
 *   - time_now: returns the current on-board time for TM timestamps
 * The same core therefore runs in unit tests, in the UDP simulator and
 * on a microcontroller. The caller owns the obsw_t (no malloc).
 *
 * Telecommand handling follows ECSS PUS-C request verification:
 *
 *   TC arrives
 *     |-- cannot read a header, or not our APID --> dropped, no report
 *     |-- fails a check (CRC, length, type, unknown service, bad data)
 *     |                                         --> TM[1,2] acceptance failed
 *     |-- accepted                              --> TM[1,1] if ack flag set
 *         |-- execution starts                  --> TM[1,3] if ack flag set
 *         |-- service runs (may emit its own TM, e.g. TM[17,2])
 *         |-- succeeded                         --> TM[1,7] if ack flag set
 *         '-- failed                            --> TM[1,8] always
 *
 * Failure reports are always sent; the TC's acknowledgement flags only
 * control the success reports.
 */

#include <stddef.h>
#include <stdint.h>

#include "tinyobsw/instrument.h"
#include "tinyobsw/pus.h"
#include "tinyobsw/status.h"

#define OBSW_TM_MAX_LEN     256u /* largest TM packet this core will build */
#define OBSW_MAX_CATCHUP    10u  /* most 1-second steps run by one obsw_tick() */
#define OBSW_HK_DEFAULT_S   1u   /* default housekeeping period, seconds */

typedef void (*obsw_tm_sink_fn)(void *ctx, const uint8_t *pkt, size_t len);
typedef pus_cuc_time_t (*obsw_time_fn)(void *ctx);

typedef struct {
    uint16_t        apid;      /* this application's APID (0..2046) */
    obsw_tm_sink_fn tm_sink;   /* may be NULL: telemetry is then discarded */
    void           *tm_ctx;
    obsw_time_fn    time_now;  /* may be NULL: TM time is then 0 */
    void           *time_ctx;
} obsw_config_t;

/* Treat the fields as private; they are public only so callers can allocate it. */
typedef struct {
    obsw_config_t cfg;
    uint16_t      tm_seq_count;      /* CCSDS sequence count for our TM */
    uint16_t      msg_counter[256];  /* PUS message type counter, per service */
    uint16_t      tc_accepted;       /* TCs that passed acceptance */
    uint16_t      tc_rejected;       /* TCs answered with TM[1,2] */

    instrument_t  instr;

    /* 1-second scheduler */
    uint32_t      last_tick_s;
    uint8_t       clock_started;

    /* ST[3] housekeeping */
    uint8_t       hk_enabled;
    uint16_t      hk_interval_s;
    uint16_t      hk_countdown;

    uint8_t       tm_buf[OBSW_TM_MAX_LEN];
} obsw_t;

/* Set up the core. APID must not be the reserved idle APID 0x7FF. */
obsw_status_t obsw_init(obsw_t *obsw, const obsw_config_t *cfg);

/*
 * Handle one received telecommand of exactly len bytes. Emits all the
 * resulting telemetry through tm_sink before returning. Returns OBSW_OK
 * if the command was accepted and completed, otherwise the reason it
 * was rejected or failed (the same code sent to ground in TM[1,2]/[1,8]).
 */
obsw_status_t obsw_handle_tc(obsw_t *obsw, const uint8_t *buf, size_t len);

/*
 * Run periodic work. Call it often (e.g. every 100 ms); it reads
 * time_now and runs one step per elapsed whole second: instrument
 * physics, then periodic housekeeping. After a long stall it runs at
 * most OBSW_MAX_CATCHUP steps and skips the rest; if the clock jumps
 * backwards it simply resynchronises. Does nothing without time_now.
 */
void obsw_tick(obsw_t *obsw);

#endif
