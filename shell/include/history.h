#ifndef HISTORY_H
#define HISTORY_H

#include <string>

// Command history. There is one store, readline's, so the up-arrow, Ctrl+R and the `history` builtin all see
// the same list. It is loaded from and saved to a file between sessions.
namespace History {

// $ALPSHELL_HISTFILE if set, otherwise $HOME/.alpshell_history ("" if neither is available).
std::string filePath();

// Reads the history file into readline's list. A missing file is not an error.
void load();

// Adds a command unless it is empty or the same as the previous one.
void add(const std::string& line);

// Writes the list back to the history file (the last 1000 entries are kept).
void save();

}  // namespace History

#endif // HISTORY_H
