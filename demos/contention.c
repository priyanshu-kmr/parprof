// Lock contention: every iteration takes one shared critical section.
// --fixed uses a reduction.
#include <omp.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define ITEMS 30000000L

static uint64_t cost(long i) {
    uint64_t x = i;
    for (int k = 0; k < 40; ++k) x = x * 6364136223846793005ull + 1442695040888963407ull;
    return x >> 60;
}

int main(int argc, char *argv[]) {
    int fixed = argc > 1 && strcmp(argv[1], "--fixed") == 0;

    double start = omp_get_wtime();
    uint64_t total = 0;
    if (fixed) {
#pragma omp parallel for reduction(+ : total)
        for (long i = 0; i < ITEMS; ++i) total += cost(i);
    } else {
#pragma omp parallel for
        for (long i = 0; i < ITEMS; ++i) {
            uint64_t v = cost(i);
#pragma omp critical
            total += v;
        }
    }
    double secs = omp_get_wtime() - start;

    printf("%s: %d threads, total %llu, %.3f s\n", fixed ? "reduction" : "critical",
           omp_get_max_threads(), (unsigned long long)total, secs);
    return 0;
}
