#include "builtins.h"

#include <limits.h>
#include <readline/history.h>
#include <signal.h>
#include <unistd.h>

#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "job_control.h"
#include "shell.h"

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

void out(const std::string& s) { writeFd(STDOUT_FILENO, s); }
void err(const std::string& s) { writeFd(STDERR_FILENO, "alpshell: " + s + "\n"); }

std::string currentDirectory() {
    char buf[PATH_MAX];
    return getcwd(buf, sizeof buf) != nullptr ? std::string(buf) : std::string();
}

// ------------------------------------------------------------ cd pwd echo

int builtinCd(Shell& shell, const std::vector<std::string>& argv) {
    if (argv.size() > 2) {
        err("cd: too many arguments");
        return 1;
    }
    std::string target;
    bool print_target = false;
    if (argv.size() == 1) {
        const auto home = shell.vars().get("HOME");
        if (!home || home->empty()) {
            err("cd: HOME not set");
            return 1;
        }
        target = *home;
    } else if (argv[1] == "-") {
        const auto old = shell.vars().get("OLDPWD");
        if (!old || old->empty()) {
            err("cd: OLDPWD not set");
            return 1;
        }
        target = *old;
        print_target = true;
    } else {
        target = argv[1];
    }
    const std::string before = currentDirectory();
    if (chdir(target.c_str()) != 0) {
        err("cd: " + target + ": " + std::strerror(errno));
        return 1;
    }
    if (!before.empty()) shell.vars().exportValue("OLDPWD", before);
    const std::string after = currentDirectory();
    if (!after.empty()) shell.vars().exportValue("PWD", after);
    if (print_target) out(after + "\n");
    return 0;
}

int builtinPwd(Shell&, const std::vector<std::string>&) {
    const std::string dir = currentDirectory();
    if (dir.empty()) {
        err(std::string("pwd: ") + std::strerror(errno));
        return 1;
    }
    out(dir + "\n");
    return 0;
}

int builtinEcho(Shell&, const std::vector<std::string>& argv) {
    size_t i = 1;
    bool newline = true;
    while (i < argv.size() && argv[i] == "-n") {
        newline = false;
        ++i;
    }
    std::string line;
    for (; i < argv.size(); ++i) {
        line += argv[i];
        if (i + 1 < argv.size()) line += ' ';
    }
    if (newline) line += '\n';
    out(line);
    return 0;
}

// ------------------------------------------------------------ export unset exit history

int builtinExport(Shell& shell, const std::vector<std::string>& argv) {
    if (argv.size() == 1) {
        for (const auto& kv : shell.vars().exported()) out("declare -x " + kv.first + "=\"" + kv.second + "\"\n");
        return 0;
    }
    int status = 0;
    for (size_t i = 1; i < argv.size(); ++i) {
        const std::string& arg = argv[i];
        const size_t eq = arg.find('=');
        const std::string name = arg.substr(0, eq);
        if (!Variables::isValidName(name)) {
            err("export: `" + arg + "': not a valid identifier");
            status = 1;
        } else if (eq == std::string::npos) {
            shell.vars().exportExisting(name);
        } else {
            shell.vars().exportValue(name, arg.substr(eq + 1));
        }
    }
    return status;
}

// Like bash 5.2, a name that is not a valid identifier is silently ignored (status 0).
int builtinUnset(Shell& shell, const std::vector<std::string>& argv) {
    for (size_t i = 1; i < argv.size(); ++i) {
        if (Variables::isValidName(argv[i])) shell.vars().unset(argv[i]);
    }
    return 0;
}

int builtinExit(Shell& shell, const std::vector<std::string>& argv) {
    if (argv.size() > 2) {
        err("exit: too many arguments");
        return 1;  // like bash: the shell does not exit
    }
    int code = shell.lastStatus();
    if (argv.size() == 2) {
        char* end = nullptr;
        errno = 0;
        const long value = std::strtol(argv[1].c_str(), &end, 10);
        if (argv[1].empty() || *end != '\0' || errno == ERANGE) {
            err("exit: " + argv[1] + ": numeric argument required");
            code = 2;
        } else {
            code = static_cast<int>(value & 0xFF);
        }
    }
    shell.requestExit(code);
    return code;
}

int builtinHistory(Shell&, const std::vector<std::string>&) {
    HIST_ENTRY** entries = history_list();
    if (entries == nullptr) return 0;
    for (int i = 0; entries[i] != nullptr; ++i) {
        char number[16];
        std::snprintf(number, sizeof number, "%5d  ", history_base + i);
        out(std::string(number) + entries[i]->line + "\n");
    }
    return 0;
}

// ------------------------------------------------------------ jobs fg bg kill wait

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

int builtinJobs(Shell&, const std::vector<std::string>&) {
    JobControl::print_jobs();
    return 0;
}

int builtinFg(Shell&, const std::vector<std::string>& argv) {
    if (!JobControl::enabled()) {
        err("fg: no job control");
        return 1;
    }
    Job* j = selectJob(argv, "fg");
    if (j == nullptr) return 1;
    out(j->command + "\n");
    return JobControl::put_in_foreground(j, true);
}

int builtinBg(Shell&, const std::vector<std::string>& argv) {
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
    out("[" + std::to_string(j->id) + "]+ " + j->command + " &\n");
    return 0;
}

int builtinKill(Shell&, const std::vector<std::string>& argv) {
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
        if (!target.empty() && target[0] == '%') {
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
        if (target.empty() || *end != '\0') {
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

// wait: with no arguments waits for every background job; with arguments (pid or %job) for those jobs and
// returns the status of the last one.
int builtinWait(Shell&, const std::vector<std::string>& argv) {
    if (argv.size() == 1) {
        for (Job* j : JobControl::background_jobs()) JobControl::wait_for(j);
        return 0;
    }
    int status = 0;
    for (size_t i = 1; i < argv.size(); ++i) {
        Job* j = JobControl::find_by_spec(argv[i]);
        if (j == nullptr) {
            char* end = nullptr;
            const long pid = std::strtol(argv[i].c_str(), &end, 10);
            if (!argv[i].empty() && *end == '\0') j = JobControl::find_by_pid(static_cast<pid_t>(pid));
        }
        if (j == nullptr) {
            err("wait: " + argv[i] + ": not a child of this shell");
            status = 127;
            continue;
        }
        status = JobControl::wait_for(j);
    }
    return status;
}

struct Entry {
    const char* name;
    int (*run)(Shell&, const std::vector<std::string>&);
};

const Entry kBuiltins[] = {
    {"cd", builtinCd},         {"pwd", builtinPwd},     {"echo", builtinEcho},   {"export", builtinExport},
    {"unset", builtinUnset},   {"exit", builtinExit},   {"history", builtinHistory}, {"jobs", builtinJobs},
    {"fg", builtinFg},         {"bg", builtinBg},       {"kill", builtinKill},   {"wait", builtinWait},
};

}  // namespace

const std::vector<std::string>& builtinNames() {
    static const std::vector<std::string> names = [] {
        std::vector<std::string> v;
        for (const Entry& e : kBuiltins) v.push_back(e.name);
        return v;
    }();
    return names;
}

bool isBuiltin(const std::string& name) {
    for (const Entry& e : kBuiltins)
        if (name == e.name) return true;
    return false;
}

int runBuiltin(Shell& shell, const std::vector<std::string>& argv) {
    for (const Entry& e : kBuiltins)
        if (argv[0] == e.name) return e.run(shell, argv);
    return 127;
}
