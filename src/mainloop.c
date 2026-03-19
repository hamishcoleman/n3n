/**
 * Copyright (C) Hamish Coleman
 * SPDX-License-Identifier: GPL-3.0-only
 *
 */

#include <assert.h>
#include <connslot/connslot.h>  // for slots_fdset
#include <errno.h>              // for errno
#include <n2n_typedefs.h>       // for n3n_runtime_data
#include <n3n/edge.h>           // for edge_read_proto3_udp
#include <n3n/logging.h>        // for traceEvent
#include <n3n/mainloop.h>       // for fd_info_proto
#include <n3n/metrics.h>
#include <n3n/logging.h>        // for traceEvent
#include <stddef.h>
#include <stdint.h>

#ifndef _WIN32
#include <sys/select.h>         // for select, FD_ZERO,
#include <unistd.h>             // for close
#endif

#ifdef DEBUG_MALLOC
#ifdef __GLIBC__
#include <malloc.h>             // for mallinfo2, malloc_info
#endif
#endif

#include "edge_utils.h"         // for edge_read_from_tap
#include "management.h"         // for readFromMgmtSocket
#include "minmax.h"             // for min, max
#include "pktbuf.h"
#include "portable_endian.h"    // for htobe16

#ifndef _WIN32
// Another wonderful gift from the world of POSIX compliance is not worth much
#define closesocket(a) close(a)
#endif

static struct metrics {
    uint32_t mainloop;      // mainloop_runonce() is called
    uint32_t register_fd;   // mainloop_register_fd() is called
    uint32_t unregister_fd; // mainloop_unregister_fd() is called
    uint32_t connlist_alloc;
    uint32_t connlist_free;
    uint32_t send_queue_fail;   // Attempted to send v3tcp but buffer in use
} metrics;

static struct n3n_metrics_items_llu32 metrics_items = {
    .name = "count",
    .desc = "Track the events in the lifecycle of mainloop objects",
    .name1 = "event",
    .items = {
        {
            .val1 = "mainloop",
            .offset = offsetof(struct metrics, mainloop),
        },
        {
            .val1 = "register_fd",
            .offset = offsetof(struct metrics, register_fd),
        },
        {
            .val1 = "unregister_fd",
            .offset = offsetof(struct metrics, unregister_fd),
        },
        {
            .val1 = "connlist_alloc",
            .offset = offsetof(struct metrics, connlist_alloc),
        },
        {
            .val1 = "connlist_free",
            .offset = offsetof(struct metrics, connlist_free),
        },
        {
            .val1 = "send_queue_fail",
            .offset = offsetof(struct metrics, send_queue_fail),
        },
        { },
    },
};

static char *proto_str[] = {
    [fd_info_proto_unknown] = "?",
    [fd_info_proto_tuntap] = "tuntap",
    [fd_info_proto_listen_http] = "listen_http",
    [fd_info_proto_v3udp] = "v3udp",
    [fd_info_proto_v3tcp] = "v3tcp",
    [fd_info_proto_http] = "http",
};

struct fd_info {
    int fd;                     // The file descriptor for this connection
    int stats_reads;            // The number of ready to read events
    enum fd_info_proto proto;   // What protocol to use on a read event
    int8_t connnr;              // which connlist[] is being used as buffer
};

// A static array of known file descriptors will not scale once full TCP
// connection support is added, but will work for now
#define MAX_HANDLES 16
static struct fd_info fdlist[MAX_HANDLES];
static int fdlist_next_search;

#define MAX_CONN 8
// TODO: need pools of struct conn, for each expected buffer size
static struct conn connlist[MAX_CONN];
static int connlist_next_search;

static void metrics_callback (strbuf_t **reply, const struct n3n_metrics_module *module) {
    int slot = 0;
    char buf[16];
    while(slot < MAX_HANDLES) {
        if(fdlist[slot].fd == -1) {
            slot++;
            continue;
        }

        snprintf(buf, sizeof(buf), "%i", fdlist[slot].fd);

        n3n_metrics_render_u32tags(
            reply,
            module,
            "fd_reads",
            (char *)&fdlist[slot].stats_reads - (char *)&fdlist,
            2,  // number of tag+val pairs
            "fd",
            buf,
            "proto",
            proto_str[fdlist[slot].proto]
        );
        // TODO:
        // - do we need to keep each fd lifecycle clear by tracking and
        // outputting the open timestamp?
        slot++;
    }
}

