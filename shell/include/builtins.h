#ifndef BUILTINS_H
#define BUILTINS_H

#include <string>
#include <vector>

class Shell;

// Builtins: cd pwd echo export unset exit history wait jobs fg bg kill.
// They run inside the shell process, because they act on the shell's own state, except when they are part of
// a pipeline or run in the background: then they run in a forked child like any other command.

bool isBuiltin(const std::string& name);

// The names of all builtins (used by tab completion).
const std::vector<std::string>& builtinNames();

// Runs a builtin. argv[0] is its name. Returns its exit status.
int runBuiltin(Shell& shell, const std::vector<std::string>& argv);

#endif // BUILTINS_H
