#ifndef SHELL_H
#define SHELL_H

#include <sys/types.h>

#include <string>

#include "executor.h"
#include "parser.h"
#include "variables.h"

// The interpreter: parses a line, expands it, and runs it, keeping the shell's state (variables, the exit
// status of the last foreground pipeline, whether `exit` was requested).
class Shell {
public:
    enum class LineResult {
        Done,        // the line was run (or was empty)
        Incomplete,  // the line ends inside a quote or after an operator: append the next line and try again
        Exit         // the shell should exit now (the `exit` builtin, or a syntax error in a script)
    };

    explicit Shell(bool interactive);

    // Runs one logical line, which may hold several commands separated by ; && || and &.
    LineResult runLine(const std::string& text);

    // Called when the input ended while a line was still incomplete.
    void reportUnexpectedEof();

    Variables& vars() { return vars_; }
    bool interactive() const { return interactive_; }

    int lastStatus() const { return last_status_; }  // $?
    void setLastStatus(int status) { last_status_ = status; }

    // The `exit` builtin asks the shell to stop with `code`.
    void requestExit(int code);
    bool exitRequested() const { return exit_requested_; }
    // The exit status the shell process should return: the requested one, else that of the last command.
    int exitCode() const { return exit_requested_ ? exit_code_ : last_status_; }

private:
    LineResult syntaxError(const std::string& message);
    int runAndOr(const AndOrList& list);
    void runBackground(const AndOrList& list);
    int runPipeline(const Pipeline& pipeline, bool background, const std::string& text);
    bool expandCommand(const SimpleCommand& command, ProcessSpec& spec);
    int runWithoutCommand(const ProcessSpec& spec);
    int runBuiltinInShell(const ProcessSpec& spec);
    void announceBackground(pid_t pid);

    Variables vars_;
    bool interactive_;
    int last_status_ = 0;
    pid_t last_bg_pid_ = 0;
    bool exit_requested_ = false;
    int exit_code_ = 0;
};

#endif // SHELL_H
