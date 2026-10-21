#include "completion.h"

#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstdlib>
#include <cstring>
#include <set>
#include <sstream>
#include <string>
#include <vector>

extern "C" {
#include <readline/readline.h>
}

#include "builtins.h"

namespace {

// True if the word being completed is where a command name goes: at the start of the line, or after | ; & .
bool inCommandPosition(int start) {
    int i = start - 1;
    while (i >= 0 && (rl_line_buffer[i] == ' ' || rl_line_buffer[i] == '\t')) --i;
    if (i < 0) return true;
    const char c = rl_line_buffer[i];
    return c == '|' || c == ';' || c == '&';
}

char* commandGenerator(const char* text, int state) {
    static std::vector<std::string> matches;
    static std::size_t next = 0;
    if (state == 0) {
        std::set<std::string> unique;
        const std::string prefix = text;
        for (const std::string& name : builtinNames())
            if (name.compare(0, prefix.size(), prefix) == 0) unique.insert(name);

        const char* path_env = std::getenv("PATH");
        std::stringstream paths(path_env != nullptr ? path_env : "/bin:/usr/bin");
        std::string dir;
        while (std::getline(paths, dir, ':')) {
            if (dir.empty()) continue;
            DIR* d = opendir(dir.c_str());
            if (d == nullptr) continue;
            while (const dirent* entry = readdir(d)) {
                if (std::strncmp(entry->d_name, prefix.c_str(), prefix.size()) != 0) continue;
                const std::string full = dir + "/" + entry->d_name;
                struct stat st;
                if (stat(full.c_str(), &st) == 0 && S_ISREG(st.st_mode) && access(full.c_str(), X_OK) == 0) {
                    unique.insert(entry->d_name);
                }
            }
            closedir(d);
        }
        matches.assign(unique.begin(), unique.end());
        next = 0;
    }
    if (next < matches.size()) return strdup(matches[next++].c_str());
    return nullptr;
}

}  // namespace

char** alpshell_completion(const char* text, int start, int /*end*/) {
    if (inCommandPosition(start)) {
        // If nothing matches, readline falls back to file names, so "./prog<TAB>" still works.
        return rl_completion_matches(text, commandGenerator);
    }
    return nullptr;  // readline's default: file names
}
