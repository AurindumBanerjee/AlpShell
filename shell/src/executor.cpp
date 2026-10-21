#include "executor.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "job_control.h"

namespace {

void writeErr(const std::string& s) {
    const char* p = s.data();
    size_t left = s.size();
    while (left > 0) {
        ssize_t n = write(STDERR_FILENO, p, left);
        if (n < 0) {
            if (errno == EINTR) continue;
            return;
        }
        p += n;
        left -= static_cast<size_t>(n);
    }
}

int openFor(const ResolvedRedir& r) {
    switch (r.op) {
        case RedirOp::In: return open(r.path.c_str(), O_RDONLY);
        case RedirOp::Out:
        case RedirOp::ErrOut: return open(r.path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0666);
        default: return open(r.path.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0666);
    }
}

// Runs in the forked child and never returns.
[[noreturn]] void runChild(const ProcessSpec& spec, size_t index, int prev_read, const int fds[2], pid_t pgid,
                           bool background, bool job_control) {
    if (job_control) {
        // Join the pipeline's group and, for a foreground job, take the terminal. The parent does the same;
        // doing it on both sides closes the race on who runs first. This must happen before the signals
        // are reset: a background process that calls tcsetpgrp would be stopped by SIGTTOU.
        const pid_t group = pgid != 0 ? pgid : getpid();
        setpgid(0, group);
        if (!background) tcsetpgrp(JobControl::terminal_fd(), group);
    }
    JobControl::reset_signals_in_child(background && !job_control);

    if (background && !job_control && index == 0) {
        // POSIX: an asynchronous command in a shell without job control reads from /dev/null.
        const int null_fd = open("/dev/null", O_RDONLY);
        if (null_fd >= 0) {
            dup2(null_fd, STDIN_FILENO);
            close(null_fd);
        }
    }
    if (prev_read != -1) {
        dup2(prev_read, STDIN_FILENO);
        close(prev_read);
    }
    if (fds[1] != -1) {
        close(fds[0]);
        dup2(fds[1], STDOUT_FILENO);
        close(fds[1]);
    }
    if (!applyRedirections(spec.redirs)) _exit(1);
    if (spec.argv.empty()) _exit(0);

    std::vector<char*> args;
    for (const std::string& a : spec.argv) args.push_back(const_cast<char*>(a.c_str()));
    args.push_back(nullptr);
    execvp(args[0], args.data());

    const int err = errno;
    const std::string& cmd = spec.argv[0];
    if (err == ENOENT) {
        writeErr("alpshell: " + cmd + ": " + (cmd.find('/') == std::string::npos ? "command not found" : std::strerror(err)) + "\n");
        _exit(127);
    }
    writeErr("alpshell: " + cmd + ": " + std::strerror(err) + "\n");
    _exit(126);
}

}  // namespace

bool applyRedirections(const std::vector<ResolvedRedir>& redirs) {
    for (const ResolvedRedir& r : redirs) {
        if (r.op == RedirOp::ErrToOut) {
            dup2(STDOUT_FILENO, STDERR_FILENO);
            continue;
        }
        if (r.op == RedirOp::OutToErr) {
            dup2(STDERR_FILENO, STDOUT_FILENO);
            continue;
        }
        const int fd = openFor(r);
        if (fd < 0) {
            writeErr("alpshell: " + r.path + ": " + std::strerror(errno) + "\n");
            return false;
        }
        const int target = r.op == RedirOp::In ? STDIN_FILENO
                           : (r.op == RedirOp::ErrOut || r.op == RedirOp::ErrAppend) ? STDERR_FILENO
                                                                                     : STDOUT_FILENO;
        if (fd != target) {
            dup2(fd, target);
            close(fd);
        }
    }
    return true;
}

PipelineResult runPipeline(const std::vector<ProcessSpec>& procs, bool background, const std::string& text) {
    PipelineResult result;
    const size_t n = procs.size();
    if (n == 0) return result;

    const bool job_control = JobControl::enabled();
    std::vector<pid_t> pids;
    pid_t pgid = 0;
    int prev_read = -1;

    // Do not let the children inherit text that is still sitting in our stdio buffers.
    std::fflush(nullptr);

    auto fail = [&](const char* what, int fds[2]) {
        const int err = errno;
        if (fds[0] != -1) close(fds[0]);
        if (fds[1] != -1) close(fds[1]);
        if (prev_read != -1) close(prev_read);
        // Children that were already started must not be left running with half a pipeline.
        JobControl::abort_launch(pgid, pids);
        writeErr(std::string("alpshell: ") + what + ": " + std::strerror(err) + "\n");
        result.launched = false;
        result.status = 1;
        return result;
    };

    for (size_t i = 0; i < n; ++i) {
        int fds[2] = {-1, -1};
        if (i + 1 < n && pipe2(fds, O_CLOEXEC) < 0) return fail("pipe", fds);

        const pid_t pid = fork();
        if (pid < 0) return fail("fork", fds);
        if (pid == 0) runChild(procs[i], i, prev_read, fds, pgid, background, job_control);

        if (job_control) {
            if (pgid == 0) pgid = pid;
            setpgid(pid, pgid);
        }
        pids.push_back(pid);
        if (prev_read != -1) close(prev_read);
        if (fds[1] != -1) close(fds[1]);
        prev_read = fds[0];
    }

    result.last_pid = pids.back();
    Job* job = JobControl::add_job(job_control ? pgid : 0, pids, text, !background);
    if (!background) result.status = JobControl::put_in_foreground(job, false, &result.stopped);
    return result;
}

PipelineResult executePipeline(const std::vector<Command>& commands, bool background, const std::string& text) {
    std::vector<ProcessSpec> specs;
    for (const Command& c : commands) {
        ProcessSpec s;
        s.argv.push_back(c.executable);
        s.argv.insert(s.argv.end(), c.arguments.begin(), c.arguments.end());
        if (!c.inputFile.empty()) s.redirs.push_back({RedirOp::In, c.inputFile});
        if (!c.outputFile.empty()) s.redirs.push_back({c.appendOutput ? RedirOp::Append : RedirOp::Out, c.outputFile});
        specs.push_back(std::move(s));
    }
    return runPipeline(specs, background, text);
}
