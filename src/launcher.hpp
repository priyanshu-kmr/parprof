#pragma once

#include <sys/types.h>

#include <string>
#include <utility>
#include <vector>

using EnvList = std::vector<std::pair<std::string, std::string>>;

// A forked child that blocks before exec() until release() is called, so the
// parent can attach perf counters to it first.
class PausedChild {
public:
    // Throws std::system_error if the pipes or fork fail.
    PausedChild(char* const argv[], const EnvList& env);
    ~PausedChild();
    PausedChild(const PausedChild&) = delete;
    PausedChild& operator=(const PausedChild&) = delete;

    pid_t pid() const { return pid_; }

    // Lets the child exec. Returns 0 once exec succeeded, or the errno of a failed exec.
    int release();

private:
    pid_t pid_ = -1;
    int gate_fd_ = -1;   // parent writes one byte here to let the child exec
    int error_fd_ = -1;  // child writes errno here if exec fails; EOF means exec succeeded
    bool released_ = false;
};
