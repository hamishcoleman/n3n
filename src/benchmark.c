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

static const uint8_t _test_data_speck[] = {
    0x86,0xfa,0xe8,0x7a,0x5b,0xbc,0xa9,0xb7,
    0x6e,0xc7,0xdb,0xdf,0xff,0xfb,0x56,0x24,
    0xe7,0xdc,0x11,0xc1,0xcf,0x25,0x40,0xbe,
    0x47,0xba,0x08,0xca,0x1c,0x83,0xa1,0x68,
    0xf6,0xc8,0x33,0xa6,0xef,0x64,0x2a,0x11,
    0x0b,0x23,0x28,0x9d,0x69,0xb8,0xeb,0x3f,
    0x95,0xd8,0xec,0x67,0x7a,0xa6,0x9b,0xd6,
    0xf0,0xd8,0x9a,0x9f,0xe0,0x50,0xdb,0xb7,
    0x91,0x3c,0xbf,0x67,0x02,0x68,0xf4,0xe7,
    0xd1,0xf7,0x99,0xf4,0xe4,0x7d,0x96,0x12,
    0x31,0x4b,0x34,0x7d,0xc9,0xdc,0x39,0x30,
    0x3c,0xb1,0x6e,0x27,0xdd,0x0a,0x45,0xc5,
    0x54,0x5c,0x12,0xc2,0x5e,0x97,0x95,0xd4,
    0x81,0x21,0xf7,0xfb,0xb1,0x93,0xab,0x9d,
    0xb5,0x2c,0xda,0x43,0x2e,0x08,0x3b,0x37,
    0xa8,0xc9,0x24,0xbc,0x4d,0xef,0x32,0x88,
    0x62,0xa2,0xad,0x59,0x33,0xdd,0xfe,0x3a,
    0xe0,0x9a,0xf6,0x3d,0x37,0x63,0x28,0x36,
    0x86,0xeb,0x16,0x7e,0x95,0xb2,0xab,0xca,
    0x5c,0x3e,0xd6,0x70,0x67,0x4d,0xa3,0xaf,
    0x0e,0xbb,0x5b,0x0d,0xd3,0x55,0x72,0x05,
    0x15,0xf3,0x28,0x31,0x62,0xdb,0x41,0xa3,
    0x0e,0x02,0x54,0xf3,0x62,0xf1,0xb0,0x6f,
    0x25,0x64,0x38,0x69,0xbb,0x87,0x71,0x84,
    0x14,0xcd,0x7f,0x34,0xcd,0x4f,0x7d,0xf5,
    0xdc,0x36,0x5d,0xe5,0xf3,0xd1,0xee,0x6d,
    0x40,0x20,0xe1,0xac,0x32,0xfa,0xe3,0xa5,
    0xe6,0x45,0xcc,0x36,0x3e,0xf7,0x98,0xd1,
    0xd1,0x40,0xd7,0xa0,0xb9,0xf9,0x15,0xc3,
    0xf2,0x97,0x06,0x6a,0x38,0xca,0xc9,0x61,
    0xe4,0x95,0xeb,0x1d,0xa8,0x89,0xbd,0x3d,
    0x22,0x80,0xa6,0x8f,0x22,0xd2,0x6f,0x3d,
    0xdf,0x96,0x9d,0x76,0x64,0xef,0xfc,0x78,
    0xb6,0x3c,0xd3,0xbc,0x6c,0x21,0xdb,0x6a,
    0xa5,0x8c,0xfd,0x13,0x73,0x84,0xa2,0x61,
    0xa8,0x1c,0xea,0xb8,0x39,0x4a,0xda,0xc2,
    0x50,0x9f,0x96,0x20,0x61,0x7c,0x26,0xd5,
    0x41,0xa2,0xaa,0x2f,0xe3,0xdb,0x24,0xe7,
    0x45,0xa5,0xb0,0x2c,0x6c,0x38,0xc9,0x45,
    0x38,0x7d,0x0c,0x0d,0xe1,0xdb,0x59,0x8e,
    0xed,0x7a,0x0f,0xe9,0x47,0x0e,0xdc,0xc7,
    0x40,0xf2,0x94,0x4f,0xd1,0x58,0xae,0xfd,
    0xcb,0x3a,0xe1,0x7e,0xfe,0x7d,0xfc,0x72,
    0xeb,0x79,0x96,0xc1,0x98,0x93,0x99,0x23,
    0xd1,0x8c,0xa0,0x0c,0x50,0xf1,0xd1,0x01,
    0x5a,0xf0,0xf2,0xbe,0x43,0xc4,0x50,0xea,
    0x79,0x6f,0x69,0x8f,0xa9,0x6b,0x96,0xe6,
    0x10,0x38,0xf1,0x97,0x1b,0x8f,0x29,0xeb,
    0x32,0xae,0x3c,0xad,0xbd,0x81,0x9e,0x4a,
    0xd4,0xe5,0xaa,0x54,0xc0,0x89,0xc0,0xc3,
    0xaf,0x61,0xab,0xbc,0x2f,0x82,0xbc,0xd3,
    0xe2,0xbb,0x06,0xfb,0x4b,0xae,0x3f,0x48,
    0xbf,0x88,0x97,0xff,0xf1,0xdb,0xa2,0xe7,
    0x72,0xbd,0xcd,0x49,0xdb,0xdb,0xf3,0x86,
    0x7f,0x97,0x24,0x9b,0x4e,0x3c,0xf7,0xca,
    0x94,0x7a,0xdb,0x86,0x98,0x8e,0x39,0x57,
    0x0e,0x41,0x34,0x6c,0x28,0x8e,0x22,0xea,
    0xcf,0x0d,0xb7,0x60,0x7c,0xb6,0x02,0x4d,
    0x79,0x32,0x6e,0xc7,0xc2,0x2e,0x8a,0x0b,
    0xff,0xe7,0xa9,0x20,0x30,0x5f,0x18,0xa8,
    0x10,0x96,0xeb,0x4c,0x3f,0xfb,0x36,0xba,
    0xf5,0xf1,0x0b,0x80,0x7a,0x63,0x0e,0xc3,
    0xa8,0x7b,0x86,0x97,0x65,0xc7,0xe9,0xa9,
    0x62,0x11,0x33,0x3a,0x80,0x64,0x00,0x71,
    0xb4,0x6e,0x0a,0xf2,0xce,0x04,0x32,0x56,
    0xac,0xda,0xd1,0xea,0xf3,0x20,0xff,0x6e,
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
    [test_data_speck] = {
        .size = sizeof(_test_data_speck),
        .data = &_test_data_speck,
    },
};

