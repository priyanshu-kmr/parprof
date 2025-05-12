#pragma once

#include <string>

struct GpuSummary {
    std::string name;
    double avg_util_pct = 0;
    double peak_util_pct = 0;
    double peak_mem_delta_mib = 0;  // peak memory in use, minus what was in use before the run
    int samples = 0;
};

// Samples NVIDIA GPU 0 through NVML. The library is loaded with dlopen, so
// parprof builds and runs on machines without the NVIDIA driver.
// Utilization and memory are device-wide, not limited to the profiled process.
class GpuSampler {
public:
    GpuSampler();

    bool available() const;
    const std::string& error() const;

    void sample();
    GpuSummary summary() const;

private:
    GpuSummary summary_;
    double util_sum_ = 0;
    double baseline_mem_mib_ = 0;
};
