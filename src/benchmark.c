/*
 * Copyright (C) Hamish Coleman
 * SPDX-License-Identifier: GPL-3.0-only
 *
 */

#include <inttypes.h>
#include <n3n/benchmark.h>
#include <signal.h>
#include <stdbool.h>            // for true, false
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>

#ifdef __linux__
#include <linux/perf_event.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>

#define LINUX_PERF  1
#endif

#if LINUX_PERF
static long perf_event_open (
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

static int perf_setup() {
    struct perf_event_attr pe;

    memset(&pe, 0, sizeof(pe));
    pe.type = PERF_TYPE_HARDWARE;
    pe.size = sizeof(pe);
    pe.config = PERF_COUNT_HW_INSTRUCTIONS;
    pe.disabled = 1;
    pe.exclude_kernel = 1;
    pe.exclude_hv = 1;

    return perf_event_open(&pe, 0, -1, -1, 0);
}

static void perf_measure_start(int fd) {
    if(fd == -1) {
        return;
    }
    ioctl(fd, PERF_EVENT_IOC_RESET, 0);
    ioctl(fd, PERF_EVENT_IOC_ENABLE, 0);
}

static void perf_measure_collect(int fd, struct bench_item *item) {
    if(fd == -1) {
        return;
    }

    ioctl(fd, PERF_EVENT_IOC_DISABLE, 0);

    uint64_t instr;
    read(fd, &instr, sizeof(instr));
    close(fd);

    item->instr = instr;
}
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

#ifndef _WIN32
static void handler (int nr) {
    alarm_fired = true;
}
#endif

static void run_one_item (const int seconds, struct bench_item *item) {
    struct timeval tv1;
    struct timeval tv2;

#ifdef LINUX_PERF
    int fd = perf_setup();
#endif

    void *data = item->setup();

    int loops = 0;
    alarm_fired = false;

#ifndef _WIN32
    struct sigaction sa = {
        .sa_handler = &handler,
    };
    sigaction(SIGALRM, &sa, NULL);
    alarm(seconds);
#endif

    gettimeofday(&tv1, NULL);
#ifdef LINUX_PERF
    perf_measure_start(fd);
#endif

    while(!alarm_fired) {
        uint64_t in;
        uint64_t out;

        item->run(data, &in, &out);
        loops++;
        item->bytes_in += in;
        item->bytes_out += out;

#ifdef _WIN32
        gettimeofday(&tv2, NULL);
        if((tv2.tv_sec - tv1.tv_sec) >= seconds) {
            alarm_fired = true;
        }
#endif
    }

    // TODO: per loop min/max/sumofsquares?

#ifdef LINUX_PERF
    perf_measure_collect(fd, item);
#endif
    gettimeofday(&tv2, NULL);

    item->teardown(data);

#ifdef _WIN32
    // Just do a half-arsed job on windows, which matches their ability to
    // support POSIX
    tv1.tv_sec = tv2.tv_sec - tv1.tv_sec;
    tv1.tv_usec = tv2.tv_usec - tv1.tv_usec;
#else
    timersub(&tv2, &tv1, &tv1);
#endif

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
        printf("%" PRIu64 ",%" PRIu64 ",", p->bytes_in, p->bytes_out);
        printf("%" PRIu64 ",%" PRIu64 "\n", p->loops, p->instr);

        p = p->next;
    }
}

void n3n_initfuncs_benchmark () {
    n3n_benchmark_register(&bench_nop);
}
