#include "procfs.hpp"

#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>

#include <cctype>
#include <cstdlib>
#include <sstream>

namespace {

std::optional<std::string> read_file(const std::string& path) {
    int fd = open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) return std::nullopt;
    std::string text;
    char buf[4096];
    ssize_t n;
    while ((n = read(fd, buf, sizeof buf)) > 0) text.append(buf, n);
    close(fd);
    if (n < 0) return std::nullopt;
    return text;
}

// Finds a line "key: value" (spaces allowed before the colon) and returns value.
std::optional<uint64_t> find_value(const std::string& text, const std::string& key) {
    for (size_t pos = text.find(key); pos != std::string::npos; pos = text.find(key, pos + 1)) {
        if (pos != 0 && text[pos - 1] != '\n') continue;  // must start a line
        size_t colon = pos + key.size();
        while (colon < text.size() && text[colon] == ' ') ++colon;
        if (colon < text.size() && text[colon] == ':')
            return std::strtoull(text.c_str() + colon + 1, nullptr, 10);
    }
    return std::nullopt;
}

}  // namespace

bool parse_stat(const std::string& text, ThreadSample& out) {
    // "tid (comm) state ...": comm may contain spaces or ')', so split at the last ')'.
    size_t open = text.find('(');
    size_t close = text.rfind(')');
    if (open == std::string::npos || close == std::string::npos || close < open) return false;
    out.name = text.substr(open + 1, close - open - 1);

    // Field 3 (state) is the first token after ')'; we want field 39 (processor).
    std::istringstream rest(text.substr(close + 1));
    std::string field;
    for (int i = 3; i <= 39; ++i)
        if (!(rest >> field)) return false;
    out.core = std::atoi(field.c_str());
    return true;
}

bool parse_status(const std::string& text, ThreadSample& out) {
    auto vol = find_value(text, "voluntary_ctxt_switches");
    auto invol = find_value(text, "nonvoluntary_ctxt_switches");
    if (!vol || !invol) return false;
    out.voluntary_switches = *vol;
    out.involuntary_switches = *invol;
    return true;
}

bool parse_schedstat(const std::string& text, ThreadSample& out) {
    std::istringstream in(text);
    return static_cast<bool>(in >> out.run_ns >> out.wait_ns);
}

std::optional<uint64_t> parse_sched_migrations(const std::string& text) {
    return find_value(text, "se.nr_migrations");
}

std::vector<pid_t> list_threads(pid_t pid) {
    std::vector<pid_t> tids;
    std::string path = "/proc/" + std::to_string(pid) + "/task";
    DIR* dir = opendir(path.c_str());
    if (!dir) return tids;
    while (dirent* entry = readdir(dir)) {
        if (std::isdigit(static_cast<unsigned char>(entry->d_name[0])))
            tids.push_back(std::atoi(entry->d_name));
    }
    closedir(dir);
    return tids;
}

std::optional<ThreadSample> sample_thread(pid_t pid, pid_t tid) {
    std::string dir = "/proc/" + std::to_string(pid) + "/task/" + std::to_string(tid) + "/";
    auto stat = read_file(dir + "stat");
    auto status = read_file(dir + "status");
    auto schedstat = read_file(dir + "schedstat");
    if (!stat || !status || !schedstat) return std::nullopt;

    ThreadSample s;
    s.tid = tid;
    if (!parse_stat(*stat, s) || !parse_status(*status, s) || !parse_schedstat(*schedstat, s))
        return std::nullopt;
    if (auto sched = read_file(dir + "sched")) s.migrations = parse_sched_migrations(*sched);
    return s;
}
