#ifndef SYNTAX_HIGHLIGHT_H
#define SYNTAX_HIGHLIGHT_H

#include <string>

// While a line is being edited, the "$" of the prompt turns red when the line cannot be parsed.

// True if `line` has a lexical or syntax error (including a line that is still incomplete).
bool line_has_syntax_error(const std::string& line);

// Turns the highlighting on for the primary prompt and off for continuation prompts.
void set_syntax_highlight(bool enabled);

// readline hooks: assign these to rl_redisplay_function and rl_char_is_quoted_p.
extern "C" void redisplay_hook();
int is_quoted(char* text, int index);

#endif // SYNTAX_HIGHLIGHT_H
