#include "history.h"

#include <readline/history.h>

#include <cstdlib>

namespace History {

namespace {
constexpr int kMaxEntries = 1000;
}

std::string filePath() {
    if (const char* explicit_path = std::getenv("ALPSHELL_HISTFILE")) {
        if (*explicit_path != '\0') return explicit_path;
    }
    if (const char* home = std::getenv("HOME")) {
        if (*home != '\0') return std::string(home) + "/.alpshell_history";
    }
    return "";
}

void load() {
    stifle_history(kMaxEntries);
    const std::string path = filePath();
    if (!path.empty()) read_history(path.c_str());
}

void add(const std::string& line) {
    if (line.empty()) return;
    HIST_ENTRY* last = history_get(history_base + history_length - 1);
    if (last != nullptr && line == last->line) return;
    add_history(line.c_str());
}

void save() {
    const std::string path = filePath();
    if (!path.empty()) write_history(path.c_str());
}

}  // namespace History
