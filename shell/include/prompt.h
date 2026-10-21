#ifndef PROMPT_H
#define PROMPT_H

#include <string>

// The primary prompt: "~/dir $ ". Colour escapes are wrapped in \001 ... \002 so that readline does not
// count them when it works out the width of the prompt (without this, long lines wrap in the wrong place).
// With `syntax_error` the "$" is red instead of green.
std::string get_prompt(bool syntax_error = false);

// The prompt shown while a command continues on the next line (inside a quote, after a trailing | or &&).
std::string get_continuation_prompt();

#endif // PROMPT_H
