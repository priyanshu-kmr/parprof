// Load imbalance: later rows cost more, schedule(static) splits them unevenly.
// --fixed uses schedule(dynamic).
#include <omp.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define ROWS 250000

static uint32_t cell(uint32_t i, uint32_t j) {
    uint32_t x = i * 2654435761u ^ j;
    x ^= x >> 13;
    x *= 0x5bd1e995u;
    return x ^ (x >> 15);
}

int main(int argc, char *argv[]) {
    int fixed = argc > 1 && strcmp(argv[1], "--fixed") == 0;
    omp_set_schedule(fixed ? omp_sched_dynamic : omp_sched_static, fixed ? 64 : 0);

    double start = omp_get_wtime();
    uint64_t checksum = 0;
#pragma omp parallel for schedule(runtime) reduction(^ : checksum)
    for (int i = 0; i < ROWS; ++i) {
        uint32_t row = 0;
        for (int j = 0; j < i; ++j) row += cell(i, j);
        checksum ^= row;
    }
    double secs = omp_get_wtime() - start;

    printf("%s schedule: %d threads, checksum %llx, %.3f s\n", fixed ? "dynamic" : "static",
           omp_get_max_threads(), (unsigned long long)checksum, secs);
    return 0;
}
