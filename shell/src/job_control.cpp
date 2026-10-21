#include "job_control.h"

#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iterator>

// ---------------------------------------------------------------- Job

bool Job::all_finished() const {
    return std::all_of(procs.begin(), procs.end(), [](const Process& p) { return p.finished; });
}

JobState Job::state() const {
    if (all_finished()) return JobState::Done;
    // Stopped only when nothing is left running: every process that has not exited is stopped.
    const bool any_running = std::any_of(procs.begin(), procs.end(), [](const Process& p) { return !p.finished && !p.stopped; });
    return any_running ? JobState::Running : JobState::Stopped;
}

int Job::exit_status() const {
    if (procs.empty()) return 0;
    const Process& last = procs.back();
    if (last.finished) {
        if (WIFEXITED(last.wait_status)) return WEXITSTATUS(last.wait_status);
        if (WIFSIGNALED(last.wait_status)) return 128 + WTERMSIG(last.wait_status);
        return 0;
    }
    if (last.stopped) return 128 + SIGTSTP;
    return 0;
}

bool Job::update(pid_t pid, int wait_status) {
    for (Process& p : procs) {
        if (p.pid != pid) continue;
        if (WIFSTOPPED(wait_status)) {
            p.stopped = true;
        } else if (WIFCONTINUED(wait_status)) {
            p.stopped = false;
        } else {
            p.finished = true;
            p.stopped = false;
            p.wait_status = wait_status;
        }
        return true;
    }
    return false;
}

// ---------------------------------------------------------------- state

