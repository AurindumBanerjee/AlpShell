#ifndef LEXER_H
#define LEXER_H

#include <cstddef>
#include <string>
#include <vector>

// How a piece of a word was quoted. Expansion depends on it: nothing is expanded inside '...',
// only variables are expanded inside "...", and unquoted text also gets globbing and field splitting.
// Text produced by a backslash escape is stored as a Single part (literal).
enum class Quote { None, Single, Double };

struct WordPart {
    std::string text;
    Quote quote = Quote::None;
};

// A shell word is a sequence of parts, e.g. a"b c"'d' has three parts.
using Word = std::vector<WordPart>;

enum class TokenType {
    Word,
    Pipe,           // |
    And,            // &&
    Or,             // ||
    Semi,           // ;
    Amp,            // &
    RedirIn,        // <
    RedirOut,       // > or 1>
    RedirAppend,    // >> or 1>>
    RedirErrOut,    // 2>
    RedirErrAppend, // 2>>
    RedirErrToOut,  // 2>&1
    RedirOutToErr,  // >&2 or 1>&2
    End
};

struct Token {
    TokenType type = TokenType::End;
    Word parts;                // Word tokens only
    std::size_t start = 0;     // byte range [start, end) in the input
    std::size_t end = 0;

    // Operator spelling, or the word with quotes removed. Used in error messages.
    std::string spelling() const;
};

struct LexResult {
    std::vector<Token> tokens;  // always ends with an End token when error is empty
    std::string error;          // empty when the input was tokenized
    bool incomplete = false;    // ended inside a quote or after a backslash: more input could complete it
    bool ok() const { return error.empty(); }
};

class Lexer {
public:
    // Splits one logical line. Comments (# at the start of a word) run to the end of the line.
    // A backslash followed by a newline joins the lines.
    LexResult tokenize(const std::string& input) const;
};

#endif // LEXER_H
