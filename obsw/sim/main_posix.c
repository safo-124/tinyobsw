/*
 * obsw_sim: runs the tinyobsw core as a normal Linux program, talking
 * CCSDS/PUS packets over UDP instead of a spacecraft radio link.
 *
 *   ground station  --TC (one packet per UDP datagram)-->  obsw_sim
 *   ground station  <--TM-------------------------------  obsw_sim
 *
 * Telemetry goes back to whichever address sent the most recent TC.
 *
 * The on-board clock is a simulated free-running oscillator. It starts
 * at 0 at boot and runs fast by
 *
 *     rate error (ppm) = base + tempco x (detector temperature - 25 degC)
 *
 * like a real uncompensated crystal, whose frequency depends on its
 * temperature. Defaults: base 250 ppm, tempco 10 ppm/degC, so a heater
 * fault also makes the clock drift faster. The ground estimates this
 * drift from ST[9] time reports (ground/tinyground/timecorr.py).
 *
 * Usage: obsw_sim [-p port] [-a apid] [-d base_ppm] [-k tempco_ppm_per_degC]
 *                 [-q quiet] [-v log housekeeping too]
 */
#define _POSIX_C_SOURCE 200809L

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "tinyobsw/obsw.h"

#define DEFAULT_PORT       10025
#define DEFAULT_APID       100
#define DEFAULT_BASE_PPM   250.0
#define DEFAULT_TEMPCO_PPM 10.0
#define MAX_PACKET   (CCSDS_PRIMARY_HDR_LEN + 0x10000u)

typedef struct {
    int                fd;
    struct sockaddr_in ground;
    int                have_ground;
    int                quiet;
    int                verbose; /* also log every housekeeping packet */
} sim_link_t;

/* The simulated oscillator behind the on-board clock. */
typedef struct {
    struct timespec last;       /* real time of the previous reading */
    double          obt;        /* on-board time, seconds since boot */
    double          base_ppm;
    double          tempco_ppm; /* per degC away from 25 degC */
    const obsw_t   *core;       /* to read the detector temperature */
} sim_clock_t;

static volatile sig_atomic_t g_running = 1;

static void on_signal(int sig)
{
    (void)sig;
    g_running = 0;
}

static const char *status_name(obsw_status_t st)
{
    switch (st) {
    case OBSW_OK:                  return "OK";
    case OBSW_ERR_NULL:            return "NULL";
    case OBSW_ERR_SHORT_BUFFER:    return "SHORT_BUFFER";
    case OBSW_ERR_BAD_VERSION:     return "BAD_VERSION";
    case OBSW_ERR_FIELD_RANGE:     return "FIELD_RANGE";
    case OBSW_ERR_LENGTH_MISMATCH: return "LENGTH_MISMATCH";
    case OBSW_ERR_CRC:             return "CRC";
    case OBSW_ERR_WRONG_TYPE:      return "WRONG_TYPE";
    case OBSW_ERR_NO_SEC_HDR:      return "NO_SEC_HDR";
    case OBSW_ERR_BAD_PUS_VERSION: return "BAD_PUS_VERSION";
    case OBSW_ERR_UNSUPPORTED:     return "UNSUPPORTED";
    case OBSW_ERR_WRONG_APID:      return "WRONG_APID";
    case OBSW_ERR_ILLEGAL_SERVICE: return "ILLEGAL_SERVICE";
    case OBSW_ERR_ILLEGAL_SUBTYPE: return "ILLEGAL_SUBTYPE";
    case OBSW_ERR_BAD_APP_DATA:    return "BAD_APP_DATA";
    case OBSW_ERR_ILLEGAL_TRANSITION: return "ILLEGAL_TRANSITION";
    case OBSW_ERR_UNKNOWN_FUNCTION:   return "UNKNOWN_FUNCTION";
    case OBSW_ERR_UNKNOWN_SID:        return "UNKNOWN_SID";
    case OBSW_ERR_UNKNOWN_PMON:       return "UNKNOWN_PMON";
    }
    return "UNKNOWN";
}

static double clock_rate_ppm(const sim_clock_t *clk)
{
    double temp_c = clk->core->instr.temp_cdeg / 100.0;
    return clk->base_ppm + clk->tempco_ppm * (temp_c - 25.0);
}

/* obsw_time_fn: advance the oscillator to now and read it as CUC. */
static pus_cuc_time_t onboard_time(void *ctx)
{
    sim_clock_t *clk = (sim_clock_t *)ctx;
    struct timespec now;
    pus_cuc_time_t t;
    double elapsed;
    double whole;

    clock_gettime(CLOCK_MONOTONIC, &now);
    elapsed = (double)(now.tv_sec - clk->last.tv_sec) +
              (double)(now.tv_nsec - clk->last.tv_nsec) / 1e9;
    clk->last = now;
    clk->obt += elapsed * (1.0 + clock_rate_ppm(clk) * 1e-6);

    whole    = (double)(uint32_t)clk->obt;
    t.coarse = (uint32_t)clk->obt;
    t.fine   = (uint16_t)((clk->obt - whole) * 65536.0);
    return t;
}

/* obsw_tm_sink_fn: send each TM packet to the ground as one datagram. */
static void udp_tm_sink(void *ctx, const uint8_t *pkt, size_t len)
{
    sim_link_t *link = (sim_link_t *)ctx;
    pus_tm_t tm;

    if (!link->have_ground) {
        return;
    }
    if (sendto(link->fd, pkt, len, 0, (const struct sockaddr *)&link->ground,
               sizeof link->ground) < 0) {
        perror("sendto");
        return;
    }
    if (!link->quiet && pus_tm_parse(pkt, len, &tm) == OBSW_OK &&
        (link->verbose || !((tm.sec.service == 3 && tm.sec.subtype == 25) ||
                            (tm.sec.service == 9 && tm.sec.subtype == 2)))) {
        fprintf(stderr, "  TM[%u,%u] %zu bytes, seq %u\n",
                (unsigned)tm.sec.service, (unsigned)tm.sec.subtype, len,
                (unsigned)tm.primary.seq_count);
    }
}

