#include "prompt.h"

#include <limits.h>
#include <unistd.h>

#include <cstdlib>
#include <cstring>

namespace {

// \001 and \002 are readline's RL_PROMPT_START_IGNORE and RL_PROMPT_END_IGNORE.
#define IGNORE_BEGIN "\001"
#define IGNORE_END "\002"
#define ESC_RESET IGNORE_BEGIN "\033[0m" IGNORE_END
#define ESC_CYAN IGNORE_BEGIN "\033[36m" IGNORE_END
#define ESC_GREEN_BOLD IGNORE_BEGIN "\033[32m\033[1m" IGNORE_END
#define ESC_RED_BOLD IGNORE_BEGIN "\033[31m\033[1m" IGNORE_END

}  // namespace

std::string get_prompt(bool syntax_error) {
    char cwd[PATH_MAX];
    std::string dir = getcwd(cwd, sizeof cwd) != nullptr ? cwd : "?";

    // Show the home directory as ~ (only for the directory itself or paths below it).
    if (const char* home = std::getenv("HOME")) {
        const std::size_t n = std::strlen(home);
        if (n > 0 && dir.compare(0, n, home) == 0 && (dir.size() == n || dir[n] == '/')) {
            dir.replace(0, n, "~");
        }
    }
    return ESC_CYAN + dir + ESC_RESET " " + (syntax_error ? ESC_RED_BOLD : ESC_GREEN_BOLD) + "$ " ESC_RESET;
}

std::string get_continuation_prompt() { return "> "; }