namespace {

std::list<Job> g_jobs;  // std::list: pointers to jobs stay valid while other jobs come and go
bool g_enabled = false;
int g_tty = -1;
pid_t g_shell_pgid = 0;
struct termios g_shell_tmodes;

void write_all(int fd, const std::string& s) {
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

int next_id() {
    int id = 0;
    for (const Job& j : g_jobs) id = std::max(id, j.id);
    return id + 1;
}

// Collects at most one state change for the job. Returns true if one was recorded. With block == false it
// returns false when nothing has changed; with block == true it waits.
bool poll_once(Job& j, bool block) {
    const int flags = WUNTRACED | WCONTINUED | (block ? 0 : WNOHANG);
    for (;;) {
        int st = 0;
        if (j.pgid > 0) {
            const pid_t p = waitpid(-j.pgid, &st, flags);
            if (p > 0) {
                j.update(p, st);
                return true;
            }
            if (p == 0) return false;
            if (errno == EINTR) continue;
            for (Process& pr : j.procs) {  // ECHILD: somebody else reaped them; nothing left to wait for
                pr.finished = true;
                pr.stopped = false;
            }
            return false;
        }
        // No process group (scripts): wait for the job's processes one by one.
        for (Process& pr : j.procs) {
            if (pr.finished) continue;
            const pid_t p = waitpid(pr.pid, &st, flags);
            if (p > 0) {
                j.update(p, st);
                return true;
            }
            if (p < 0) {
                if (errno == EINTR) continue;
                pr.finished = true;  // ECHILD
                pr.stopped = false;
                return true;
            }
            if (block) break;
        }
        return false;
    }
}

const char* mark_for(const Job& j, const Job* current, const Job* previous) {
    if (&j == current) return "+";
    if (&j == previous) return "-";
    return " ";
}

std::string state_text(const Job& j) {
    switch (j.state()) {
        case JobState::Running: return "Running";
        case JobState::Stopped: return "Stopped";
        case JobState::Done: break;
    }
    const Process& last = j.procs.back();
    if (WIFSIGNALED(last.wait_status)) return strsignal(WTERMSIG(last.wait_status));
    const int code = j.exit_status();
    return code == 0 ? "Done" : "Exit " + std::to_string(code);
}

// "[1]+  Running                 sleep 100 &"
std::string job_line(const Job& j, const Job* current, const Job* previous) {
    char head[96];
    std::snprintf(head, sizeof head, "[%d]%s  %-22s ", j.id, mark_for(j, current, previous), state_text(j).c_str());
    std::string line = head + j.command;
    if (j.state() == JobState::Running) line += " &";
    return line + "\n";
}

// The + and - marks in job listings go to the two most recent jobs, finished ones included, so a "Done"
// notice for the newest job still shows [1]+. (fg and bg select with current_job(), which skips finished jobs.)
void marker_jobs(const Job*& cur, const Job*& prev) {
    cur = prev = nullptr;
    for (auto it = g_jobs.rbegin(); it != g_jobs.rend(); ++it) {
        if (it->id == 0) continue;
        if (cur == nullptr) {
            cur = &*it;
        } else {
            prev = &*it;
            break;
        }
    }
}

const Job* previous_job(const Job* current) {
    const Job* prev = nullptr;
    for (const Job& j : g_jobs) {
        if (j.id == 0 || j.state() == JobState::Done || &j == current) continue;
        prev = &j;
    }
    return prev;
}

}  // namespace

// ---------------------------------------------------------------- public API

namespace JobControl {

void init(bool interactive) {
    g_enabled = false;
    if (!interactive || !isatty(STDIN_FILENO)) return;
    g_tty = STDIN_FILENO;

    // Wait until we are in the foreground process group (we may have been started in the background).
    while (tcgetpgrp(g_tty) != (g_shell_pgid = getpgrp())) {
        kill(-g_shell_pgid, SIGTTIN);
    }

    signal(SIGINT, SIG_IGN);
    signal(SIGQUIT, SIG_IGN);
    signal(SIGTSTP, SIG_IGN);
    signal(SIGTTIN, SIG_IGN);
    signal(SIGTTOU, SIG_IGN);

    // Put the shell in its own group and take the terminal.
    g_shell_pgid = getpid();
    if (getpgrp() != g_shell_pgid && setpgid(g_shell_pgid, g_shell_pgid) < 0) {
        std::perror("alpshell: cannot put the shell in its own process group");
        return;
    }
    tcsetpgrp(g_tty, g_shell_pgid);
    tcgetattr(g_tty, &g_shell_tmodes);
    g_enabled = true;
}

bool enabled() { return g_enabled; }
int terminal_fd() { return g_tty; }

void reset_signals_in_child(bool ignore_int_and_quit) {
    const int sigs[] = {SIGINT, SIGQUIT, SIGTSTP, SIGTTIN, SIGTTOU, SIGCHLD};
    for (int s : sigs) signal(s, SIG_DFL);
    if (ignore_int_and_quit) {
        signal(SIGINT, SIG_IGN);
        signal(SIGQUIT, SIG_IGN);
    }
    sigset_t none;
    sigemptyset(&none);
    sigprocmask(SIG_SETMASK, &none, nullptr);
}

Job* add_job(pid_t pgid, const std::vector<pid_t>& pids, const std::string& command, bool foreground) {
    Job j;
    j.pgid = pgid;
    j.command = command;
    j.foreground = foreground;
    for (pid_t p : pids) {
        Process pr;
        pr.pid = p;
        j.procs.push_back(pr);
    }
    if (!foreground) j.id = next_id();
    g_jobs.push_back(j);
    return &g_jobs.back();
}

int wait_foreground(Job* j, bool* stopped_out) {
    while (!j->all_finished() && j->state() != JobState::Stopped) {
        if (!poll_once(*j, true)) break;
    }
    const int status = j->exit_status();
    const bool stopped = j->state() == JobState::Stopped;
    if (stopped_out != nullptr) *stopped_out = stopped;

    if (g_enabled) {
        if (stopped) {
            tcgetattr(g_tty, &j->tmodes);  // remember the modes the job left the terminal in
            j->has_tmodes = true;
        }
        tcsetpgrp(g_tty, g_shell_pgid);
        tcsetattr(g_tty, TCSADRAIN, &g_shell_tmodes);
    }

    if (stopped) {
        if (j->id == 0) j->id = next_id();
        j->foreground = false;
        j->reported = JobState::Stopped;
        if (g_enabled) {
            const Job *cur, *prev;
            marker_jobs(cur, prev);
            write_all(STDOUT_FILENO, "\n" + job_line(*j, cur, prev));
        }
        return status;
    }

    if (g_enabled && !j->procs.empty() && WIFSIGNALED(j->procs.back().wait_status) &&
        WTERMSIG(j->procs.back().wait_status) == SIGINT) {
        write_all(STDOUT_FILENO, "\n");  // the terminal echoed ^C but did not move to a new line
    }
    g_jobs.remove_if([j](const Job& other) { return &other == j; });
    return status;
}

int put_in_foreground(Job* j, bool cont, bool* stopped) {
    j->foreground = true;
    if (g_enabled) {
        if (cont && j->has_tmodes) tcsetattr(g_tty, TCSADRAIN, &j->tmodes);
        tcsetpgrp(g_tty, j->pgid);
    }
    if (cont) {
        for (Process& p : j->procs) p.stopped = false;
        j->reported = JobState::Running;
        signal_job(j, SIGCONT);
    }
    return wait_foreground(j, stopped);
}

void put_in_background(Job* j, bool cont) {
    j->foreground = false;
    if (j->id == 0) j->id = next_id();
    if (cont) {
        for (Process& p : j->procs) p.stopped = false;
        j->reported = JobState::Running;
        signal_job(j, SIGCONT);
    }
}

void reap() {
    for (Job& j : g_jobs) {
        if (j.foreground) continue;
        while (poll_once(j, false)) {
        }
    }
}

int wait_for(Job* j) {
    while (!j->all_finished()) {
        int st = 0;
        pid_t p = -1;
        if (j->pgid > 0) {
            p = waitpid(-j->pgid, &st, 0);  // no WUNTRACED: a stopped job keeps `wait` waiting, as in bash
        } else {
            for (Process& pr : j->procs) {
                if (!pr.finished) {
                    p = waitpid(pr.pid, &st, 0);
                    break;
                }
            }
        }
        if (p > 0) {
            j->update(p, st);
        } else if (errno != EINTR) {
            for (Process& pr : j->procs) pr.finished = true;
        }
    }
    j->reported = JobState::Done;  // the user asked for this job: no "Done" notice later
    return j->exit_status();
}

void leave_job_control() {
    g_enabled = false;
    g_jobs.clear();
}

std::vector<Job*> background_jobs() {
    std::vector<Job*> out;
    for (Job& j : g_jobs)
        if (!j.foreground) out.push_back(&j);
    return out;
}

void notify() {
    for (auto it = g_jobs.begin(); it != g_jobs.end();) {
        Job& j = *it;
        if (j.foreground) {
            ++it;
            continue;
        }
        const JobState s = j.state();
        if (s != j.reported && (s == JobState::Done || s == JobState::Stopped)) {
            if (g_enabled) {
                const Job *cur, *prev;
                marker_jobs(cur, prev);
                write_all(STDOUT_FILENO, job_line(j, cur, prev));
            }
            j.reported = s;
        } else if (s == JobState::Running) {
            j.reported = JobState::Running;
        }
        it = (s == JobState::Done) ? g_jobs.erase(it) : std::next(it);
    }
}

void print_jobs() {
    reap();
    const Job *cur, *prev;
    marker_jobs(cur, prev);
    for (const Job& j : g_jobs) {
        if (j.id != 0) write_all(STDOUT_FILENO, job_line(j, cur, prev));
    }
    for (auto it = g_jobs.begin(); it != g_jobs.end();) {
        it = (it->state() == JobState::Done) ? g_jobs.erase(it) : std::next(it);
    }
}

Job* find_by_id(int id) {
    for (Job& j : g_jobs)
        if (j.id == id && id != 0) return &j;
    return nullptr;
}

Job* find_by_pid(pid_t pid) {
    for (Job& j : g_jobs)
        for (const Process& p : j.procs)
            if (p.pid == pid) return &j;
    return nullptr;
}

Job* current_job() {
    Job* cur = nullptr;
    for (Job& j : g_jobs) {
        if (j.id != 0 && j.state() != JobState::Done) cur = &j;
    }
    return cur;
}

Job* find_by_spec(const std::string& spec) {
    if (spec.empty()) return nullptr;
    if (spec == "%%" || spec == "%+") return current_job();
    if (spec == "%-") return const_cast<Job*>(previous_job(current_job()));
    const std::string digits = spec[0] == '%' ? spec.substr(1) : spec;
    if (digits.empty() || digits.find_first_not_of("0123456789") != std::string::npos) return nullptr;
    return find_by_id(std::atoi(digits.c_str()));
}

bool has_jobs() { return !g_jobs.empty(); }

int signal_job(Job* j, int sig) {
    if (j->pgid > 0) return killpg(j->pgid, sig);
    int rc = 0;
    for (const Process& p : j->procs) {
        if (!p.finished && kill(p.pid, sig) != 0) rc = -1;
    }
    return rc;
}

void abort_launch(pid_t pgid, const std::vector<pid_t>& pids) {
    for (pid_t p : pids) kill(p, SIGKILL);
    if (pgid > 0) killpg(pgid, SIGKILL);
    for (pid_t p : pids) {
        while (waitpid(p, nullptr, 0) < 0 && errno == EINTR) {
        }
    }
}

}  // namespace JobControl
