// AlpShell entry point.
//
//   alpshell                 interactive shell (when stdin is a terminal), otherwise runs stdin
//   alpshell script.sh       runs a script
//   alpshell -c 'command'    runs one command string
#include <signal.h>
#include <unistd.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

extern "C" {
#include <readline/history.h>
#include <readline/readline.h>
}

#include "completion.h"
#include "history.h"
#include "job_control.h"
#include "prompt.h"
#include "shell.h"
#include "syntax_highlight.h"

namespace {

volatile sig_atomic_t g_interrupted = 0;

// Ctrl+C while a line is being edited: drop the line and show a fresh prompt. The shell is not killed.
// The handler only sets a flag (that is all a signal handler can safely do); readline's event hook, which
// readline calls about ten times a second while it waits for a key, notices the flag and ends the read.
void onSigintAtPrompt(int) { g_interrupted = 1; }

int endReadOnInterrupt() {
    if (g_interrupted) rl_done = 1;
    return 0;
}

// readline() with SIGINT turned into "cancel this line". At every other time the shell ignores SIGINT.
char* readLine(const char* prompt) {
    if (!JobControl::enabled()) return readline(prompt);
    struct sigaction handler;
    struct sigaction previous;
    handler.sa_handler = onSigintAtPrompt;
    sigemptyset(&handler.sa_mask);
    handler.sa_flags = SA_RESTART;
    sigaction(SIGINT, &handler, &previous);
    char* line = readline(prompt);
    sigaction(SIGINT, &previous, nullptr);
    return line;
}

// Runs every line of a script or command string. A line that ends inside a quote or after an operator is
// joined with the next one.
int runStream(Shell& shell, std::istream& in) {
    std::string pending;
    std::string line;
    while (std::getline(in, line)) {
        const std::string text = pending.empty() ? line : pending + "\n" + line;
        const Shell::LineResult result = shell.runLine(text);
        if (result == Shell::LineResult::Incomplete) {
            pending = text;
            continue;
        }
        pending.clear();
        JobControl::reap();  // collect finished background jobs (silently: this shell is not interactive)
        JobControl::notify();
        if (result == Shell::LineResult::Exit) return shell.exitCode();
    }
    if (!pending.empty()) shell.reportUnexpectedEof();
    return shell.exitCode();
}

int runInteractive(Shell& shell) {
    JobControl::init(true);
    rl_catch_signals = 0;  // the shell handles signals itself: see JobControl::init and readLine
    rl_event_hook = endReadOnInterrupt;
    rl_attempted_completion_function = alpshell_completion;
    rl_redisplay_function = redisplay_hook;
    rl_char_is_quoted_p = &is_quoted;
    History::load();

    std::string pending;  // the start of a command that continues on the next line
    while (true) {
        JobControl::reap();    // collect finished or stopped background jobs ...
        JobControl::notify();  // ... and tell the user before the prompt
        set_syntax_highlight(pending.empty());
        const std::string prompt = pending.empty() ? get_prompt() : get_continuation_prompt();

        g_interrupted = 0;
        char* raw = readLine(prompt.c_str());
        if (raw == nullptr) {  // Ctrl+D
            std::cout << (pending.empty() ? "exit\n" : "\n") << std::flush;
            if (!pending.empty()) shell.reportUnexpectedEof();
            break;
        }
        const std::string line = raw;
        std::free(raw);
        if (g_interrupted) {  // Ctrl+C: forget the line, and a command that was being continued as well
            pending.clear();
            std::cout << std::endl;
            continue;
        }

        const std::string text = pending.empty() ? line : pending + "\n" + line;
        const Shell::LineResult result = shell.runLine(text);
        if (result == Shell::LineResult::Incomplete) {
            pending = text;
            continue;
        }
        pending.clear();
        std::string for_history = text;
        std::replace(for_history.begin(), for_history.end(), '\n', ' ');
        History::add(for_history);
        if (result == Shell::LineResult::Exit) break;
    }
    History::save();
    return shell.exitCode();
}

void usage(FILE* out, const char* program) {
    std::fprintf(out, "usage: %s [-c command | script_file]\n", program);
}

}  // namespace

int main(int argc, char* argv[]) {
    std::string command;
    bool have_command = false;
    const char* script = nullptr;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "-c") {
            if (i + 1 >= argc) {
                usage(stderr, argv[0]);
                return 2;
            }
            command = argv[++i];
            have_command = true;
        } else if (arg == "-h" || arg == "--help") {
            usage(stdout, argv[0]);
            return 0;
        } else if (script == nullptr && !arg.empty() && arg[0] != '-') {
            script = argv[i];
        } else {
            usage(stderr, argv[0]);
            return 2;
        }
    }
    if (have_command && script != nullptr) {
        usage(stderr, argv[0]);
        return 2;
    }

    const bool interactive = !have_command && script == nullptr && isatty(STDIN_FILENO);
    Shell shell(interactive);

    int status;
    if (interactive) {
        status = runInteractive(shell);
    } else if (have_command) {
        std::istringstream in(command);
        status = runStream(shell, in);
    } else if (script != nullptr) {
        std::ifstream in(script);
        if (!in) {
            std::perror((std::string("alpshell: ") + script).c_str());
            return 127;
        }
        status = runStream(shell, in);
    } else {
        status = runStream(shell, std::cin);
    }
    return status & 0xFF;
}
