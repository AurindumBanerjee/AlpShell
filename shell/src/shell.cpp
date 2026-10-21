#include "shell.h"

#include <fcntl.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <iostream>

#include "builtins.h"
#include "expand.h"
#include "job_control.h"
#include "lexer.h"

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

RedirOp toOp(RedirKind kind) {
    switch (kind) {
        case RedirKind::In: return RedirOp::In;
        case RedirKind::Out: return RedirOp::Out;
        case RedirKind::Append: return RedirOp::Append;
        case RedirKind::ErrOut: return RedirOp::ErrOut;
        case RedirKind::ErrAppend: return RedirOp::ErrAppend;
        case RedirKind::ErrToOut: return RedirOp::ErrToOut;
        case RedirKind::OutToErr: break;
    }
    return RedirOp::OutToErr;
}

}  // namespace

Shell::Shell(bool interactive) : interactive_(interactive) {}

void Shell::requestExit(int code) {
    exit_requested_ = true;
    exit_code_ = code & 0xFF;
}

Shell::LineResult Shell::syntaxError(const std::string& message) {
    writeErr("alpshell: " + message + "\n");
    last_status_ = 2;
    // A script stops at a syntax error (POSIX); an interactive shell just prompts again.
    if (!interactive_) {
        requestExit(2);
        return LineResult::Exit;
    }
    return LineResult::Done;
}

void Shell::reportUnexpectedEof() {
    writeErr("alpshell: syntax error: unexpected end of file\n");
    last_status_ = 2;
    requestExit(2);
}

Shell::LineResult Shell::runLine(const std::string& text) {
    const LexResult lexed = Lexer().tokenize(text);
    if (!lexed.ok()) {
        if (lexed.incomplete) return LineResult::Incomplete;
        return syntaxError(lexed.error);
    }
    const ParseResult parsed = Parser().parse(lexed.tokens, text);
    if (!parsed.ok()) {
        if (parsed.incomplete) return LineResult::Incomplete;
        return syntaxError(parsed.error);
    }
    for (const AndOrList& list : parsed.line.lists) {
        if (list.background) {
            runBackground(list);
        } else {
            runAndOr(list);
        }
        if (exit_requested_) return LineResult::Exit;
    }
    return LineResult::Done;
}

// ---------------------------------------------------------------- lists

// a && b || c: left to right; a skipped command leaves the status of the one before it.
int Shell::runAndOr(const AndOrList& list) {
    int status = last_status_;
    for (const auto& item : list.pipelines) {
        const Connector connector = item.first;
        if (connector == Connector::And && status != 0) continue;
        if (connector == Connector::Or && status == 0) continue;
        status = runPipeline(item.second, /*background=*/false, list.text);
        last_status_ = status;
        if (exit_requested_) break;
    }
    return status;
}

void Shell::announceBackground(pid_t pid) {
    last_bg_pid_ = pid;
    if (!interactive_) return;
    if (Job* job = JobControl::find_by_pid(pid)) {
        const std::string line = "[" + std::to_string(job->id) + "] " + std::to_string(pid) + "\n";
        if (write(STDOUT_FILENO, line.data(), line.size()) < 0) {
            // nothing sensible to do if the terminal is gone
        }
    }
}

void Shell::runBackground(const AndOrList& list) {
    if (list.pipelines.size() == 1) {
        runPipeline(list.pipelines[0].second, /*background=*/true, list.text);
        return;
    }
    // "a && b &" runs the whole list in a forked subshell, which is then the job.
    std::fflush(nullptr);
    const bool job_control = JobControl::enabled();
    const pid_t pid = fork();
    if (pid < 0) {
        writeErr(std::string("alpshell: fork: ") + std::strerror(errno) + "\n");
        last_status_ = 1;
        return;
    }
    if (pid == 0) {
        if (job_control) setpgid(0, 0);
        JobControl::reset_signals_in_child(/*ignore_int_and_quit=*/!job_control);
        JobControl::leave_job_control();
        interactive_ = false;
        if (!job_control) {
            const int null_fd = open("/dev/null", O_RDONLY);
            if (null_fd >= 0) {
                dup2(null_fd, STDIN_FILENO);
                close(null_fd);
            }
        }
        runAndOr(list);
        _exit(exitCode());
    }
    if (job_control) setpgid(pid, pid);
    JobControl::add_job(job_control ? pid : 0, {pid}, list.text, /*foreground=*/false);
    announceBackground(pid);
}

