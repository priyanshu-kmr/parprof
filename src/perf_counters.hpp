#pragma once

#include <sys/types.h>

#include <optional>

// Hardware counter totals for a process and every thread it created.
// A value is empty when the CPU or kernel doesn't support that event.
struct HwCounts {
    std::optional<double> cycles;
    std::optional<double> instructions;
    std::optional<double> l1d_loads;
    std::optional<double> l1d_misses;
};

// Counts user-space events in `pid` and all of its threads using perf_event_open.
// Counting starts when the process calls exec(); totals are final once it exits.
class PerfCounters {
public:
    explicit PerfCounters(pid_t pid);
    ~PerfCounters();
    PerfCounters(const PerfCounters&) = delete;
    PerfCounters& operator=(const PerfCounters&) = delete;

    HwCounts read() const;

private:
    enum { kCycles, kInstructions, kL1dLoads, kL1dMisses, kCount };
    int fds_[kCount];
};
