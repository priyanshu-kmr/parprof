// False sharing: per-thread counters share a cache line.
// --fixed pads each counter to its own line.
#include <omp.h>
#include <stdio.h>
#include <string.h>

#define TOTAL_INCREMENTS 2000000000L
#define MAX_THREADS 256

struct padded_counter {
    _Alignas(64) volatile long value;  // volatile: really write memory each time
};

volatile long packed[MAX_THREADS];
struct padded_counter padded[MAX_THREADS];

int main(int argc, char *argv[]) {
    int fixed = argc > 1 && strcmp(argv[1], "--fixed") == 0;
    if (omp_get_max_threads() > MAX_THREADS) omp_set_num_threads(MAX_THREADS);

    double start = omp_get_wtime();
    int threads = 0;
#pragma omp parallel
    {
        int t = omp_get_thread_num();
        threads = omp_get_num_threads();
        long n = TOTAL_INCREMENTS / omp_get_num_threads();
        volatile long *counter = fixed ? &padded[t].value : &packed[t];
        for (long i = 0; i < n; ++i) *counter = *counter + 1;
    }
    double secs = omp_get_wtime() - start;

    long sum = 0;
    for (int t = 0; t < threads; ++t) sum += fixed ? padded[t].value : packed[t];
    printf("%s counters: %d threads, sum %ld, %.3f s\n", fixed ? "padded" : "packed", threads, sum, secs);
    return 0;
}