// ---------------------------------------------------------------- one pipeline

bool Shell::expandCommand(const SimpleCommand& command, ProcessSpec& spec) {
    const ExpandContext ctx{vars_, last_status_, last_bg_pid_};
    try {
        std::size_t i = 0;
        // Leading NAME=value words are assignments for this command only.
        for (; i < command.words.size(); ++i) {
            std::string name;
            Word value;
            if (!splitAssignment(command.words[i], name, value)) break;
            spec.env.emplace_back(name, expandWordSingle(value, ctx));
        }
        for (; i < command.words.size(); ++i) {
            const std::vector<std::string> fields = expandWord(command.words[i], ctx);
            spec.argv.insert(spec.argv.end(), fields.begin(), fields.end());
        }
        for (const Redirection& r : command.redirs) {
            ResolvedRedir resolved;
            resolved.op = toOp(r.kind);
            if (r.kind != RedirKind::ErrToOut && r.kind != RedirKind::OutToErr) {
                resolved.path = expandWordSingle(r.target, ctx);
                if (resolved.path.empty()) throw ExpandError("ambiguous redirect");
            }
            spec.redirs.push_back(std::move(resolved));
        }
    } catch (const ExpandError& e) {
        writeErr(std::string("alpshell: ") + e.what() + "\n");
        return false;
    }
    return true;
}

// A command with no program name: "VAR=value" sets a shell variable, "> file" creates the file.
int Shell::runWithoutCommand(const ProcessSpec& spec) {
    {
        RedirectionScope scope(spec.redirs);
        if (!scope.ok()) return 1;
    }
    for (const auto& assignment : spec.env) vars_.set(assignment.first, assignment.second);
    return 0;
}

// A lone builtin runs in the shell process, because cd, export, exit... must change the shell itself.
int Shell::runBuiltinInShell(const ProcessSpec& spec) {
    RedirectionScope scope(spec.redirs);
    if (!scope.ok()) return 1;
    // NAME=value before a builtin is visible to it only for the duration of the call.
    std::vector<std::pair<std::string, std::optional<std::string>>> saved;
    for (const auto& assignment : spec.env) {
        saved.emplace_back(assignment.first, vars_.get(assignment.first));
        vars_.set(assignment.first, assignment.second);
    }
    const int status = runBuiltin(*this, spec.argv);
    for (auto it = saved.rbegin(); it != saved.rend(); ++it) {
        if (it->second) vars_.set(it->first, *it->second);
        else vars_.unset(it->first);
    }
    return status;
}

int Shell::runPipeline(const Pipeline& pipeline, bool background, const std::string& text) {
    std::vector<ProcessSpec> specs;
    for (const SimpleCommand& command : pipeline.commands) {
        ProcessSpec spec;
        if (!expandCommand(command, spec)) {
            last_status_ = 1;
            return 1;
        }
        specs.push_back(std::move(spec));
    }

    if (specs.size() == 1 && !background) {
        const ProcessSpec& only = specs[0];
        if (only.argv.empty()) return last_status_ = runWithoutCommand(only);
        if (isBuiltin(only.argv[0])) return last_status_ = runBuiltinInShell(only);
    }

    // Inside a pipeline, or in the background, a builtin runs in its own forked process.
    for (ProcessSpec& spec : specs) {
        if (!spec.argv.empty() && isBuiltin(spec.argv[0])) {
            const std::vector<std::string> argv = spec.argv;
            spec.builtin = [this, argv]() { return runBuiltin(*this, argv); };
        }
    }

    const PipelineResult result = launchPipeline(specs, background, text);
    if (background) {
        if (result.launched) announceBackground(result.last_pid);
        return 0;
    }
    return result.status;
}