#ifdef DEBUG_MALLOC
#ifdef __GLIBC__
static struct mallinfo2 metrics_mi;

static void metrics_mallinfo2 (strbuf_t **reply, const struct n3n_metrics_module *module) {
    metrics_mi = mallinfo2();

    n3n_metrics_render_u32tags(
        reply,
        module,
        "bytes",
        offsetof(struct mallinfo2, arena),
        1,  // number of tag+val pairs
        "field",
        "arena"
    );
    n3n_metrics_render_u32tags(
        reply,
        module,
        "bytes",
        offsetof(struct mallinfo2, uordblks),
        1,  // number of tag+val pairs
        "field",
        "uordblks"
    );
    n3n_metrics_render_u32tags(
        reply,
        module,
        "bytes",
        offsetof(struct mallinfo2, fordblks),
        1,  // number of tag+val pairs
        "field",
        "fordblks"
    );
    n3n_metrics_render_u32tags(
        reply,
        module,
        "bytes",
        offsetof(struct mallinfo2, keepcost),
        1,  // number of tag+val pairs
        "field",
        "keepcost"
    );
}

static struct n3n_metrics_module metrics_module_mallinfo2 = {
    .name = "mallinfo2",
    .data = &metrics_mi,
    .cb = &metrics_mallinfo2,
    .type = n3n_metrics_type_cb,
};
#endif
#endif

static struct n3n_metrics_module metrics_module_dynamic = {
    .name = "mainloop",
    .data = &fdlist,
    .cb = &metrics_callback,
    .type = n3n_metrics_type_cb,
};

static struct n3n_metrics_module metrics_module_static = {
    .name = "mainloop",
    .data = &metrics,
    .items_llu32 = &metrics_items,
    .type = n3n_metrics_type_llu32,
};

static void connlist_init () {
    int conn = 0;
    while(conn < MAX_CONN) {
        conn_init(&connlist[conn], 4000, 1000);
        conn++;
    }
    connlist_next_search = 0;
}

static void connlist_deinit () {
    int conn = 0;
    while(conn < MAX_CONN) {
        // TODO: this crosses the layer boundaries
        free(connlist[conn].request);
        free(connlist[conn].reply_header);
        conn++;
    }
}

static int connlist_alloc (enum conn_proto proto) {
    int conn = connlist_next_search % MAX_CONN;
    int count = MAX_CONN;
    while(count) {
        if(connlist[conn].proto == CONN_PROTO_UNK) {
            connlist[conn].proto = proto;
            connlist_next_search = conn + 1;
            metrics.connlist_alloc++;
            return conn;
        }
        conn = (conn + 1) % MAX_CONN;
        count--;
    }
    return -1;
}

static void connlist_free (int connnr) {
    if(connnr > MAX_CONN) {
        // TODO: error!
        return;
    }
    connlist[connnr].fd = -1;
    connlist[connnr].proto = CONN_PROTO_UNK;
    connlist[connnr].state = CONN_EMPTY;
    connlist_next_search = connnr;
    metrics.connlist_free++;
}

// Used only to initialise the array at startup
static void fdlist_zero () {
    int slot = 0;
    while(slot < MAX_HANDLES) {
        fdlist[slot].connnr = -1;
        fdlist[slot].fd = -1;
        fdlist[slot].proto = fd_info_proto_unknown;
        slot++;
    }
    fdlist_next_search = 0;
}

static int fdlist_allocslot (int fd, enum fd_info_proto proto) {
    int slot = fdlist_next_search % MAX_HANDLES;
    int count = MAX_HANDLES;
    while(count) {
        if(fdlist[slot].fd == -1) {
            metrics.register_fd++;
            fdlist[slot].fd = fd;
            fdlist[slot].proto = proto;
            fdlist[slot].stats_reads = 0;

            if(proto == fd_info_proto_v3tcp) {
                int connnr = connlist_alloc(CONN_PROTO_BE16LEN);
                assert(connnr != -1);

                fdlist[slot].connnr = connnr;
                conn_accept(&connlist[connnr], fd, CONN_PROTO_BE16LEN);
            } else {
                fdlist[slot].connnr = -1;
            }

            fdlist_next_search = slot + 1;
            return slot;
        }
        slot = (slot + 1) % MAX_HANDLES;
        count--;
    }

    // TODO: the moment this starts to fire, we need to revamp the
    // implementation of the fdlist table
    assert(slot != -1);
    return -1;
}

