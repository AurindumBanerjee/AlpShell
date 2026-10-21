#ifndef EXPAND_H
#define EXPAND_H

#include <sys/types.h>

#include <stdexcept>
#include <string>
#include <vector>

#include "lexer.h"
#include "variables.h"

// Word expansion, applied after quoting has been decided by the lexer:
//
//   '...'    nothing is expanded
//   "..."    $VAR, ${VAR}, $? are expanded; the result is never split or globbed
//   unquoted ~ (at the start of a word), $VAR, ${VAR}, $? are expanded, the result is split into fields at
//            whitespace, and fields with * ? [ are matched against file names with glob(3)
//
// Backslash-escaped characters were turned into single-quoted parts by the lexer, so they stay literal.
// Not supported: $1, $@, $#, ${VAR:-x} and the other parameter operators (a ${...} that is not a plain name
// is an error), command substitution, ~user.

struct ExpandError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

struct ExpandContext {
    const Variables& vars;
    int last_status = 0;    // $?
    pid_t last_bg_pid = 0;  // $!
};

// Expands one word into zero or more fields (zero for an unquoted variable that is empty or unset).
std::vector<std::string> expandWord(const Word& word, const ExpandContext& ctx);

// Expands one word into exactly one string: no field splitting and no globbing. Used for redirection
// targets and assignment values.
std::string expandWordSingle(const Word& word, const ExpandContext& ctx);

// Recognises NAME=value at the start of a word (NAME unquoted). On success fills `name` and `value`.
bool splitAssignment(const Word& word, std::string& name, Word& value);

#endif // EXPAND_H
