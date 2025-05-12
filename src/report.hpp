#pragma once

#include <cstdio>
#include <optional>
#include <string>
#include <vector>

#include "analysis.hpp"

void print_report(std::FILE* out, const RunResult& r, const std::vector<Finding>& findings);
void print_scaling(std::FILE* out, const std::vector<ScalePoint>& points,
                   const std::optional<Finding>& verdict);
bool write_timeline_csv(const std::string& path, const RunResult& r);

// "exited with code 3", "killed by signal 15 (Terminated)", ...
std::string describe_exit(int wait_status);
// Shell convention: exit code, or 128 + signal number.
int exit_code(int wait_status);