static void fdlist_freefd (int fd) {
    int slot = 0;
    if(fd == -1) {
        // Cannot release an error fd!
        return;
    }
    while(slot < MAX_HANDLES) {
        if(fdlist[slot].fd != fd) {
            slot++;
            continue;
        }
        metrics.unregister_fd++;
        if(fdlist[slot].connnr != -1) {
            connlist_free(fdlist[slot].connnr);
            fdlist[slot].connnr = -1;
        }
        fdlist[slot].fd = -1;
        fdlist[slot].proto = fd_info_proto_unknown;
        fdlist_next_search = slot;
        return;
    }

    // TODO:
    // - could assert or similar
}

static int fdlist_fd_set (fd_set *rd, fd_set *wr) {
    int max_sock = 0;
    int slot = 0;
    while(slot < MAX_HANDLES) {
        if(fdlist[slot].fd == -1) {
            slot++;
            continue;
        }

        // TODO:
        // - if no empty conn, dont FD_SET on proto TCP listen

        if(fdlist[slot].connnr == -1) {
            FD_SET(fdlist[slot].fd, rd);
            max_sock = MAX(max_sock, fdlist[slot].fd);
        } else {
            if(connlist[fdlist[slot].connnr].reply_sendpos == 0) {
                // Only select for reading if we have finished previous write
                // FIXME:
                // this check assumes that the conn_write() that kicks off
                // a sending event will have made at least some progress
                FD_SET(fdlist[slot].fd, rd);
                max_sock = MAX(max_sock, fdlist[slot].fd);
            }
        }

        if(fdlist[slot].connnr == -1) {
            slot++;
            continue;
        }

        if(conn_iswriter(&connlist[fdlist[slot].connnr])) {
            FD_SET(fdlist[slot].fd, wr);
        }

        slot++;
    }
    return max_sock;
}

static void handle_fd(const time_t now, const struct fd_info info, struct n3n_runtime_data *eee) {
    switch (info.proto) {
        case fd_info_proto_unknown:
            assert(false);
            return;

        case fd_info_proto_tuntap:
            edge_read_from_tap(eee);
            return;

        case fd_info_proto_listen_http: {
            int client = accept(info.fd, NULL, 0);
            if (client == -1) {
                return;
            }

            int slotnr = fdlist_allocslot(client, fd_info_proto_http);
            if (slotnr < 0) {
                send(client, "HTTP/1.1 503 full\r\n", 19, 0);
                closesocket(client);
                return;
            }

            int connnr = connlist_alloc(CONN_PROTO_HTTP);
            if (connnr < 0) {
                send(client, "HTTP/1.1 503 full\r\n", 19, 0);
                closesocket(client);
                fdlist_freefd(client);
                return;
            }

            fdlist[slotnr].connnr = connnr;
            conn_accept(&connlist[connnr], client, CONN_PROTO_HTTP);
            return;
        }

        case fd_info_proto_v3udp: {
            struct n3n_pktbuf *pkt = n3n_pktbuf_alloc(N2N_PKT_BUF_SIZE);
            if (!pkt) {
                abort(); // allocation failure is fatal
            }
            pkt->owner = n3n_pktbuf_owner_rx_pdu;
            edge_read_proto3_udp(eee, info.fd, pkt, now);
            n3n_pktbuf_free(pkt);
            return;
        }

        case fd_info_proto_v3tcp: {
            struct conn *conn = &connlist[info.connnr];
            conn_read(conn, info.fd);

            switch (conn->state) {
                case CONN_EMPTY:
                case CONN_READING:
                    return;

                case CONN_ERROR:
                case CONN_CLOSED:
                    conn_close(conn, info.fd);
                    sb_zero(conn->request);
                    edge_read_proto3_tcp(eee, -1, NULL, -1, now);
                    return;

                case CONN_READY: {
                    uint16_t tmp;
                    memcpy(&tmp, &conn->request->str[0], sizeof(tmp));
                    size_t size = ntohs(tmp);
                    edge_read_proto3_tcp(eee, info.fd, (uint8_t *)&conn->request->str[2], size, now);

                    size_t total = (size_t)size + 2;
                    if (sb_len(conn->request) == total) {
                        sb_zero(conn->request);
                        conn->state = CONN_EMPTY;
                    } else {
                        size_t more = sb_len(conn->request) - total;
#if TRACE_ENABLED && (TRACE_LEVEL >= TRACE_DEBUG)
                        traceEvent(TRACE_DEBUG, "packet has %zu more bytes", more);
#endif
                        memmove(conn->request->str, &conn->request->str[total], more);
                        conn->request->rd_pos = 0;
                        conn->request->wr_pos = more;
                        conn->state = CONN_READING;
                    }
                    return;
                }
            }
            return;
        }

        case fd_info_proto_http: {
            struct conn *conn = &connlist[info.connnr];
            conn_read(conn, info.fd);

            switch (conn->state) {
                case CONN_EMPTY:
                case CONN_READING:
                    return;

                case CONN_READY:
                    mgmt_api_handler(eee, conn);
                    if (conn->reply_sendpos == 0) {
                        sb_zero(conn->request);
                    }
                    return;

                case CONN_ERROR:
                case CONN_CLOSED:
                    conn_close(conn, info.fd);
                    fdlist_freefd(info.fd);
                    return;
            }
            return;
        }
    }
}
/* TODO: decide if this quick helper is actually useful and needed
 * It was added to try and provide an action to do if select returns an error,
 * but it didnt end up closing connections - and the original error was traced
 * to an alloc without matching free
 */
