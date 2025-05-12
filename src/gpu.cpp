#include "gpu.hpp"

#include <dlfcn.h>

#include <algorithm>

namespace {

// The few NVML types we need, matching nvml.h.
constexpr int kNvmlSuccess = 0;
struct NvmlUtilization {
    unsigned int gpu;     // % of time a kernel was running over the last sample period
    unsigned int memory;  // % of time device memory was being read or written
};
struct NvmlMemory {
    unsigned long long total, free, used;  // bytes
};

// NVML is loaded and initialized once and stays loaded: nvmlShutdown() leaks a
// device file descriptor per init/shutdown cycle, which adds up in --scale mode.
struct Nvml {
    void* device = nullptr;
    std::string name;
    std::string error;
    int (*get_utilization)(void*, NvmlUtilization*) = nullptr;
    int (*get_memory)(void*, NvmlMemory*) = nullptr;

    bool memory_mib(double& mib) const {
        NvmlMemory mem{};
        if (get_memory(device, &mem) != kNvmlSuccess) return false;
        mib = mem.used / (1024.0 * 1024.0);
        return true;
    }
};

template <typename Fn>
Fn load(void* lib, const char* symbol) {
    return reinterpret_cast<Fn>(dlsym(lib, symbol));
}

Nvml open_nvml() {
    Nvml n;
    void* lib = dlopen("libnvidia-ml.so.1", RTLD_NOW | RTLD_LOCAL);
    if (!lib) {
        n.error = "NVML not found (is the NVIDIA driver installed?)";
        return n;
    }
    auto init = load<int (*)()>(lib, "nvmlInit_v2");
    auto get_handle = load<int (*)(unsigned int, void**)>(lib, "nvmlDeviceGetHandleByIndex_v2");
    auto get_name = load<int (*)(void*, char*, unsigned int)>(lib, "nvmlDeviceGetName");
    n.get_utilization = load<int (*)(void*, NvmlUtilization*)>(lib, "nvmlDeviceGetUtilizationRates");
    n.get_memory = load<int (*)(void*, NvmlMemory*)>(lib, "nvmlDeviceGetMemoryInfo");
    if (!init || !get_handle || !get_name || !n.get_utilization || !n.get_memory) {
        n.error = "NVML is missing expected functions";
        return n;
    }
    if (init() != kNvmlSuccess) {
        n.error = "nvmlInit failed";
        return n;
    }

    void* device = nullptr;
    char name[96] = {};
    if (get_handle(0, &device) != kNvmlSuccess || get_name(device, name, sizeof name) != kNvmlSuccess) {
        n.error = "no NVIDIA GPU found";
        return n;
    }
    n.device = device;
    n.name = name;
    return n;
}

const Nvml& nvml() {
    static const Nvml instance = open_nvml();
    return instance;
}

}  // namespace

GpuSampler::GpuSampler() {
    if (!available()) return;
    summary_.name = nvml().name;
    nvml().memory_mib(baseline_mem_mib_);
}

bool GpuSampler::available() const { return nvml().device != nullptr; }

const std::string& GpuSampler::error() const { return nvml().error; }

void GpuSampler::sample() {
    if (!available()) return;
    NvmlUtilization util{};
    if (nvml().get_utilization(nvml().device, &util) == kNvmlSuccess) {
        util_sum_ += util.gpu;
        summary_.peak_util_pct = std::max(summary_.peak_util_pct, double(util.gpu));
        ++summary_.samples;
    }
    double mib;
    if (nvml().memory_mib(mib))
        summary_.peak_mem_delta_mib = std::max(summary_.peak_mem_delta_mib, mib - baseline_mem_mib_);
}

GpuSummary GpuSampler::summary() const {
    GpuSummary s = summary_;
    if (s.samples > 0) s.avg_util_pct = util_sum_ / s.samples;
    return s;
}
