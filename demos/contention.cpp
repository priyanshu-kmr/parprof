// Lock contention: every iteration adds its result to one shared total inside
// a critical section. Threads spend their time queueing for the lock, and with
// OMP_WAIT_POLICY=passive a thread that finds the lock taken sleeps in the
// kernel (a futex wait), which shows up as a voluntary context switch.
//
// --fixed uses reduction(+:total): each thread sums privately, then combines once.
#include <omp.h>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>

constexpr long kItems = 30'000'000;

uint64_t cost(long i) {
    uint64_t x = i;
    for (int k = 0; k < 40; ++k) x = x * 6364136223846793005ull + 1442695040888963407ull;
    return x >> 60;
}

int main(int argc, char* argv[]) {
    bool fixed = argc > 1 && std::strcmp(argv[1], "--fixed") == 0;

    auto start = std::chrono::steady_clock::now();
    uint64_t total = 0;
    if (fixed) {
#pragma omp parallel for reduction(+ : total)
        for (long i = 0; i < kItems; ++i) total += cost(i);
    } else {
#pragma omp parallel for
        for (long i = 0; i < kItems; ++i) {
            uint64_t v = cost(i);
#pragma omp critical
            total += v;
        }
    }
    double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();

    std::printf("%s: %d threads, total %llu, %.3f s\n", fixed ? "reduction" : "critical",
                omp_get_max_threads(), (unsigned long long)total, secs);
}