static void fdlist_closeidle (const time_t now) {
    int slot = 0;
    // A linear scan is not ideal, but until we support things other than
    // select() it will need to suffice
    while(slot < MAX_HANDLES) {
        int fd = fdlist[slot].fd;
        if(fdlist[slot].connnr != -1) {
            int timeout = 60;
            struct conn *conn = &connlist[fdlist[slot].connnr];
            bool closed = conn_closeidle(conn, fd, now, timeout);
            if(closed) {
                fdlist_freefd(fd);
            }
        }
        slot++;
    }
}

static void fdlist_check_ready(fd_set *rd, fd_set *wr, const time_t now, struct n3n_runtime_data *eee) {
    for (int slot = 0; slot < MAX_HANDLES; slot++) {
        const int fd = fdlist[slot].fd;
        if (fd == -1) continue;

        const int connnr = fdlist[slot].connnr;
        struct conn *conn = (connnr != -1) ? &connlist[connnr] : NULL;

        /* Check for readable data */
        if (FD_ISSET(fd, rd)) {
            fdlist[slot].stats_reads++;
            handle_fd(now, fdlist[slot], eee);
        }

        /* Check for writable data */
        if (FD_ISSET(fd, wr)) {
            if (connnr == -1) {
#if TRACE_ENABLED && (TRACE_LEVEL >= TRACE_DEBUG)
                traceEvent(TRACE_DEBUG, "writer bad connnr");
#endif
                continue;
            }

            conn_write(conn, fd);

            /* Clear request buffer if write is complete */
            if (conn->reply_sendpos == 0) {
                sb_zero(conn->request);
            }
        }

        /* Close idle connections (only if connection exists) */
        if (conn) {
            // Only check idle timeout if we did I/O or periodically
            // But for simplicity and correctness, we keep per-loop check
            // (60s timeout → check is cheap)
            if (conn_closeidle(conn, fd, now, 60)) {
                fdlist_freefd(fd);
            }
        }
    }
}

#ifdef DEBUG_MALLOC
#ifdef __GLIBC__
static time_t last_mallinfo;
#endif
#endif

