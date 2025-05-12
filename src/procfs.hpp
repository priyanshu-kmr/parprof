#pragma once

#include <sys/types.h>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// One snapshot of a thread's scheduler statistics, read from /proc/<pid>/task/<tid>/.
struct ThreadSample {
    pid_t tid = 0;
    std::string name;
    int core = -1;                       // CPU the thread last ran on (stat, field 39)
    uint64_t run_ns = 0;                 // time spent running on a CPU (schedstat)
    uint64_t wait_ns = 0;                // time spent runnable, waiting for a CPU (schedstat)
    uint64_t voluntary_switches = 0;     // thread blocked: lock, I/O, sleep (status)
    uint64_t involuntary_switches = 0;   // thread was preempted (status)
    std::optional<uint64_t> migrations;  // moves between CPUs (sched, needs CONFIG_SCHED_DEBUG)
};

std::vector<pid_t> list_threads(pid_t pid);

// Returns nothing if the thread exited while it was being read.
std::optional<ThreadSample> sample_thread(pid_t pid, pid_t tid);

// Parsers for the individual files, exposed for unit tests.
bool parse_stat(const std::string& text, ThreadSample& out);
bool parse_status(const std::string& text, ThreadSample& out);
bool parse_schedstat(const std::string& text, ThreadSample& out);
std::optional<uint64_t> parse_sched_migrations(const std::string& text);