static int parse_double(const char *s, double lo, double hi, double *out)
{
    char *end = NULL;
    double v;
    errno = 0;
    v = strtod(s, &end);
    if (errno != 0 || end == s || *end != '\0' || v < lo || v > hi) {
        return -1;
    }
    *out = v;
    return 0;
}

static int parse_uint(const char *s, unsigned long max, unsigned long *out)
{
    char *end = NULL;
    unsigned long v;
    errno = 0;
    v = strtoul(s, &end, 0); /* accepts decimal or 0x-prefixed hex */
    if (errno != 0 || end == s || *end != '\0' || v > max) {
        return -1;
    }
    *out = v;
    return 0;
}

int main(int argc, char **argv)
{
    unsigned long port = DEFAULT_PORT;
    unsigned long apid = DEFAULT_APID;
    static uint8_t rx[MAX_PACKET];
    static obsw_t core;
    obsw_config_t cfg;
    sim_link_t link;
    sim_clock_t clk;
    struct sockaddr_in addr;
    struct sigaction sa;
    int opt;

    memset(&link, 0, sizeof link);
    memset(&clk, 0, sizeof clk);
    clk.base_ppm   = DEFAULT_BASE_PPM;
    clk.tempco_ppm = DEFAULT_TEMPCO_PPM;
    clk.core       = &core;
    while ((opt = getopt(argc, argv, "p:a:d:k:qvh")) != -1) {
        switch (opt) {
        case 'p':
            if (parse_uint(optarg, 65535, &port) != 0 || port == 0) {
                fprintf(stderr, "bad port: %s\n", optarg);
                return 2;
            }
            break;
        case 'a':
            if (parse_uint(optarg, CCSDS_APID_IDLE - 1u, &apid) != 0) {
                fprintf(stderr, "bad APID: %s (0..2046)\n", optarg);
                return 2;
            }
            break;
        case 'd':
            if (parse_double(optarg, -10000.0, 10000.0, &clk.base_ppm) != 0) {
                fprintf(stderr, "bad base drift: %s (ppm, -10000..10000)\n", optarg);
                return 2;
            }
            break;
        case 'k':
            if (parse_double(optarg, -1000.0, 1000.0, &clk.tempco_ppm) != 0) {
                fprintf(stderr, "bad temperature coefficient: %s (ppm/degC)\n", optarg);
                return 2;
            }
            break;
        case 'q':
            link.quiet = 1;
            break;
        case 'v':
            link.verbose = 1;
            break;
        default:
            fprintf(stderr, "usage: %s [-p port] [-a apid] [-d base_ppm] [-k tempco] [-q] [-v]\n", argv[0]);
            return opt == 'h' ? 0 : 2;
        }
    }

    clock_gettime(CLOCK_MONOTONIC, &clk.last);

    cfg.apid     = (uint16_t)apid;
    cfg.tm_sink  = udp_tm_sink;
    cfg.tm_ctx   = &link;
    cfg.time_now = onboard_time;
    cfg.time_ctx = &clk;
    if (obsw_init(&core, &cfg) != OBSW_OK) {
        fprintf(stderr, "obsw_init failed\n");
        return 1;
    }

    link.fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (link.fd < 0) {
        perror("socket");
        return 1;
    }
    memset(&addr, 0, sizeof addr);
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons((uint16_t)port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK); /* local machine only */
    if (bind(link.fd, (struct sockaddr *)&addr, sizeof addr) != 0) {
        perror("bind");
        close(link.fd);
        return 1;
    }

    /* Ctrl+C or kill stops the loop cleanly instead of killing mid-packet. */
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_signal;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    fprintf(stderr, "obsw_sim: APID %lu, listening for TCs on 127.0.0.1:%lu\n", apid, port);
    fprintf(stderr, "obsw_sim: on-board clock runs %+.0f ppm %+.1f ppm/degC away from 25 degC\n",
            clk.base_ppm, clk.tempco_ppm);

    while (g_running) {
        struct pollfd pfd;
        struct sockaddr_in from;
        socklen_t from_len = sizeof from;
        ssize_t n;
        obsw_status_t st;
        int ready;

        pfd.fd      = link.fd;
        pfd.events  = POLLIN;
        pfd.revents = 0;
        /* Wake up at least every 100 ms to run the 1-second scheduler. */
        ready = poll(&pfd, 1, 100);
        obsw_tick(&core);
        if (ready < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("poll");
            break;
        }
        if (ready == 0) {
            continue;
        }

        n = recvfrom(link.fd, rx, sizeof rx, 0, (struct sockaddr *)&from, &from_len);
        if (n < 0) {
            if (errno != EINTR) {
                perror("recvfrom");
            }
            continue;
        }
        link.ground      = from;
        link.have_ground = 1;
        if (!link.quiet) {
            fprintf(stderr, "TC %zd bytes from %s:%u\n", n,
                    inet_ntoa(from.sin_addr), (unsigned)ntohs(from.sin_port));
        }
        st = obsw_handle_tc(&core, rx, (size_t)n);
        if (!link.quiet) {
            fprintf(stderr, "  -> %s (%d)\n", status_name(st), (int)st);
        }
    }

    fprintf(stderr, "obsw_sim: stopped\n");
    close(link.fd);
    return 0;
}
