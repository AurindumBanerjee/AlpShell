#include "syntax_highlight.h"

#include <cstdio>
#include <cstdlib>

extern "C" {
#include <readline/readline.h>
}

#include "lexer.h"
#include "parser.h"
#include "prompt.h"

namespace {
bool g_enabled = false;
}

void set_syntax_highlight(bool enabled) { g_enabled = enabled; }

bool line_has_syntax_error(const std::string& line) {
    const LexResult lexed = Lexer().tokenize(line);
    if (!lexed.ok()) return true;
    return !Parser().parse(lexed.tokens, line).ok();
}

// Is the character at `index` inside quotes or escaped by a backslash? (readline uses this so that it does
// not treat a space or operator inside quotes as a word break.)
int is_quoted(char* text, int index) {
    bool single = false;
    bool dbl = false;
    for (int i = 0; i < index && text[i] != '\0'; ++i) {
        const char c = text[i];
        if (single) {
            if (c == '\'') single = false;
        } else if (c == '\\') {
            if (i + 1 == index) return 1;  // the character at `index` is escaped
            ++i;
        } else if (dbl) {
            if (c == '"') dbl = false;
        } else if (c == '\'') {
            single = true;
        } else if (c == '"') {
            dbl = true;
        }
    }
    return single || dbl ? 1 : 0;
}

extern "C" void redisplay_hook() {
    if (g_enabled) {
        char* text = rl_copy_text(0, rl_end);
        const bool bad = text != nullptr && line_has_syntax_error(text);
        std::free(text);
        rl_set_prompt(get_prompt(bad).c_str());
    }
    rl_redisplay();
}
