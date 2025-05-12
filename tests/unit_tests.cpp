// Unit tests for the /proc parsers and the analysis rules. No framework: each
// CHECK prints the failing line, and the exit code is the number of failures.
#include <unistd.h>

#include <cmath>
#include <cstdio>

#include "analysis.hpp"
#include "procfs.hpp"

static int failures = 0;

#define CHECK(cond)                                                    \
    do {                                                               \
        if (!(cond)) {                                                 \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            ++failures;                                                \
        }                                                              \
    } while (0)

static bool has_finding(const std::vector<Finding>& fs, const std::string& key) {
    for (const auto& f : fs)
        if (f.key == key) return true;
    return false;
}

static ThreadSummary thread(pid_t tid, double cpu_s, uint64_t vol_cs = 0, double wait_s = 0) {
    ThreadSummary t;
    t.tid = tid;
    t.cpu_s = cpu_s;
    t.voluntary_switches = vol_cs;
    t.wait_s = wait_s;
    return t;
}

static void test_parse_stat() {
    // A thread name with spaces and a ')' must not shift the fields.
    std::string text = "4242 (my (weird) app) R 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20 "
                       "21 22 23 24 25 26 27 28 29 30 31 32 33 34 35 7 0 0 0\n";
    ThreadSample s;
    CHECK(parse_stat(text, s));
    CHECK(s.name == "my (weird) app");
    CHECK(s.core == 7);

    ThreadSample bad;
    CHECK(!parse_stat("4242 (short) R 1 2 3\n", bad));
    CHECK(!parse_stat("garbage", bad));
}

static void test_parse_status() {
    std::string text = "Name:\tworker\nvoluntary_ctxt_switches:\t120\nnonvoluntary_ctxt_switches:\t7\n";
    ThreadSample s;
    CHECK(parse_status(text, s));
    // "voluntary_ctxt_switches" is also a suffix of "nonvoluntary_ctxt_switches".
    CHECK(s.voluntary_switches == 120);
    CHECK(s.involuntary_switches == 7);
    CHECK(!parse_status("Name:\tworker\n", s));
}

static void test_parse_schedstat_and_sched() {
    ThreadSample s;
    CHECK(parse_schedstat("123456789 2000 15\n", s));
    CHECK(s.run_ns == 123456789);
    CHECK(s.wait_ns == 2000);
    CHECK(!parse_schedstat("", s));

    std::string sched = "worker (4242, #threads: 4)\n-----\nse.exec_start   :   1.0\n"
                        "se.nr_migrations                             :                   31\n";
    CHECK(parse_sched_migrations(sched) == 31u);
    CHECK(!parse_sched_migrations("nothing here\n"));
}

static void test_live_process() {
    // Reading our own thread must work and show some CPU time.
    pid_t pid = getpid();
    auto tids = list_threads(pid);
    CHECK(tids.size() == 1 && tids[0] == pid);
    volatile double x = 0;
    for (int i = 0; i < 20'000'000; ++i) x = x + 1;
    auto s = sample_thread(pid, pid);
    CHECK(s.has_value());
    CHECK(s && s->run_ns > 0 && s->core >= 0);
    CHECK(!sample_thread(pid, 999'999'999));
}

static void test_imbalance_rule() {
    RunResult r;
    r.wall_s = 1.0;
    r.threads = {thread(1, 0.2), thread(2, 0.2), thread(3, 0.2), thread(4, 1.0)};
    CHECK(has_finding(analyze(r), "load-imbalance"));

    r.threads = {thread(1, 0.95), thread(2, 1.0), thread(3, 0.98), thread(4, 0.97)};
    CHECK(!has_finding(analyze(r), "load-imbalance"));

    // Helper threads that barely ran are ignored.
    r.threads = {thread(1, 1.0), thread(2, 1.0), thread(3, 0.001)};
    CHECK(!has_finding(analyze(r), "load-imbalance"));
}

static void test_contention_rule() {
    RunResult r;
    r.wall_s = 2.0;
    r.threads = {thread(1, 0.6, 50'000), thread(2, 0.6, 50'000)};
    CHECK(has_finding(analyze(r), "lock-contention"));

    // A few switches per thread (barriers, startup) are normal.
    r.threads = {thread(1, 1.9, 40), thread(2, 1.9, 40)};
    CHECK(!has_finding(analyze(r), "lock-contention"));
}

static void test_cache_and_oversubscription_rules() {
    RunResult r;
    r.wall_s = 1.0;
    r.threads = {thread(1, 1.0), thread(2, 1.0)};
    r.hw = {1e9, 1e8, {}, {}};  // IPC 0.1
    CHECK(has_finding(analyze(r), "low-ipc"));
    r.hw = {1e9, 2e9, {}, {}};  // IPC 2
    CHECK(!has_finding(analyze(r), "low-ipc"));
    r.hw = {};  // counters unsupported: no finding, no crash
    CHECK(!has_finding(analyze(r), "low-ipc"));
    r.threads = {thread(1, 1.0)};  // single-threaded low IPC isn't a threading problem
    r.hw = {1e9, 1e8, {}, {}};
    CHECK(!has_finding(analyze(r), "low-ipc"));

    r.threads = {thread(1, 1.0, 0, 0.8), thread(2, 1.0, 0, 0.8)};
    CHECK(has_finding(analyze(r), "cpu-oversubscription"));
}

static void test_scaling_rule() {
    auto point = [](int threads, double instr, double cycles) {
        RunResult r;
        r.hw.instructions = instr;
        r.hw.cycles = cycles;
        return ScalePoint{threads, r};
    };
    CHECK(analyze_scaling({point(1, 1e9, 1e9), point(8, 1.05e9, 9e9)}).has_value());
    CHECK(!analyze_scaling({point(1, 1e9, 1e9), point(8, 1.0e9, 1.9e9)}).has_value());
    CHECK(!analyze_scaling({point(1, 1e9, 1e9)}).has_value());
    CHECK(ipc(HwCounts{2e9, 1e9, {}, {}}).value() == 0.5);
}

int main() {
    test_parse_stat();
    test_parse_status();
    test_parse_schedstat_and_sched();
    test_live_process();
    test_imbalance_rule();
    test_contention_rule();
    test_cache_and_oversubscription_rules();
    test_scaling_rule();
    if (failures == 0) std::printf("all unit tests passed\n");
    return failures;
}
