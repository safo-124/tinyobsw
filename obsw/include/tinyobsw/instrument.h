#ifndef TINYOBSW_INSTRUMENT_H
#define TINYOBSW_INSTRUMENT_H

/*
 * A simulated science instrument: just enough physics to make
 * housekeeping telemetry worth plotting and mode logic worth testing.
 *
 * Mode transitions (anything not listed is refused):
 *
 *            +---------+   +---------+   +---------+
 *            |   OFF   |<->| STANDBY |<->| MEASURE |
 *            +---------+   +---------+   +---------+
 *                 \             ^             /
 *                  \            |            /
 *                   '-------> SAFE <--------'
 *
 *   - any mode may go to SAFE (commanded, or automatic on a fault later)
 *   - SAFE recovers only to STANDBY, so ground must look before measuring
 *   - requesting the current mode is accepted and changes nothing
 *
 * All values are integers in engineering units, as on a real
 * instrument: centi-degrees Celsius, milliamps, millivolts.
 */

#include <stdint.h>

#include "tinyobsw/status.h"

typedef enum {
    INSTR_OFF     = 0,
    INSTR_STANDBY = 1,
    INSTR_MEASURE = 2,
    INSTR_SAFE    = 3
} instr_mode_t;

#define INSTR_MODE_COUNT 4u

typedef struct {
    instr_mode_t mode;
    int16_t      temp_cdeg;     /* detector temperature, 0.01 degC */
    uint32_t     science_count; /* events counted while measuring */
    uint16_t     mode_changes;
    uint32_t     noise_state;   /* pseudo-random generator for sensor noise */
} instrument_t;

/* One housekeeping sample, as the sensors read it (with noise). */
typedef struct {
    instr_mode_t mode;
    int16_t      temp_cdeg;
    uint16_t     current_ma;
    uint16_t     voltage_mv;
    uint32_t     science_count;
    uint16_t     mode_changes;
} instr_sample_t;

/* Start in OFF at 20.00 degC. */
void instrument_init(instrument_t *instr);

/* Is raw a defined mode number? */
int instrument_mode_valid(unsigned raw);

/* Change mode. OBSW_ERR_ILLEGAL_TRANSITION if the move is not allowed. */
obsw_status_t instrument_set_mode(instrument_t *instr, instr_mode_t target);

/* Zero the science counter. */
void instrument_reset_science(instrument_t *instr);

/* Advance the physics by one second. */
void instrument_step(instrument_t *instr);

/* Read the sensors. */
void instrument_sample(instrument_t *instr, instr_sample_t *out);

#endif
