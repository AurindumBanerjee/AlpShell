#ifndef BUILTINS_H
#define BUILTINS_H

#include <string>
#include <vector>

// Builtins that run inside the shell process because they act on the shell's own state.

// True for the builtins that deal with jobs: jobs, fg, bg, kill.
bool isJobBuiltin(const std::string& name);

// Runs one of them. argv[0] is the builtin's name. Returns its exit status.
int runJobBuiltin(const std::vector<std::string>& argv);

#endif // BUILTINS_H
