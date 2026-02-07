/*
 * Copyright (C) Hamish Coleman
 * SPDX-License-Identifier: GPL-3.0-only
 *
 */

#include <inttypes.h>
#include <n3n/benchmark.h>
#include <n3n/hexdump.h>  // for fhexdump
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

static int _perf_setup1 (struct bench_item *item, int id, uint64_t config) {
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

/* *INDENT-OFF* */
static const uint8_t _test_data_32x16[]={
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

static const uint8_t _test_data_pearson_256[] = {
    0x40,0x09,0x5c,0xca,0x28,0x6b,0xfb,0x93,
    0x4c,0x4a,0xf7,0xc0,0x79,0xa8,0x04,0x5a,
    0xb5,0x3d,0xcf,0xb3,0xa7,0xed,0x18,0x56,
    0xb2,0xd9,0x8f,0xa8,0x2e,0xa1,0x08,0xbe,
};

static const uint8_t _test_data_lzo[] = {
    0x0d,0x00,0x01,0x02,0x03,0x04,0x05,0x06,
    0x07,0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,
    0x0f,0x20,0x00,0xbc,0x3c,0x00,0x00,0x02,
    0x0c,0x0d,0x0e,0x0f,0x00,0x01,0x02,0x03,
    0x04,0x05,0x06,0x07,0x08,0x09,0x0a,0x0b,
    0x0c,0x0d,0x0e,0x0f,0x11,0x00,0x00,
};

struct test_data {
    const int size;
    const void *data;
};

const struct test_data benchmark_test_data[] = {
    [test_data_none] = {
        .size = 0,
        .data = NULL,
    },
    [test_data_32x16] = {
        .size = sizeof(_test_data_32x16),
        .data = &_test_data_32x16,
    },
    [test_data_pearson_256] = {
        .size = sizeof(_test_data_pearson_256),
        .data = &_test_data_pearson_256,
    },
    [test_data_pearson_128] = {
        .size = 16,
        .data = &_test_data_pearson_256[16],
    },
    [test_data_lzo] = {
        .size = sizeof(_test_data_lzo),
        .data = &_test_data_lzo,
    },
};

/* A do-nothing function to time the benchmark framework */
static void *bench_nop_setup (void) {
    return NULL;
}

static void bench_nop_teardown (void *ctx) {
    return;
}

static size_t bench_nop_run (
    void *ctx,
    const void *data_in,
    const size_t data_in_size,
    size_t *in
) {
    *in = 0;
    return 0;
}

static struct bench_item bench_nop = {
    .name = "NOP",
    .setup = bench_nop_setup,
    .run = bench_nop_run,
    .teardown = bench_nop_teardown,
    .data_in = test_data_none,
    .data_out = test_data_none,
};

int generic_check (
    const struct bench_item *const p,
    const void *const got,
    const size_t got_size,
    const int level
) {
    if(got_size != benchmark_test_data[p->data_out].size) {
        // unexpected size results in an error
        return 1;
    }

    if(memcmp(benchmark_test_data[p->data_out].data, got, got_size) != 0) {
        // not matching expected result is an error
        return 1;
    }

    return 0;
}


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

    void *ctx = item->setup();
    const int input_size = benchmark_test_data[item->data_in].size;
    const void *input_data = benchmark_test_data[item->data_in].data;

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
        size_t count_in;

        size_t count_out = item->run(
            ctx,
            input_data,
            input_size,
            &count_in
        );
        loops++;
        item->bytes_in += count_in;
        item->bytes_out += count_out;

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

    item->teardown(ctx);

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
    struct bench_item *p;

    printf("name,variant,seconds,bytes_in,bytes_out,loops,cycles,instr\n");
    for(p = registered_items; p; p = p->next) {
        if(p->flags && BENCH_ITEM_CHECKONLY) {
            continue;
        }
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
    }
}

int benchmark_check_all (int level) {
    int result = 0;

    for(struct bench_item *p = registered_items; p; p = p->next) {
        if(p->data_out == test_data_none && !p->check) {
            continue;
        }

        fprintf(stderr, "%s", p->name);
        if(p->variant) {
            fprintf(stderr, ",%s", p->variant);
        }
        fprintf(stderr, ": ");

        void *ctx = p->setup();
        const int input_size = benchmark_test_data[p->data_in].size;
        const void *input_data = benchmark_test_data[p->data_in].data;

        size_t count_in;

        size_t count_out = p->run(
            ctx,
            input_data,
            input_size,
            &count_in
        );

        fprintf(stderr, "tested\n");

        if(level) {
            printf("%s: data_in id=%i\n", p->name, p->data_in);
        }

        bool checked = false;

        if(p->check) {
            result += p->check(ctx, level);
            checked = true;
        }

        if(p->get_output) {
            const void *out_data = p->get_output(ctx);

            if(p->data_out != test_data_none) {
                result += generic_check(p, out_data, count_out, level);
                checked = true;
            }

            if(level) {
                printf("%s: data_out:\n", p->name);
                fhexdump(0, out_data, count_out, stdout);
            }
        }

        // Sanity check for bad data structures
        if(!checked) {
            fprintf(stderr, "ERROR: neither check nor get_output available\n");
            exit(1);
        }

        if(level) {
            printf("\n");
        }
        p->teardown(ctx);
    }

    return result;
}

void n3n_initfuncs_benchmark () {
    n3n_benchmark_register(&bench_nop);
}
