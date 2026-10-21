#include "lexer.h"

#include <cstring>

std::string Token::spelling() const {
    switch (type) {
        case TokenType::Pipe: return "|";
        case TokenType::And: return "&&";
        case TokenType::Or: return "||";
        case TokenType::Semi: return ";";
        case TokenType::Amp: return "&";
        case TokenType::RedirIn: return "<";
        case TokenType::RedirOut: return ">";
        case TokenType::RedirAppend: return ">>";
        case TokenType::RedirErrOut: return "2>";
        case TokenType::RedirErrAppend: return "2>>";
        case TokenType::RedirErrToOut: return "2>&1";
        case TokenType::RedirOutToErr: return ">&2";
        case TokenType::End: return "newline";
        case TokenType::Word: break;
    }
    std::string s;
    for (const WordPart& p : parts) s += p.text;
    return s;
}

LexResult Lexer::tokenize(const std::string& in) const {
    LexResult result;
    const std::size_t n = in.size();
    std::size_t i = 0;

    Word parts;                 // the word being built
    bool in_word = false;
    std::size_t word_start = 0;

    auto begin_word = [&](std::size_t at) {
        if (!in_word) {
            in_word = true;
            word_start = at;
        }
    };
    // Appends literal text to the current word, merging with the previous part when the quoting matches.
    auto add_text = [&](const std::string& text, Quote q, bool force_new) {
        if (!force_new && !parts.empty() && parts.back().quote == q) {
            parts.back().text += text;
        } else {
            parts.push_back(WordPart{text, q});
        }
    };
    auto end_word = [&](std::size_t at) {
        if (!in_word) return;
        Token t;
        t.type = TokenType::Word;
        t.parts = std::move(parts);
        t.start = word_start;
        t.end = at;
        result.tokens.push_back(std::move(t));
        parts.clear();
        in_word = false;
    };
    auto add_op = [&](TokenType type, std::size_t len) {
        end_word(i);
        Token t;
        t.type = type;
        t.start = i;
        t.end = i + len;
        result.tokens.push_back(std::move(t));
        i += len;
    };
    auto fail_incomplete = [&](const std::string& message) {
        result.error = message;
        result.incomplete = true;
        result.tokens.clear();
    };

    while (i < n) {
        const char c = in[i];

        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            end_word(i);
            ++i;
            continue;
        }
        if (c == '#' && !in_word) {
            break;  // comment: ignore the rest of the line
        }
        if (c == '|') {
            add_op(i + 1 < n && in[i + 1] == '|' ? TokenType::Or : TokenType::Pipe, i + 1 < n && in[i + 1] == '|' ? 2 : 1);
            continue;
        }
        if (c == '&') {
            add_op(i + 1 < n && in[i + 1] == '&' ? TokenType::And : TokenType::Amp, i + 1 < n && in[i + 1] == '&' ? 2 : 1);
            continue;
        }
        if (c == ';') {
            add_op(TokenType::Semi, 1);
            continue;
        }
        if (c == '<') {
            add_op(TokenType::RedirIn, 1);
            continue;
        }
        if (c == '>') {
            if (i + 2 < n && in[i + 1] == '&' && in[i + 2] == '2') {
                add_op(TokenType::RedirOutToErr, 3);
            } else if (i + 1 < n && in[i + 1] == '>') {
                add_op(TokenType::RedirAppend, 2);
            } else {
                add_op(TokenType::RedirOut, 1);
            }
            continue;
        }
        // "1>", "1>>", "1>&2", "2>", "2>>", "2>&1": a lone digit directly in front of '>' names the descriptor.
        if ((c == '1' || c == '2') && !in_word && i + 1 < n && in[i + 1] == '>') {
            const bool append = i + 2 < n && in[i + 2] == '>';
            const bool dup_to_err = i + 3 < n && in[i + 2] == '&' && in[i + 3] == '2';
            const bool dup_to_out = i + 3 < n && in[i + 2] == '&' && in[i + 3] == '1';
            if (c == '2') {
                if (dup_to_out) {
                    add_op(TokenType::RedirErrToOut, 4);
                } else if (append) {
                    add_op(TokenType::RedirErrAppend, 3);
                } else {
                    add_op(TokenType::RedirErrOut, 2);
                }
            } else {
                if (dup_to_err) {
                    add_op(TokenType::RedirOutToErr, 4);
                } else if (append) {
                    add_op(TokenType::RedirAppend, 3);
                } else {
                    add_op(TokenType::RedirOut, 2);
                }
            }
            continue;
        }

        // ---- characters that belong to a word ----
        if (c == '\\') {
            if (i + 1 >= n) {
                fail_incomplete("unexpected end of input after backslash");
                return result;
            }
            if (in[i + 1] == '\n') {
                i += 2;  // line continuation: removed entirely (it does not start a word)
                continue;
            }
            begin_word(i);
            add_text(std::string(1, in[i + 1]), Quote::Single, false);
            i += 2;
            continue;
        }
        if (c == '\'') {
            begin_word(i);
            const std::size_t close = in.find('\'', i + 1);
            if (close == std::string::npos) {
                fail_incomplete("unexpected EOF while looking for matching `''");
                return result;
            }
            add_text(in.substr(i + 1, close - i - 1), Quote::Single, true);  // keep empty '' as a (quoted) part
            i = close + 1;
            continue;
        }
        if (c == '"') {
            begin_word(i);
            parts.push_back(WordPart{"", Quote::Double});  // a "" with no text still makes a (quoted) part
            std::size_t j = i + 1;
            bool closed = false;
            while (j < n) {
                if (in[j] == '"') {
                    closed = true;
                    break;
                }
                if (in[j] == '\\' && j + 1 < n && std::strchr("$\"\\`\n", in[j + 1]) != nullptr) {
                    if (in[j + 1] != '\n') {  // backslash-newline vanishes; the other escapes become literal text
                        parts.push_back(WordPart{std::string(1, in[j + 1]), Quote::Single});
                        parts.push_back(WordPart{"", Quote::Double});
                    }
                    j += 2;
                    continue;
                }
                parts.back().text += in[j];
                ++j;
            }
            if (!closed) {
                fail_incomplete("unexpected EOF while looking for matching `\"'");
                return result;
            }
            i = j + 1;
            continue;
        }

        begin_word(i);
        add_text(std::string(1, c), Quote::None, false);
        ++i;
    }
    end_word(i < n ? i : n);

    Token end;
    end.type = TokenType::End;
    end.start = end.end = n;
    result.tokens.push_back(end);
    return result;
}
