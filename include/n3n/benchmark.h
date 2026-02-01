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
    void *(*setup)(void);           // Any pre-run setup
    uint64_t (*run)(void *data, uint64_t *bytes_in, uint64_t *bytes_out);
    // void *(*check)(...           // TODO: add a way to check result
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

#define TEST_DATA_32x16 1   // 32 repeats of 16 bytes

void n3n_benchmark_register (struct bench_item *);

void *benchmark_get_test_data (int data_nr, int data_size);
void benchmark_run_all (int seconds);

#endif
