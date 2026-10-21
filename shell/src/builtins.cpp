#include "builtins.h"

#include <signal.h>
#include <unistd.h>

#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <cstring>

#include "job_control.h"

namespace {

void writeFd(int fd, const std::string& s) {
    const char* p = s.data();
    size_t left = s.size();
    while (left > 0) {
        ssize_t n = write(fd, p, left);
        if (n < 0) {
            if (errno == EINTR) continue;
            return;
        }
        p += n;
        left -= static_cast<size_t>(n);
    }
}

void err(const std::string& s) { writeFd(STDERR_FILENO, "alpshell: " + s + "\n"); }

// "TERM", "SIGTERM", "term" or "15" -> signal number, or -1.
int parseSignal(std::string text) {
    if (text.empty()) return -1;
    if (std::isdigit(static_cast<unsigned char>(text[0]))) {
        char* end = nullptr;
        const long v = std::strtol(text.c_str(), &end, 10);
        return (*end == '\0' && v >= 0 && v < 65) ? static_cast<int>(v) : -1;
    }
    for (char& c : text) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    if (text.compare(0, 3, "SIG") == 0) text = text.substr(3);
    static const struct {
        const char* name;
        int number;
    } table[] = {{"HUP", SIGHUP},   {"INT", SIGINT},   {"QUIT", SIGQUIT}, {"KILL", SIGKILL}, {"USR1", SIGUSR1},
                 {"USR2", SIGUSR2}, {"PIPE", SIGPIPE}, {"ALRM", SIGALRM}, {"TERM", SIGTERM}, {"CHLD", SIGCHLD},
                 {"CONT", SIGCONT}, {"STOP", SIGSTOP}, {"TSTP", SIGTSTP}, {"TTIN", SIGTTIN}, {"TTOU", SIGTTOU}};
    for (const auto& e : table)
        if (text == e.name) return e.number;
    return -1;
}

// Picks the job named by argv[1], or the current job.
Job* selectJob(const std::vector<std::string>& argv, const char* who) {
    if (argv.size() > 1) {
        Job* j = JobControl::find_by_spec(argv[1]);
        if (j == nullptr) err(std::string(who) + ": " + argv[1] + ": no such job");
        return j;
    }
    Job* j = JobControl::current_job();
    if (j == nullptr) err(std::string(who) + ": current: no such job");
    return j;
}

int builtinFg(const std::vector<std::string>& argv) {
    if (!JobControl::enabled()) {
        err("fg: no job control");
        return 1;
    }
    Job* j = selectJob(argv, "fg");
    if (j == nullptr) return 1;
    writeFd(STDOUT_FILENO, j->command + "\n");
    return JobControl::put_in_foreground(j, true);
}

int builtinBg(const std::vector<std::string>& argv) {
    if (!JobControl::enabled()) {
        err("bg: no job control");
        return 1;
    }
    Job* j = selectJob(argv, "bg");
    if (j == nullptr) return 1;
    if (j->state() != JobState::Stopped) {
        err("bg: job " + std::to_string(j->id) + " already in background");
        return 0;
    }
    JobControl::put_in_background(j, true);
    writeFd(STDOUT_FILENO, "[" + std::to_string(j->id) + "]+ " + j->command + " &\n");
    return 0;
}

int builtinKill(const std::vector<std::string>& argv) {
    size_t i = 1;
    int sig = SIGTERM;
    if (i < argv.size() && (argv[i] == "-s" || argv[i] == "-n")) {
        if (i + 1 >= argv.size() || (sig = parseSignal(argv[i + 1])) < 0) {
            err("kill: " + (i + 1 < argv.size() ? argv[i + 1] : std::string("-s")) + ": invalid signal specification");
            return 1;
        }
        i += 2;
    } else if (i < argv.size() && argv[i].size() > 1 && argv[i][0] == '-' && argv[i] != "--") {
        if ((sig = parseSignal(argv[i].substr(1))) < 0) {
            err("kill: " + argv[i].substr(1) + ": invalid signal specification");
            return 1;
        }
        ++i;
    }
    if (i < argv.size() && argv[i] == "--") ++i;
    if (i >= argv.size()) {
        err("kill: usage: kill [-s sigspec | -n signum | -sigspec] pid | %job ...");
        return 2;
    }

    int status = 0;
    for (; i < argv.size(); ++i) {
        const std::string& target = argv[i];
        if (target[0] == '%') {
            Job* j = JobControl::find_by_spec(target);
            if (j == nullptr) {
                err("kill: " + target + ": no such job");
                status = 1;
                continue;
            }
            const bool was_stopped = j->state() == JobState::Stopped;
            if (JobControl::signal_job(j, sig) != 0) {
                err("kill: " + target + ": " + std::strerror(errno));
                status = 1;
            } else if (was_stopped && (sig == SIGTERM || sig == SIGHUP)) {
                JobControl::signal_job(j, SIGCONT);  // a stopped process only dies once it is continued
            }
            continue;
        }
        char* end = nullptr;
        const long pid = std::strtol(target.c_str(), &end, 10);
        if (*end != '\0' || end == target.c_str()) {
            err("kill: " + target + ": arguments must be process or job IDs");
            status = 1;
            continue;
        }
        if (kill(static_cast<pid_t>(pid), sig) != 0) {
            err("kill: (" + target + ") - " + std::strerror(errno));
            status = 1;
        }
    }
    return status;
}

}  // namespace

bool isJobBuiltin(const std::string& name) {
    return name == "jobs" || name == "fg" || name == "bg" || name == "kill";
}

int runJobBuiltin(const std::vector<std::string>& argv) {
    if (argv[0] == "jobs") {
        JobControl::print_jobs();
        return 0;
    }
    if (argv[0] == "fg") return builtinFg(argv);
    if (argv[0] == "bg") return builtinBg(argv);
    return builtinKill(argv);
}