/* A do-nothing function to time the benchmark framework */
static void *bench_nop_setup (void) {
    return NULL;
}

static void bench_nop_teardown (void *ctx) {
    return;
}

static const ssize_t bench_nop_run (
    void *ctx,
    const void *data_in,
    const ssize_t data_in_size,
    ssize_t *in
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
    const ssize_t got_size,
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
        ssize_t count_in;

        ssize_t count_out = item->run(
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

void benchmark_run_all (const int level, const int seconds) {
    struct bench_item *p;

    if(level==0) {
        printf("Each benchmark test runs for %i seconds\n\n", seconds);
    } else if(level==1) {
        printf("name,variant,seconds,bytes_in,bytes_out,loops,cycles,instr\n");
    }

    for(p = registered_items; p; p = p->next) {
        if(p->flags && BENCH_ITEM_CHECKONLY) {
            continue;
        }

        char name[40];
        snprintf(
            &name[0],
            sizeof(name),
            "%s,%s",
            p->name,
            p->variant ? p->variant : ""
        );

        if(level==0) {
            printf("%-20s", name);
        } else if(level==1) {
            printf("%s,", name);
        }
        fflush(stdout);

        run_one_item(seconds, p);

        if(level==0) {
            float seconds = ((float)p->usec / 1000000) + p->sec;
            printf(
                "%6.1fMB/s (%0.0f bytes) -> (%0.0f bytes)",
                (float)p->bytes_in / seconds / 1000000,
                (float)p->bytes_in / p->loops,
                (float)p->bytes_out / p->loops
            );

            if(p->cycles) {
                printf(
                    " cycles/loop=%0.0f ipc=%0.2f",
                    (float)p->cycles / p->loops,
                    (float)p->instr / p->cycles
                );
            }
            printf("\n");
        } else if(level==1) {
            printf("%i.%06i,", p->sec, p->usec);
            printf("%zd,%zd,", p->bytes_in, p->bytes_out);
            printf(
                "%" PRIu64 ",%" PRIu64 ",%" PRIu64 "\n",
                p->loops,
                p->cycles,
                p->instr
            );
        }
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

        ssize_t count_in;

        ssize_t count_out = p->run(
            ctx,
            input_data,
            input_size,
            &count_in
        );

        fprintf(stderr, "tested\n");

        if(level) {
            printf("%s: data_in id=%i\n", p->name, p->data_in);
        }

        int this_result = 0;
        bool checked = false;

        if(p->check) {
            this_result += p->check(ctx, level);
            checked = true;
        }

        if(p->get_output) {
            const void *out_data = p->get_output(ctx);

            if(p->data_out != test_data_none) {
                this_result += generic_check(p, out_data, count_out, level);
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

        if(this_result) {
            fprintf(stderr, "%s: Unexpected result\n", p->name);
        }
        result += this_result;

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
