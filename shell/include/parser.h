#ifndef PARSER_H
#define PARSER_H

#include <string>
#include <utility>
#include <vector>

#include "lexer.h"

// Grammar (all on one logical line):
//
//   line     := [ list ( (';' | '&') list )* [';' | '&'] ]
//   list     := pipeline ( ('&&' | '||') pipeline )*          -- "and-or list", evaluated left to right
//   pipeline := command ( '|' command )*
//   command  := ( word | redirection )+
//
// A trailing '&' runs the whole and-or list in the background.

enum class RedirKind {
    In,         // < file
    Out,        // > file
    Append,     // >> file
    ErrOut,     // 2> file
    ErrAppend,  // 2>> file
    ErrToOut,   // 2>&1
    OutToErr    // >&2 or 1>&2
};

struct Redirection {
    RedirKind kind = RedirKind::Out;
    Word target;  // empty for ErrToOut / OutToErr
};

struct SimpleCommand {
    std::vector<Word> words;
    std::vector<Redirection> redirs;  // kept in source order: "> f 2>&1" differs from "2>&1 > f"
};

struct Pipeline {
    std::vector<SimpleCommand> commands;
};

enum class Connector { First, And, Or };

struct AndOrList {
    std::vector<std::pair<Connector, Pipeline>> pipelines;  // the first has Connector::First
    bool background = false;
    std::string text;  // source text of the list, used for job descriptions
};

struct CommandLine {
    std::vector<AndOrList> lists;
};

struct ParseResult {
    CommandLine line;
    std::string error;        // empty on success
    bool incomplete = false;  // input ended where more is required (e.g. after '|' or '&&')
    bool ok() const { return error.empty(); }
};

class Parser {
public:
    // `source` is the text the tokens came from; it is only used to fill AndOrList::text.
    ParseResult parse(const std::vector<Token>& tokens, const std::string& source = "") const;
};

#endif // PARSER_H