int mainloop_runonce(struct n3n_runtime_data *eee) {
    metrics.mainloop++;

    fd_set rd, wr;
    FD_ZERO(&rd);
    FD_ZERO(&wr);
    int maxfd = fdlist_fd_set(&rd, &wr);

    struct timeval wait_time = {
        .tv_sec = eee->sn_wait ? (SOCKET_TIMEOUT_INTERVAL_SECS / 10 + 1) : SOCKET_TIMEOUT_INTERVAL_SECS,
        .tv_usec = 0
    };

    int ready = select(maxfd + 1, &rd, &wr, NULL, &wait_time);

    if (ready == -1) {
#if TRACE_ENABLED && (TRACE_LEVEL >= TRACE_ERROR)
        traceEvent(TRACE_ERROR, "select errno=%i", errno);
#endif
        time_t now = time(NULL);
        fdlist_closeidle(now);
        return -1;
    }

    if (ready == 0) {
        // Timeout — nothing ready
        return 0;
    }

    // Only get time if we have I/O to process
    time_t now = time(NULL);
    fdlist_check_ready(&rd, &wr, now, eee);

#if defined(DEBUG_MALLOC) && defined(__GLIBC__)
    // Only enable if both debug malloc AND tracing are on
    #if TRACE_ENABLED && (TRACE_LEVEL >= TRACE_DEBUG)
    {
        static time_t last_mallinfo = 0;
        if ((now & ~0x3f) > last_mallinfo) {
            last_mallinfo = now;
            struct mallinfo2 mi = mallinfo2();
            traceEvent(TRACE_DEBUG,
                "mallinfo: arena=%i uordblks=%i, fordblks=%i, keepcost=%i",
                mi.arena, mi.uordblks, mi.fordblks, mi.keepcost);

            fprintf(stderr, "===malloc_info start===\n");
            malloc_info(0, stderr);
            fprintf(stderr, "===malloc_info end===\n");
        }
    }
    #endif
#endif

    return ready;
}

void mainloop_dump (strbuf_t **buf) {
    int i;
    sb_reprintf(buf, "i : fd(read) pr connnr\n");
    for(i=0; i<MAX_HANDLES; i++) {
        sb_reprintf(
            buf,
            "%02i: %2i(%4i) %i %i\n",
            i,
            fdlist[i].fd,
            fdlist[i].stats_reads,
            fdlist[i].proto,
            fdlist[i].connnr
        );
    }
    sb_reprintf(buf, "\n");
    for(i=0; i<MAX_CONN; i++) {
        sb_reprintf(buf,"%i: ",i);
        conn_dump(buf, &connlist[i]);
    }
}

bool mainloop_send_v3tcp (int fd, const void *buf, int bufsize) {
    // TODO:
    // - avoid the linear scan by changing the params to pass a fdlist slottnr
    //   instead of a filehandle
    int slot = 0;
    while(slot < MAX_HANDLES) {
        if(fdlist[slot].fd == fd) {
            break;
        }
        slot++;
    }
    if(fdlist[slot].fd != fd) {
        // Couldnt find this fd
        return false;
    }

    if(fdlist[slot].connnr == -1) {
        // No buffer associated with this fd
        return false;
    }

    struct conn *conn = &connlist[fdlist[slot].connnr];

    if(!conn->reply) {
        conn->reply = sb_malloc(N2N_PKT_BUF_SIZE + 2, N2N_PKT_BUF_SIZE + 2);
    } else {
        if(sb_len(conn->reply)) {
            // send buffer already in use
            // TODO:
            // - metrics!
            return false;
        }
        sb_zero(conn->reply);
    }

    uint16_t pktsize16 = htobe16(bufsize);
    sb_append(conn->reply, &pktsize16, sizeof(pktsize16));

    // TODO:
    // - avoid memcpy by using a global buffer pool and transferring ownership
    sb_append(conn->reply, buf, bufsize);

    // TODO:
    // - check bufsize for N2N_PKT_BUF_SIZE overflow

    conn_write(conn, fd);
    return true;
}

void mainloop_register_fd (int fd, enum fd_info_proto proto) {
    fdlist_allocslot(fd, proto);
}

void mainloop_unregister_fd (int fd) {
    fdlist_freefd(fd);
}

void n3n_initfuncs_mainloop () {
    connlist_init();
    fdlist_zero();
    n3n_metrics_register(&metrics_module_dynamic);
#ifdef DEBUG_MALLOC
#ifdef __GLIBC__
    n3n_metrics_register(&metrics_module_mallinfo2);
#endif
#endif
    n3n_metrics_register(&metrics_module_static);
}

void n3n_deinitfuncs_mainloop () {
    connlist_deinit();
    // TODO: once the metrics framework supports it
    // n3n_metrics_unregister(&metrics_module_dynamic);
    // n3n_metrics_unregister(&metrics_module_static);
}
