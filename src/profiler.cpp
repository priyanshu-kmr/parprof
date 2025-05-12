#include "profiler.hpp"

#include <poll.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstring>
#include <map>
#include <system_error>

#include "procfs.hpp"

namespace {

using Clock = std::chrono::steady_clock;

constexpr int kGpuSampleEvery = 10;  // NVML averages over ~100ms+ anyway; no need to poll it often

double ms_since(Clock::time_point start) {
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

// Keeps the first and latest sample of every thread seen, and builds the timeline.
class ThreadTracker {
public:
    void sample(pid_t pid, double t_ms, std::vector<TimelinePoint>& timeline) {
        for (pid_t tid : list_threads(pid)) {
            auto s = sample_thread(pid, tid);
            if (!s) continue;  // exited between listing and reading
            auto [it, is_new] = threads_.try_emplace(tid);
            Tracked& t = it->second;
            if (!is_new) {
                double dt_ns = (t_ms - t.last_t_ms) * 1e6;
                double pct = dt_ns > 0 ? 100.0 * double(s->run_ns - t.last.run_ns) / dt_ns : 0;
                timeline.push_back({t_ms, tid, pct, s->core});
                if (s->core != t.last.core) ++t.core_changes;
            }
            t.last = *s;
            t.last_t_ms = t_ms;
        }
    }

    std::vector<ThreadSummary> summaries() const {
        std::vector<ThreadSummary> out;
        for (const auto& [tid, t] : threads_) {
            ThreadSummary s;
            s.tid = tid;
            s.name = t.last.name;
            s.cpu_s = t.last.run_ns / 1e9;
            s.wait_s = t.last.wait_ns / 1e9;
            s.voluntary_switches = t.last.voluntary_switches;
            s.involuntary_switches = t.last.involuntary_switches;
            // Exact count from the kernel if available, else the core changes we happened to see.
            s.migrations = t.last.migrations.value_or(t.core_changes);
            out.push_back(s);
        }
        return out;
    }

private:
    struct Tracked {
        ThreadSample last;
        double last_t_ms = 0;
        uint64_t core_changes = 0;
    };
    std::map<pid_t, Tracked> threads_;
};

// Ignores Ctrl-C in parprof while the program runs (like time(1)), so the
// program can be interrupted and parprof still prints its report.
class IgnoreInterrupts {
public:
    IgnoreInterrupts() {
        old_int_ = signal(SIGINT, SIG_IGN);
        old_quit_ = signal(SIGQUIT, SIG_IGN);
    }
    ~IgnoreInterrupts() {
        signal(SIGINT, old_int_);
        signal(SIGQUIT, old_quit_);
    }

private:
    sighandler_t old_int_, old_quit_;
};

}  // namespace

RunResult profile(char* const argv[], const Options& opts) {
    IgnoreInterrupts ignore;
    RunResult result;

    std::optional<GpuSampler> gpu;
    if (opts.gpu) {
        gpu.emplace();
        if (!gpu->available()) {
            result.gpu_error = gpu->error();
            gpu.reset();
        }
    }

    PausedChild child(argv, opts.env);
    PerfCounters counters(child.pid());  // attach before the program starts

    // A pidfd becomes readable when the process exits, so poll() doubles as
    // "sleep until the next sample or until the program ends, whichever is first".
    int pidfd = static_cast<int>(syscall(SYS_pidfd_open, child.pid(), 0));
    if (pidfd < 0) throw std::system_error(errno, std::generic_category(), "pidfd_open");

    if (int err = child.release()) {
        close(pidfd);
        throw ExecError(std::string("cannot run '") + argv[0] + "': " + std::strerror(err), err);
    }
    auto start = Clock::now();

    ThreadTracker tracker;
    pollfd pfd{pidfd, POLLIN, 0};
    for (int tick = 0;; ++tick) {
        tracker.sample(child.pid(), ms_since(start), result.timeline);
        if (gpu && tick % kGpuSampleEvery == 0) gpu->sample();

        double next_ms = (tick + 1.0) * opts.interval_ms;  // fixed schedule, no drift
        int timeout = std::max(0, static_cast<int>(next_ms - ms_since(start)));
        int ready = poll(&pfd, 1, timeout);
        if (ready > 0) break;
        if (ready < 0 && errno != EINTR) {
            close(pidfd);
            throw std::system_error(errno, std::generic_category(), "poll");
        }
    }
    result.wall_s = ms_since(start) / 1000.0;
    close(pidfd);

    rusage usage{};
    while (wait4(child.pid(), &result.wait_status, 0, &usage) < 0 && errno == EINTR) {}
    result.cpu_s = usage.ru_utime.tv_sec + usage.ru_utime.tv_usec / 1e6 +
                   usage.ru_stime.tv_sec + usage.ru_stime.tv_usec / 1e6;

    result.threads = tracker.summaries();
    result.hw = counters.read();
    if (gpu) result.gpu = gpu->summary();
    return result;
}
