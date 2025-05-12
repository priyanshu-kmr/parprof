#include "report.hpp"

#include <sys/wait.h>

#include <cstring>

namespace {

void print_finding(std::FILE* out, const Finding& f) {
    std::fprintf(out, "  [%s] %s\n", f.key.c_str(), f.message.c_str());
}

std::string ratio_or_na(const std::optional<double>& v, const char* fmt, double scale = 1) {
    if (!v) return "n/a";
    char buf[32];
    std::snprintf(buf, sizeof buf, fmt, *v * scale);
    return buf;
}

}  // namespace

void print_report(std::FILE* out, const RunResult& r, const std::vector<Finding>& findings) {
    std::fprintf(out, "\n== parprof: %s, %.3f s wall, %.3f s CPU ==\n\n",
                 describe_exit(r.wait_status).c_str(), r.wall_s, r.cpu_s);

    std::fprintf(out, "%8s  %-16s %9s %6s %8s %9s %9s %7s\n", "TID", "NAME", "CPU(s)", "CPU%",
                 "RUNQ(ms)", "VOL-CS", "INVOL-CS", "MIGR");
    for (const auto& t : r.threads) {
        double pct = r.wall_s > 0 ? 100 * t.cpu_s / r.wall_s : 0;
        std::fprintf(out, "%8d  %-16s %9.3f %6.1f %8.1f %9llu %9llu %7llu\n", t.tid, t.name.c_str(),
                     t.cpu_s, pct, 1000 * t.wait_s, (unsigned long long)t.voluntary_switches,
                     (unsigned long long)t.involuntary_switches, (unsigned long long)t.migrations);
    }

    const HwCounts& hw = r.hw;
    std::fprintf(out, "\nhardware (user space): %s instructions, %s cycles, IPC %s, L1D miss %s\n",
                 ratio_or_na(hw.instructions, "%.3g").c_str(), ratio_or_na(hw.cycles, "%.3g").c_str(),
                 ratio_or_na(ipc(hw), "%.2f").c_str(), ratio_or_na(l1d_miss_rate(hw), "%.1f%%", 100).c_str());

    if (r.gpu) {
        std::fprintf(out, "gpu: %s, util avg %.0f%% / peak %.0f%%, memory +%.0f MiB at peak\n",
                     r.gpu->name.c_str(), r.gpu->avg_util_pct, r.gpu->peak_util_pct,
                     r.gpu->peak_mem_delta_mib);
    } else if (!r.gpu_error.empty()) {
        std::fprintf(out, "gpu: unavailable (%s)\n", r.gpu_error.c_str());
    }

    std::fprintf(out, "\n");
    if (findings.empty()) std::fprintf(out, "no problems detected\n");
    for (const auto& f : findings) print_finding(out, f);
}

void print_scaling(std::FILE* out, const std::vector<ScalePoint>& points,
                   const std::optional<Finding>& verdict) {
    if (points.empty()) return;
    const RunResult& base = points.front().result;
    std::fprintf(out, "\n== parprof scaling ==\n\n");
    std::fprintf(out, "%7s %9s %8s %12s %12s %6s %8s\n", "THREADS", "WALL(s)", "SPEEDUP",
                 "INSTR(rel)", "CYCLES(rel)", "IPC", "L1D-MISS");
    for (const auto& p : points) {
        const HwCounts& hw = p.result.hw;
        std::optional<double> instr, cycles;
        if (hw.instructions && base.hw.instructions) instr = *hw.instructions / *base.hw.instructions;
        if (hw.cycles && base.hw.cycles) cycles = *hw.cycles / *base.hw.cycles;
        std::fprintf(out, "%7d %9.3f %7.2fx %12s %12s %6s %8s\n", p.threads, p.result.wall_s,
                     base.wall_s / p.result.wall_s, ratio_or_na(instr, "%.2fx").c_str(),
                     ratio_or_na(cycles, "%.2fx").c_str(), ratio_or_na(ipc(hw), "%.2f").c_str(),
                     ratio_or_na(l1d_miss_rate(hw), "%.1f%%", 100).c_str());
    }
    std::fprintf(out, "\n");
    if (verdict) print_finding(out, *verdict);
    else std::fprintf(out, "no cache contention detected\n");
}

bool write_timeline_csv(const std::string& path, const RunResult& r) {
    std::FILE* f = std::fopen(path.c_str(), "w");
    if (!f) return false;
    std::fprintf(f, "t_ms,tid,cpu_pct,core\n");
    for (const auto& p : r.timeline) std::fprintf(f, "%.1f,%d,%.1f,%d\n", p.t_ms, p.tid, p.cpu_pct, p.core);
    return std::fclose(f) == 0;
}

std::string describe_exit(int wait_status) {
    if (WIFSIGNALED(wait_status)) {
        int sig = WTERMSIG(wait_status);
        return "killed by signal " + std::to_string(sig) + " (" + strsignal(sig) + ")";
    }
    return "exited with code " + std::to_string(WEXITSTATUS(wait_status));
}

int exit_code(int wait_status) {
    return WIFSIGNALED(wait_status) ? 128 + WTERMSIG(wait_status) : WEXITSTATUS(wait_status);
}
