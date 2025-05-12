#include <getopt.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <system_error>

#include "analysis.hpp"
#include "profiler.hpp"
#include "report.hpp"

namespace {

void usage(std::FILE* out) {
    std::fprintf(out,
        "usage: parprof [options] [--] program [args...]\n"
        "\n"
        "Runs a program and reports why its threads are slow. The report goes to stderr.\n"
        "\n"
        "  -i, --interval MS     sampling interval in milliseconds (default 10)\n"
        "  -c, --csv FILE        write a per-thread CPU timeline (plot with scripts/plot.py)\n"
        "  -s, --scale LIST      run once per thread count, e.g. 1,2,4,8 (sets OMP_NUM_THREADS)\n"
        "  -g, --gpu             also sample NVIDIA GPU 0 through NVML\n"
        "      --keep-wait-policy  don't set OMP_WAIT_POLICY=passive (see README)\n"
        "  -h, --help\n");
}

bool parse_scale(const char* text, std::vector<int>& out) {
    std::stringstream in(text);
    std::string item;
    while (std::getline(in, item, ',')) {
        char* end;
        long n = std::strtol(item.c_str(), &end, 10);
        if (item.empty() || *end || n < 1 || n > 1024) return false;
        out.push_back(static_cast<int>(n));
    }
    return !out.empty();
}

}  // namespace

int main(int argc, char* argv[]) {
    Options opts;
    std::string csv_path;
    std::vector<int> scale;
    bool keep_wait_policy = false;

    const option long_opts[] = {
        {"interval", required_argument, nullptr, 'i'},
        {"csv", required_argument, nullptr, 'c'},
        {"scale", required_argument, nullptr, 's'},
        {"gpu", no_argument, nullptr, 'g'},
        {"keep-wait-policy", no_argument, nullptr, 'k'},
        {"help", no_argument, nullptr, 'h'},
        {nullptr, 0, nullptr, 0},
    };
    // Leading '+': stop at the first non-option, so the program's own flags are left alone.
    int c;
    while ((c = getopt_long(argc, argv, "+i:c:s:gh", long_opts, nullptr)) != -1) {
        switch (c) {
        case 'i':
            opts.interval_ms = std::atoi(optarg);
            if (opts.interval_ms < 1) {
                std::fprintf(stderr, "parprof: --interval must be at least 1 ms\n");
                return 2;
            }
            break;
        case 'c': csv_path = optarg; break;
        case 's':
            if (!parse_scale(optarg, scale)) {
                std::fprintf(stderr, "parprof: --scale wants a list like 1,2,4,8\n");
                return 2;
            }
            break;
        case 'g': opts.gpu = true; break;
        case 'k': keep_wait_policy = true; break;
        case 'h': usage(stdout); return 0;
        default: usage(stderr); return 2;
        }
    }
    if (optind >= argc) {
        usage(stderr);
        return 2;
    }
    char** program = argv + optind;

    // Idle OpenMP threads spin by default, which makes them look busy. Passive
    // waiting puts them to sleep so CPU time reflects real work.
    if (!keep_wait_policy && !std::getenv("OMP_WAIT_POLICY"))
        opts.env.emplace_back("OMP_WAIT_POLICY", "passive");

    try {
        if (scale.empty()) {
            RunResult r = profile(program, opts);
            print_report(stderr, r, analyze(r));
            if (!csv_path.empty() && !write_timeline_csv(csv_path, r)) {
                std::fprintf(stderr, "parprof: cannot write %s: %s\n", csv_path.c_str(), std::strerror(errno));
                return 1;
            }
            return exit_code(r.wait_status);
        }

        std::vector<ScalePoint> points;
        for (int n : scale) {
            Options run_opts = opts;
            run_opts.env.emplace_back("OMP_NUM_THREADS", std::to_string(n));
            std::fprintf(stderr, "parprof: running with %d thread(s)...\n", n);
            points.push_back({n, profile(program, run_opts)});
            if (int code = exit_code(points.back().result.wait_status)) {
                std::fprintf(stderr, "parprof: program %s\n", describe_exit(points.back().result.wait_status).c_str());
                return code;
            }
        }
        print_scaling(stderr, points, analyze_scaling(points));
        return 0;
    } catch (const ExecError& e) {
        std::fprintf(stderr, "parprof: %s\n", e.what());
        return 127;
    } catch (const std::system_error& e) {
        std::fprintf(stderr, "parprof: %s\n", e.what());
        return 1;
    }
}
