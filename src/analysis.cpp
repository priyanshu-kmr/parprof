#include "analysis.hpp"

#include <algorithm>
#include <cstdio>

namespace {

// Thresholds, calibrated on the demo programs (see README).
constexpr double kImbalanceRatio = 1.4;          // busiest thread's CPU time / average
constexpr double kContentionSwitchRate = 1000;   // voluntary switches per thread per second
constexpr double kLowIpc = 0.5;                  // instructions per cycle
constexpr double kOversubscribedWait = 0.25;     // time waiting for a CPU / time running
constexpr double kMinCpuSeconds = 0.05;          // ignore threads that barely ran
constexpr double kScalingCycleGrowth = 3.0;      // cycles grew this much (SMT alone can double them)...
constexpr double kScalingMaxWorkGrowth = 1.3;    // ...while instructions grew less than this

std::string format(const char* fmt, auto... args) {
    char buf[512];
    std::snprintf(buf, sizeof buf, fmt, args...);
    return buf;
}

std::vector<ThreadSummary> busy_threads(const RunResult& r) {
    std::vector<ThreadSummary> out;
    for (const auto& t : r.threads)
        if (t.cpu_s >= kMinCpuSeconds) out.push_back(t);
    return out;
}

std::optional<Finding> check_imbalance(const std::vector<ThreadSummary>& threads) {
    if (threads.size() < 2) return std::nullopt;
    double total = 0;
    const ThreadSummary* busiest = &threads[0];
    for (const auto& t : threads) {
        total += t.cpu_s;
        if (t.cpu_s > busiest->cpu_s) busiest = &t;
    }
    double ratio = busiest->cpu_s / (total / threads.size());
    if (ratio < kImbalanceRatio) return std::nullopt;
    return Finding{"load-imbalance",
                   format("thread %d did %.2fx the average work; the others sat idle waiting "
                          "for it. Try schedule(dynamic) or smaller chunks.",
                          busiest->tid, ratio)};
}

std::optional<Finding> check_contention(const RunResult& r, const std::vector<ThreadSummary>& threads) {
    if (threads.size() < 2 || r.wall_s <= 0) return std::nullopt;
    double switches = 0;
    for (const auto& t : threads) switches += t.voluntary_switches;
    double rate = switches / threads.size() / r.wall_s;
    if (rate < kContentionSwitchRate) return std::nullopt;
    return Finding{"lock-contention",
                   format("each thread blocked %.0f times per second, which usually means "
                          "threads queueing for a contended lock (a futex wait each time). "
                          "Shrink the critical section or use per-thread results (e.g. a reduction).",
                          rate)};
}

// One run can't tell cache-line contention from other memory stalls, so this
// only points at the cause; --scale (analyze_scaling) confirms it.
std::optional<Finding> check_low_ipc(const RunResult& r, const std::vector<ThreadSummary>& threads) {
    auto i = ipc(r.hw);
    if (threads.size() < 2 || !i || *i >= kLowIpc) return std::nullopt;
    return Finding{"low-ipc",
                   format("IPC is %.2f: the cores spend most cycles stalled, usually on memory. "
                          "In multithreaded code this is often threads fighting over the same "
                          "cache lines (false sharing). Run with --scale to confirm.",
                          *i)};
}

std::optional<Finding> check_oversubscription(const std::vector<ThreadSummary>& threads) {
    double run = 0, wait = 0;
    for (const auto& t : threads) {
        run += t.cpu_s;
        wait += t.wait_s;
    }
    if (run <= 0 || wait / run < kOversubscribedWait) return std::nullopt;
    return Finding{"cpu-oversubscription",
                   format("threads spent %.0f%% as long waiting for a free CPU as running. "
                          "There are more runnable threads than cores; use fewer threads.",
                          100 * wait / run)};
}

}  // namespace

std::optional<double> ipc(const HwCounts& hw) {
    if (!hw.cycles || !hw.instructions || *hw.cycles <= 0) return std::nullopt;
    return *hw.instructions / *hw.cycles;
}

std::optional<double> l1d_miss_rate(const HwCounts& hw) {
    if (!hw.l1d_loads || !hw.l1d_misses || *hw.l1d_loads <= 0) return std::nullopt;
    return *hw.l1d_misses / *hw.l1d_loads;
}

std::vector<Finding> analyze(const RunResult& r) {
    auto threads = busy_threads(r);
    std::vector<Finding> findings;
    for (auto f : {check_imbalance(threads), check_contention(r, threads), check_low_ipc(r, threads),
                   check_oversubscription(threads)})
        if (f) findings.push_back(*f);
    return findings;
}

std::optional<Finding> analyze_scaling(const std::vector<ScalePoint>& points) {
    if (points.size() < 2) return std::nullopt;
    auto by_threads = [](const ScalePoint& a, const ScalePoint& b) { return a.threads < b.threads; };
    const ScalePoint& lo = *std::min_element(points.begin(), points.end(), by_threads);
    const ScalePoint& hi = *std::max_element(points.begin(), points.end(), by_threads);
    const HwCounts &a = lo.result.hw, &b = hi.result.hw;
    if (!a.cycles || !b.cycles || !a.instructions || !b.instructions) return std::nullopt;

    double work = *b.instructions / *a.instructions;
    double cycles = *b.cycles / *a.cycles;
    if (cycles < kScalingCycleGrowth || work > kScalingMaxWorkGrowth) return std::nullopt;
    return Finding{"cache-contention",
                   format("going from %d to %d threads, the work stayed the same (%.2fx "
                          "instructions) but took %.1fx the CPU cycles. Threads are fighting "
                          "over shared cache lines: pad per-thread data to 64 bytes, or stop "
                          "sharing it.",
                          lo.threads, hi.threads, work, cycles)};
}
