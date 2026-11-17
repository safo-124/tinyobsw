#include "tinyobsw/obsw.h"

#include "bytes.h"
#include "obsw_internal.h"

/* ------------------------------------------------------------------ */
/* Routing table: every telecommand this software accepts.            */
/* To add a command, write its handler and add one line here.         */
/* ------------------------------------------------------------------ */

typedef struct {
    uint8_t            service;
    uint8_t            subtype;
    uint16_t           min_data;  /* application data length limits, */
    uint16_t           max_data;  /* checked before the TC is accepted */
    obsw_tc_check_fn   check;     /* extra acceptance checks, may be NULL */
    obsw_tc_handler_fn handler;
} tc_route_t;

static const tc_route_t ROUTES[] = {
    /* service, subtype, min, max, check,                handler */
    {  3,  5,  2,   9, st03_check_sid_list,  st03_enable        }, /* enable periodic HK */
    {  3,  6,  2,   9, st03_check_sid_list,  st03_disable       }, /* disable periodic HK */
    {  3, 27,  2,   9, st03_check_sid_list,  st03_one_shot      }, /* one-shot HK report */
    {  3, 31,  4,  25, st03_check_intervals, st03_set_intervals }, /* change HK period */
    {  8,  1,  2,  16, st08_check,           st08_perform       }, /* perform a function */
    { 17,  1,  0,   0, NULL,                 st17_are_you_alive }, /* are you alive? */
};

#define N_ROUTES (sizeof ROUTES / sizeof ROUTES[0])

/*
 * Acceptance checks on a TC that already parsed cleanly: is the
 * service/subtype implemented, is the data the right length, and does
 * the service-specific check pass?
 */
static obsw_status_t find_route(const obsw_t *obsw, const pus_tc_t *tc,
                                const tc_route_t **out)
{
    size_t i;
    int service_known = 0;

    for (i = 0; i < N_ROUTES; ++i) {
        if (ROUTES[i].service != tc->sec.service) {
            continue;
        }
        service_known = 1;
        if (ROUTES[i].subtype == tc->sec.subtype) {
            if (tc->data_len < ROUTES[i].min_data ||
                tc->data_len > ROUTES[i].max_data) {
                return OBSW_ERR_BAD_APP_DATA;
            }
            if (ROUTES[i].check != NULL) {
                obsw_status_t st = ROUTES[i].check(obsw, tc);
                if (st != OBSW_OK) {
                    return st;
                }
            }
            *out = &ROUTES[i];
            return OBSW_OK;
        }
    }
    return service_known ? OBSW_ERR_ILLEGAL_SUBTYPE : OBSW_ERR_ILLEGAL_SERVICE;
}

/* ------------------------------------------------------------------ */
/* Public API                                                         */
/* ------------------------------------------------------------------ */

obsw_status_t obsw_init(obsw_t *obsw, const obsw_config_t *cfg)
{
    size_t i;

    if (obsw == NULL || cfg == NULL) {
        return OBSW_ERR_NULL;
    }
    if (cfg->apid >= CCSDS_APID_IDLE) {
        return OBSW_ERR_FIELD_RANGE;
    }
    obsw->cfg = *cfg;
    obsw->tm_seq_count = 0u;
    for (i = 0; i < sizeof obsw->msg_counter / sizeof obsw->msg_counter[0]; ++i) {
        obsw->msg_counter[i] = 0u;
    }
    obsw->tc_accepted   = 0u;
    obsw->tc_rejected   = 0u;
    obsw->last_tick_s   = 0u;
    obsw->clock_started = 0u;
    instrument_init(&obsw->instr);
    st03_init(obsw);
    return OBSW_OK;
}

