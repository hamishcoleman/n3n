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

static int _perf_setup1 (struct bench_item *item, int id,uint64_t config) {
    struct perf_event_attr pe;

    memset(&pe, 0, sizeof(pe));
    pe.type = PERF_TYPE_HARDWARE;
    pe.size = sizeof(pe);
    pe.config = config;
    pe.disabled = 1;
    pe.exclude_kernel = 1;
    pe.exclude_hv = 1;

    pe.sample_period = 0;
    pe.read_format = PERF_FORMAT_GROUP | PERF_FORMAT_ID;

    int fd = perf_event_open(&pe, 0, -1, item->fd[0], 0);
    if(fd == -1) {
        return -1;
    }

    ioctl(fd, PERF_EVENT_IOC_ID, &item->id[id]);
    return fd;
}

static void perf_setup (struct bench_item *item) {
    item->fd[0] = -1;  // make the kernel see the first setup as leader
    item->fd[0] = _perf_setup1(item, 0,  PERF_COUNT_HW_INSTRUCTIONS);
    if(item->fd[0] == -1) {
        return;
    }

    item->fd[1] = _perf_setup1(item, 1, PERF_COUNT_HW_CPU_CYCLES);
    if(item->fd[1] == -1) {
        close(item->fd[0]);
        item->fd[0] = -1;
        return;
    }
}

static void perf_measure_start (struct bench_item *item) {
    if(item->fd[0] == -1) {
        return;
    }
    ioctl(item->fd[0], PERF_EVENT_IOC_RESET, PERF_IOC_FLAG_GROUP);
    ioctl(item->fd[0], PERF_EVENT_IOC_ENABLE, PERF_IOC_FLAG_GROUP);
}

struct read_format {
    uint64_t nr;
    struct {
        uint64_t value;
        uint64_t id;
    } values[2];
};

static void perf_measure_collect (struct bench_item *item) {
    if(item->fd[0] == -1) {
        return;
    }

    ioctl(item->fd[0], PERF_EVENT_IOC_DISABLE, PERF_IOC_FLAG_GROUP);

    struct read_format data;
    read(item->fd[0], &data, sizeof(data));

    for(int i = 0; i < data.nr; i++) {
        if(data.values[i].id == item->id[0]) {
            item->instr = data.values[i].value;
        } else if(data.values[i].id == item->id[1]) {
            item->cycles = data.values[i].value;
        } else {
            printf("Unexpected perf id\n");
            exit(1);
        }
    }

    close(item->fd[0]);
    close(item->fd[1]);
    item->fd[0] = -1;
    item->fd[1] = -1;
}
#else
static void perf_setup (struct bench_item *item) {
    return;
}
static void perf_measure_start (struct bench_item *item) {
    return;
}
static void perf_measure_collect (struct bench_item *item) {
    return;
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

    perf_setup(item);

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
    perf_measure_start(item);

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

    perf_measure_collect(item);
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

void benchmark_run_all (const int seconds) {
    struct bench_item *p = registered_items;

    printf("name,variant,seconds,bytes_in,bytes_out,loops,cycles,instr\n");
    while(p) {
        printf("%s,", p->name);
        if(p->variant) {
            printf("%s", p->variant);
        }
        printf(",");

        run_one_item(seconds, p);

        printf("%i.%06i,", p->sec, p->usec);
        printf("%" PRIu64 ",%" PRIu64 ",", p->bytes_in, p->bytes_out);
        printf(
            "%" PRIu64 ",%" PRIu64 ",%" PRIu64 "\n",
            p->loops,
            p->cycles,
            p->instr
        );

        p = p->next;
    }
}

/* *INDENT-OFF* */
static uint8_t test_data[]={
  0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15, 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,
  0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15, 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,
  0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15, 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,
  0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15, 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,
  0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15, 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,
  0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15, 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,
  0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15, 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,
  0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15, 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,
  0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15, 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,
  0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15, 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,
  0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15, 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,
  0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15, 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,
  0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15, 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,
  0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15, 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,
  0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15, 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,
  0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15, 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15
};
/* *INDENT-ON* */

struct test_data benchmark_test_data[TEST_DATA_COUNT] = {
    [TEST_DATA_32x16] = {
        .size = sizeof(test_data),
        .data = &test_data,
    },
};

void n3n_initfuncs_benchmark () {
    n3n_benchmark_register(&bench_nop);
}
