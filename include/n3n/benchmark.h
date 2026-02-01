/*
 * Copyright (C) Hamish Coleman
 * SPDX-License-Identifier: GPL-3.0-only
 *
 */

#ifndef _N3N_BENCHMARK_H_
#define _N3N_BENCHMARK_H_

#include <stdint.h>

struct bench_item {
    struct bench_item *next;

    const char *name;                     // What is this testing
    const char *variant;                  // variant, eg name of optimisation
    int flags;
    void *(*setup)(void);           // Any pre-run setup
    uint64_t (*run)(void *data, uint64_t *bytes_in, uint64_t *bytes_out);
    int (*check)(void *data, int level);   // Check result against expected
    void (*teardown)(void *data);   // destroy any setup done

    // Perf processing tmp storage
    int fd[2];              // perf event fd (.0 == group leader)
    int id[2];              // perf event id

    // Returned Results
    int sec;            // How many seconds did we run for
    int usec;           // add how many microseconds
    uint64_t bytes_in;  // Total input bytes processed by all the runs
    uint64_t bytes_out; // Total output bytes processed by all the runs
    uint64_t loops;     // How many loops did we get
    uint64_t cycles;    // how many CPU cycles elapsed
    uint64_t instr;     // how many CPU instructions retired
};

#define BENCH_ITEM_CHECKONLY   0x1  // benchmark should be skipped

struct test_data {
    const int size;
    const void *data;
};

#define TEST_DATA_32x16 0   // 32 repeats of 16 bytes
#define TEST_DATA_COUNT 1

extern const struct test_data benchmark_test_data[TEST_DATA_COUNT];

void n3n_benchmark_register (struct bench_item *);

void benchmark_run_all (int seconds);
int benchmark_check_all (int level);

#endif
