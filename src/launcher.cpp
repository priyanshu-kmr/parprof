#include "launcher.hpp"

#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <system_error>

namespace {

void close_fd(int& fd) {
    if (fd >= 0) close(fd);
    fd = -1;
}

}  // namespace

PausedChild::PausedChild(char* const argv[], const EnvList& env) {
    // Both pipes are close-on-exec: a successful exec closes the child's end of
    // the error pipe, which the parent sees as EOF.
    int gate[2], error[2];
    if (pipe2(gate, O_CLOEXEC) < 0) throw std::system_error(errno, std::generic_category(), "pipe2");
    if (pipe2(error, O_CLOEXEC) < 0) {
        int e = errno;
        close(gate[0]);
        close(gate[1]);
        throw std::system_error(e, std::generic_category(), "pipe2");
    }

    pid_ = fork();
    if (pid_ < 0) {
        int e = errno;
        for (int fd : {gate[0], gate[1], error[0], error[1]}) close(fd);
        throw std::system_error(e, std::generic_category(), "fork");
    }

    if (pid_ == 0) {
        close(gate[1]);
        close(error[0]);
        signal(SIGINT, SIG_DFL);  // the parent ignores these while the program runs
        signal(SIGQUIT, SIG_DFL);

        char go;
        if (read(gate[0], &go, 1) != 1) _exit(127);  // parent gave up before releasing us
        for (const auto& [key, value] : env) setenv(key.c_str(), value.c_str(), 1);
        execvp(argv[0], argv);

        int e = errno;
        (void)!write(error[1], &e, sizeof e);
        _exit(127);
    }

    close(gate[0]);
    close(error[1]);
    gate_fd_ = gate[1];
    error_fd_ = error[0];
}

PausedChild::~PausedChild() {
    close_fd(gate_fd_);  // an unreleased child reads EOF and exits
    close_fd(error_fd_);
    if (!released_ && pid_ > 0) waitpid(pid_, nullptr, 0);
}

int PausedChild::release() {
    released_ = true;
    char go = 1;
    if (write(gate_fd_, &go, 1) != 1) return errno;
    close_fd(gate_fd_);

    int exec_errno = 0;
    ssize_t n;
    do {
        n = read(error_fd_, &exec_errno, sizeof exec_errno);
    } while (n < 0 && errno == EINTR);
    close_fd(error_fd_);

    if (n == sizeof exec_errno) {
        waitpid(pid_, nullptr, 0);  // reap the child that failed to exec
        return exec_errno;
    }
    return 0;
}