obsw_status_t obsw_handle_tc(obsw_t *obsw, const uint8_t *buf, size_t len)
{
    ccsds_primary_hdr_t hdr;
    uint8_t request_id[PUS_REQUEST_ID_LEN];
    uint16_t reply_to = 0u; /* unknown until the secondary header is trusted */
    const tc_route_t *route = NULL;
    pus_tc_t tc;
    obsw_status_t st;

    if (obsw == NULL || buf == NULL) {
        return OBSW_ERR_NULL;
    }

    /* Without a readable header there is no request ID to quote: drop silently. */
    st = ccsds_decode_primary(buf, len, &hdr);
    if (st != OBSW_OK) {
        return st;
    }
    /* Addressed to another application: not ours to answer. */
    if (hdr.apid != obsw->cfg.apid) {
        return OBSW_ERR_WRONG_APID;
    }
    copy_bytes(request_id, buf, PUS_REQUEST_ID_LEN);

    /* Acceptance: packet checks, then routing checks. */
    st = pus_tc_parse(buf, len, &tc);
    if (st == OBSW_OK) {
        reply_to = tc.sec.source_id;
        st = find_route(obsw, &tc, &route);
    }
    if (st != OBSW_OK) {
        obsw->tc_rejected = (uint16_t)(obsw->tc_rejected + 1u);
        st01_report_failure(obsw, ST01_ACCEPT_FAIL, request_id, reply_to, st);
        return st;
    }
    obsw->tc_accepted = (uint16_t)(obsw->tc_accepted + 1u);
    if (tc.sec.ack_flags & PUS_ACK_ACCEPTANCE) {
        st01_report_success(obsw, ST01_ACCEPT_OK, request_id, reply_to);
    }

    /* Execution. */
    if (tc.sec.ack_flags & PUS_ACK_START) {
        st01_report_success(obsw, ST01_START_OK, request_id, reply_to);
    }
    st = route->handler(obsw, &tc);
    if (st != OBSW_OK) {
        st01_report_failure(obsw, ST01_COMPLETE_FAIL, request_id, reply_to, st);
        return st;
    }
    if (tc.sec.ack_flags & PUS_ACK_COMPLETION) {
        st01_report_success(obsw, ST01_COMPLETE_OK, request_id, reply_to);
    }
    return OBSW_OK;
}

void obsw_tick(obsw_t *obsw)
{
    uint32_t now;
    uint32_t behind;

    if (obsw == NULL || obsw->cfg.time_now == NULL) {
        return;
    }
    now = obsw->cfg.time_now(obsw->cfg.time_ctx).coarse;
    if (!obsw->clock_started) {
        obsw->last_tick_s   = now;
        obsw->clock_started = 1u;
        return;
    }

    behind = now - obsw->last_tick_s; /* unsigned: wraps correctly */
    if (behind > 0x80000000u) {
        obsw->last_tick_s = now; /* clock went backwards: resynchronise */
        return;
    }
    if (behind > OBSW_MAX_CATCHUP) {
        /* Long stall: skip the missed seconds rather than flood the link. */
        obsw->last_tick_s = now - OBSW_MAX_CATCHUP;
        behind = OBSW_MAX_CATCHUP;
    }
    while (behind > 0u) {
        obsw->last_tick_s++;
        instrument_step(&obsw->instr);
        st03_periodic(obsw);
        behind--;
    }
}

/* ------------------------------------------------------------------ */
/* Telemetry output, shared by all services                           */
/* ------------------------------------------------------------------ */

obsw_status_t obsw_emit_tm(obsw_t *obsw, uint8_t service, uint8_t subtype,
                           uint16_t dest_id, const uint8_t *data, size_t data_len)
{
    pus_tm_sec_hdr_t sec;
    size_t n = 0u;
    obsw_status_t st;

    sec.time_ref_status = 0u;
    sec.service         = service;
    sec.subtype         = subtype;
    sec.msg_counter     = obsw->msg_counter[service];
    sec.dest_id         = dest_id;
    if (obsw->cfg.time_now != NULL) {
        sec.time = obsw->cfg.time_now(obsw->cfg.time_ctx);
    } else {
        sec.time.coarse = 0u;
        sec.time.fine   = 0u;
    }

    st = pus_tm_build(obsw->cfg.apid, obsw->tm_seq_count, &sec, data, data_len,
                      obsw->tm_buf, sizeof obsw->tm_buf, &n);
    if (st != OBSW_OK) {
        return st;
    }
    obsw->tm_seq_count = ccsds_next_seq_count(obsw->tm_seq_count);
    obsw->msg_counter[service] = (uint16_t)(obsw->msg_counter[service] + 1u);

    if (obsw->cfg.tm_sink != NULL) {
        obsw->cfg.tm_sink(obsw->cfg.tm_ctx, obsw->tm_buf, n);
    }
    return OBSW_OK;
}
