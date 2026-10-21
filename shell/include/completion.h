#ifndef COMPLETION_H
#define COMPLETION_H

// Tab completion for readline: commands (builtins and executables on PATH) in command position, file names
// everywhere else. Assign to rl_attempted_completion_function.
char** alpshell_completion(const char* text, int start, int end);

#endif // COMPLETION_H
