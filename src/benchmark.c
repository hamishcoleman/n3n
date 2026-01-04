/*
 * Copyright (C) Hamish Coleman
 * SPDX-License-Identifier: GPL-3.0-only
 *
 */

#include <n3n/benchmark.h>
#include <signal.h>
#include <stdbool.h>            // for true, false
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <unistd.h>

#ifdef __linux__
#include <linux/perf_event.h>
#include <sys/syscall.h>

static long
perf_event_open (
    struct perf_event_attr *hw_event,
    pid_t pid,
    int cpu,
    int group_fd,
    unsigned long flags
) {
    int ret;
    ret = syscall(SYS_perf_event_open, hw_event, pid, cpu, group_fd, flags);
    return ret;
}

#define PERF
#endif

static struct bench_item *registered_items = NULL;

void n3n_benchmark_register (struct bench_item *item) {
    item->next = registered_items;
    registered_items = item;
}

/* A do-nothing function to time the benchmark framework */
static void *bench_nop_setup (void) {
    return NULL;
}

static void bench_nop_teardown (void *data) {
    return;
}

static uint64_t bench_nop_run (void *data, uint64_t *in, uint64_t *out) {
    *in = 0;
    *out = 0;
    return 0;
}

static struct bench_item bench_nop = {
    .name = "NOP",
    .setup = bench_nop_setup,
    .run = bench_nop_run,
    .teardown = bench_nop_teardown,
};

static bool alarm_fired;

static void handler (int nr) {
    alarm_fired = true;
}

static void run_one_item (const int seconds, struct bench_item *item) {
    struct sigaction sa = {
        .sa_handler = &handler,
    };
    struct timeval tv1;
    struct timeval tv2;

#ifdef PERF
    struct perf_event_attr pe;

    memset(&pe, 0, sizeof(pe));
    pe.type = PERF_TYPE_HARDWARE;
    pe.size = sizeof(pe);
    pe.config = PERF_COUNT_HW_INSTRUCTIONS;
    pe.disabled = 1;
    pe.exclude_kernel = 1;
    pe.exclude_hv = 1;

    int fd = perf_event_open(&pe, 0, -1, -1, 0);
#endif

    void *data = item->setup();

    int loops = 0;
    alarm_fired = false;
    sigaction(SIGALRM, &sa, NULL);
    alarm(seconds);

    gettimeofday(&tv1, NULL);
#ifdef PERF
    if(fd != -1) {
        ioctl(fd, PERF_EVENT_IOC_RESET, 0);
        ioctl(fd, PERF_EVENT_IOC_ENABLE, 0);
    }
#endif

    while(!alarm_fired) {
        uint64_t in;
        uint64_t out;

        item->run(data, &in, &out);
        loops++;
        item->bytes_in += in;
        item->bytes_out += out;
    }

#ifdef PERF
    if(fd != -1) {
        ioctl(fd, PERF_EVENT_IOC_DISABLE, 0);

        uint64_t instr;
        read(fd, &instr, sizeof(instr));

        // TODO: per loop min/max/sumofsquares?

        item->instr = instr;
    }
#endif
    gettimeofday(&tv2, NULL);

    item->teardown(data);

    timersub(&tv2, &tv1, &tv1);

    item->loops = loops;
    item->sec = tv1.tv_sec;
    item->usec = tv1.tv_usec;
}

void benchmark_run (const int seconds) {
    struct bench_item *p = registered_items;

    printf("name,variant,seconds,bytes_in,bytes_out,loops,instr\n");
    while(p) {
        printf("%s,", p->name);
        if(p->variant) {
            printf("%s", p->variant);
        }
        printf(",");

        run_one_item(seconds, p);

        printf("%i.%06i,", p->sec, p->usec);
        printf("%lu,%lu,", p->bytes_in, p->bytes_out);
        printf("%lu,%lu\n", p->loops, p->instr);

        p = p->next;
    }
}

void n3n_initfuncs_benchmark () {
    n3n_benchmark_register(&bench_nop);
}
