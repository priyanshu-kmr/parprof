// False sharing: each thread increments only its own counter, yet the threads
// slow each other down. The counters are packed next to each other, so eight of
// them share one 64-byte cache line, and every write by one core invalidates
// that line in the other cores' caches.
//
// --fixed gives each counter its own cache line.
#include <omp.h>

#include <chrono>
#include <cstdio>
#include <cstring>

constexpr long kTotalIncrements = 2'000'000'000;  // split across threads: total work is fixed
constexpr int kMaxThreads = 256;

struct alignas(64) PaddedCounter {
    volatile long value;  // volatile: force a real memory write on every increment
};

volatile long packed[kMaxThreads];
PaddedCounter padded[kMaxThreads];

int main(int argc, char* argv[]) {
    bool fixed = argc > 1 && std::strcmp(argv[1], "--fixed") == 0;
    if (omp_get_max_threads() > kMaxThreads) omp_set_num_threads(kMaxThreads);

    auto start = std::chrono::steady_clock::now();
    int threads = 0;
#pragma omp parallel
    {
        int t = omp_get_thread_num();
        threads = omp_get_num_threads();
        long n = kTotalIncrements / omp_get_num_threads();
        volatile long* counter = fixed ? &padded[t].value : &packed[t];
        for (long i = 0; i < n; ++i) *counter = *counter + 1;
    }
    double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();

    long sum = 0;
    for (int t = 0; t < threads; ++t) sum += fixed ? padded[t].value : packed[t];
    std::printf("%s counters: %d threads, sum %ld, %.3f s\n", fixed ? "padded" : "packed", threads, sum, secs);
}
