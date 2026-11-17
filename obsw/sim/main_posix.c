/*
 * obsw_sim: runs the tinyobsw core as a normal Linux program, talking
 * CCSDS/PUS packets over UDP instead of a spacecraft radio link.
 *
 *   ground station  --TC (one packet per UDP datagram)-->  obsw_sim
 *   ground station  <--TM-------------------------------  obsw_sim
 *
 * Telemetry goes back to whichever address sent the most recent TC.
 * On-board time is seconds since the simulator started, like a real
 * on-board clock that starts counting at boot.
 *
 * Usage: obsw_sim [-p port] [-a apid] [-q quiet] [-v log housekeeping too]
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

#define DEFAULT_PORT 10025
#define DEFAULT_APID 100
#define MAX_PACKET   (CCSDS_PRIMARY_HDR_LEN + 0x10000u)

typedef struct {
    int                fd;
    struct sockaddr_in ground;
    int                have_ground;
    int                quiet;
    int                verbose; /* also log every housekeeping packet */
} sim_link_t;

static volatile sig_atomic_t g_running = 1;
static struct timespec       g_boot;

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

/* obsw_time_fn: elapsed time since start as CCSDS Unsegmented Code. */
static pus_cuc_time_t onboard_time(void *ctx)
{
    struct timespec now;
    pus_cuc_time_t t;
    time_t sec;
    long nsec;

    (void)ctx;
    clock_gettime(CLOCK_MONOTONIC, &now);
    sec  = now.tv_sec - g_boot.tv_sec;
    nsec = now.tv_nsec - g_boot.tv_nsec;
    if (nsec < 0) {
        sec -= 1;
        nsec += 1000000000L;
    }
    t.coarse = (uint32_t)sec;
    t.fine   = (uint16_t)(((uint64_t)nsec << 16) / 1000000000u);
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
        !(tm.sec.service == 3 && tm.sec.subtype == 25 && link->verbose == 0)) {
        fprintf(stderr, "  TM[%u,%u] %zu bytes, seq %u\n",
                (unsigned)tm.sec.service, (unsigned)tm.sec.subtype, len,
                (unsigned)tm.primary.seq_count);
    }
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
    struct sockaddr_in addr;
    struct sigaction sa;
    int opt;

    memset(&link, 0, sizeof link);
    while ((opt = getopt(argc, argv, "p:a:qvh")) != -1) {
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
        case 'q':
            link.quiet = 1;
            break;
        case 'v':
            link.verbose = 1;
            break;
        default:
            fprintf(stderr, "usage: %s [-p port] [-a apid] [-q] [-v]\n", argv[0]);
            return opt == 'h' ? 0 : 2;
        }
    }

    clock_gettime(CLOCK_MONOTONIC, &g_boot);

    cfg.apid     = (uint16_t)apid;
    cfg.tm_sink  = udp_tm_sink;
    cfg.tm_ctx   = &link;
    cfg.time_now = onboard_time;
    cfg.time_ctx = NULL;
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
