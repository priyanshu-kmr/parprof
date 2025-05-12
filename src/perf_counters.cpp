#include "perf_counters.hpp"

#include <linux/perf_event.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <cstdint>

namespace {

struct EventSpec {
    uint32_t type;
    uint64_t config;
};

constexpr uint64_t cache_event(uint64_t cache, uint64_t op, uint64_t result) {
    return cache | (op << 8) | (result << 16);
}

// Same order as the enum in PerfCounters.
constexpr EventSpec kEvents[] = {
    {PERF_TYPE_HARDWARE, PERF_COUNT_HW_CPU_CYCLES},
    {PERF_TYPE_HARDWARE, PERF_COUNT_HW_INSTRUCTIONS},
    {PERF_TYPE_HW_CACHE, cache_event(PERF_COUNT_HW_CACHE_L1D, PERF_COUNT_HW_CACHE_OP_READ,
                                     PERF_COUNT_HW_CACHE_RESULT_ACCESS)},
    {PERF_TYPE_HW_CACHE, cache_event(PERF_COUNT_HW_CACHE_L1D, PERF_COUNT_HW_CACHE_OP_READ,
                                     PERF_COUNT_HW_CACHE_RESULT_MISS)},
};

int perf_event_open(perf_event_attr* attr, pid_t pid) {
    // cpu = -1: follow the task on any CPU. No group: each counter stands alone,
    // which keeps working when the kernel has to multiplex them.
    return static_cast<int>(syscall(SYS_perf_event_open, attr, pid, -1, -1, PERF_FLAG_FD_CLOEXEC));
}

}  // namespace

PerfCounters::PerfCounters(pid_t pid) {
    for (int i = 0; i < kCount; ++i) {
        perf_event_attr attr{};
        attr.size = sizeof attr;
        attr.type = kEvents[i].type;
        attr.config = kEvents[i].config;
        attr.disabled = 1;
        attr.enable_on_exec = 1;  // don't count the fork/exec setup
        attr.inherit = 1;         // include threads the program creates
        attr.exclude_kernel = 1;  // user space only: allowed at perf_event_paranoid=2
        attr.exclude_hv = 1;
        attr.read_format = PERF_FORMAT_TOTAL_TIME_ENABLED | PERF_FORMAT_TOTAL_TIME_RUNNING;
        fds_[i] = perf_event_open(&attr, pid);  // -1 if unsupported; read() reports it as empty
    }
}

PerfCounters::~PerfCounters() {
    for (int fd : fds_)
        if (fd >= 0) close(fd);
}

HwCounts PerfCounters::read() const {
    std::optional<double> values[kCount];
    for (int i = 0; i < kCount; ++i) {
        struct {
            uint64_t value, enabled, running;
        } r;
        if (fds_[i] < 0 || ::read(fds_[i], &r, sizeof r) != sizeof r || r.running == 0) continue;
        // If the counter only had hardware part of the time, scale up to the full run.
        values[i] = static_cast<double>(r.value) * r.enabled / r.running;
    }
    return {values[kCycles], values[kInstructions], values[kL1dLoads], values[kL1dMisses]};
}
