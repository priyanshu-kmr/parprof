#pragma once

#include <sys/types.h>

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "gpu.hpp"
#include "launcher.hpp"
#include "perf_counters.hpp"

struct Options {
    int interval_ms = 10;  // how often to sample /proc
    bool gpu = false;      // also sample GPU 0 through NVML
    EnvList env;           // extra environment variables for the program
};

struct ThreadSummary {
    pid_t tid = 0;
    std::string name;
    double cpu_s = 0;   // time running on a CPU
    double wait_s = 0;  // time runnable but waiting for a free CPU
    uint64_t voluntary_switches = 0;
    uint64_t involuntary_switches = 0;
    uint64_t migrations = 0;
};

// CPU use of one thread over one sampling interval.
struct TimelinePoint {
    double t_ms;
    pid_t tid;
    double cpu_pct;
    int core;
};

struct RunResult {
    int wait_status = 0;  // as returned by wait4()
    double wall_s = 0;
    double cpu_s = 0;  // user + system time of all threads, exact (from wait4)
    std::vector<ThreadSummary> threads;  // sampled; may miss the last interval before exit
    std::vector<TimelinePoint> timeline;
    HwCounts hw;
    std::optional<GpuSummary> gpu;
    std::string gpu_error;
};

// Thrown when the program can't be started (e.g. not found).
struct ExecError : std::runtime_error {
    int error;
    ExecError(const std::string& what, int err) : std::runtime_error(what), error(err) {}
};

// Runs argv to completion while sampling it. Throws ExecError or std::system_error.
RunResult profile(char* const argv[], const Options& opts);
