#pragma once

#include <optional>
#include <string>
#include <vector>

#include "profiler.hpp"

struct Finding {
    std::string key;      // stable id, e.g. "load-imbalance"
    std::string message;  // what was seen and what to try
};

// Rules that turn the measurements of one run into findings.
std::vector<Finding> analyze(const RunResult& r);

// The same program run with different thread counts (OMP_NUM_THREADS).
struct ScalePoint {
    int threads;
    RunResult result;
};

// Flags runs where the work stayed the same but the cycles needed to do it grew.
std::optional<Finding> analyze_scaling(const std::vector<ScalePoint>& points);

// Derived metrics shared by the rules and the report.
std::optional<double> ipc(const HwCounts& hw);
std::optional<double> l1d_miss_rate(const HwCounts& hw);
