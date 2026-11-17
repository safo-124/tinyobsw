#include "tinyobsw/instrument.h"


/* Per-mode physics: where the temperature settles and what current flows. */
static const int16_t  TARGET_TEMP_CDEG[INSTR_MODE_COUNT] = { 2000, 2500, 3800, 2200 };
static const uint16_t CURRENT_MA[INSTR_MODE_COUNT]       = {    0,  120,  450,   35 };

#define BUS_VOLTAGE_MV   28000u
#define SCIENCE_RATE     1000u  /* events per second while measuring */

/* ALLOWED[from][to] */
static const uint8_t ALLOWED[INSTR_MODE_COUNT][INSTR_MODE_COUNT] = {
    /*            OFF STBY MEAS SAFE */
    /* OFF  */  {  1,   1,   0,   1 },
    /* STBY */  {  1,   1,   1,   1 },
    /* MEAS */  {  0,   1,   1,   1 },
    /* SAFE */  {  0,   1,   0,   1 },
};

/* Small linear congruential generator: deterministic "sensor noise". */
static uint32_t next_noise(instrument_t *instr)
{
    instr->noise_state = instr->noise_state * 1664525u + 1013904223u;
    return instr->noise_state >> 16;
}

/* Uniform noise in [-span, +span]. */
static int noise(instrument_t *instr, unsigned span)
{
    return (int)(next_noise(instr) % (2u * span + 1u)) - (int)span;
}

void instrument_init(instrument_t *instr)
{
    instr->mode          = INSTR_OFF;
    instr->temp_cdeg     = TARGET_TEMP_CDEG[INSTR_OFF];
    instr->science_count = 0u;
    instr->mode_changes  = 0u;
    instr->noise_state   = 0x5EED5EEDu;
}

int instrument_mode_valid(unsigned raw)
{
    return raw < INSTR_MODE_COUNT;
}

obsw_status_t instrument_set_mode(instrument_t *instr, instr_mode_t target)
{
    if (!instrument_mode_valid((unsigned)target)) {
        return OBSW_ERR_BAD_APP_DATA;
    }
    if (!ALLOWED[instr->mode][target]) {
        return OBSW_ERR_ILLEGAL_TRANSITION;
    }
    if (target != instr->mode) {
        instr->mode = target;
        instr->mode_changes = (uint16_t)(instr->mode_changes + 1u);
    }
    return OBSW_OK;
}

void instrument_reset_science(instrument_t *instr)
{
    instr->science_count = 0u;
}

void instrument_step(instrument_t *instr)
{
    /* First-order thermal response: close 1/8 of the gap each second,
     * rounding away from zero so it reaches the target exactly. */
    int gap  = (int)TARGET_TEMP_CDEG[instr->mode] - (int)instr->temp_cdeg;
    int move = gap > 0 ? (gap + 7) / 8 : (gap - 7) / 8;
    instr->temp_cdeg = (int16_t)(instr->temp_cdeg + move);

    if (instr->mode == INSTR_MEASURE) {
        instr->science_count += SCIENCE_RATE + (next_noise(instr) & 0x3Fu);
    }
}

void instrument_sample(instrument_t *instr, instr_sample_t *out)
{
    uint16_t current = CURRENT_MA[instr->mode];

    out->mode          = instr->mode;
    out->temp_cdeg     = (int16_t)(instr->temp_cdeg + noise(instr, 3));
    out->current_ma    = current == 0u ? 0u : (uint16_t)((int)current + noise(instr, 4));
    out->voltage_mv    = (uint16_t)((int)BUS_VOLTAGE_MV + noise(instr, 20));
    out->science_count = instr->science_count;
    out->mode_changes  = instr->mode_changes;
}
