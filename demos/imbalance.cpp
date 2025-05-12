// Load imbalance: row i of a triangular loop costs i units of work.
// schedule(static) hands each thread one contiguous block of rows, so the
// thread that gets the last block does far more work than the first, and the
// rest wait for it at the end of the loop.
//
// --fixed uses schedule(dynamic): threads grab small chunks as they go.
#include <omp.h>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>

constexpr int kRows = 250'000;

// A few dependent integer operations, so the compiler can't skip the work.
uint32_t cell(uint32_t i, uint32_t j) {
    uint32_t x = i * 2654435761u ^ j;
    x ^= x >> 13;
    x *= 0x5bd1e995u;
    return x ^ (x >> 15);
}

int main(int argc, char* argv[]) {
    bool fixed = argc > 1 && std::strcmp(argv[1], "--fixed") == 0;
    omp_set_schedule(fixed ? omp_sched_dynamic : omp_sched_static, fixed ? 64 : 0);

    auto start = std::chrono::steady_clock::now();
    uint64_t checksum = 0;
#pragma omp parallel for schedule(runtime) reduction(^ : checksum)
    for (int i = 0; i < kRows; ++i) {
        uint32_t row = 0;
        for (int j = 0; j < i; ++j) row += cell(i, j);
        checksum ^= row;
    }
    double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();

    std::printf("%s schedule: %d threads, checksum %llx, %.3f s\n", fixed ? "dynamic" : "static",
                omp_get_max_threads(), (unsigned long long)checksum, secs);
}
