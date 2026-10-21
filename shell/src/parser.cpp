#include "parser.h"

namespace {

class Impl {
public:
    Impl(const std::vector<Token>& tokens, const std::string& source) : toks_(tokens), source_(source) {}

    ParseResult run() {
        ParseResult r;
        while (cur().type != TokenType::End) {
            if (cur().type == TokenType::Semi || cur().type == TokenType::Amp) {
                return unexpected(r);
            }
            AndOrList list;
            if (!parse_and_or(list, r)) return r;
            // `list` ends at ';', '&' or the end of the line.
            if (cur().type == TokenType::Amp) {
                list.background = true;
                ++pos_;
            } else if (cur().type == TokenType::Semi) {
                ++pos_;
            }
            r.line.lists.push_back(std::move(list));
        }
        return r;
    }

private:
    const Token& cur() const { return toks_[pos_]; }

    static bool is_redirect(TokenType t) {
        switch (t) {
            case TokenType::RedirIn:
            case TokenType::RedirOut:
            case TokenType::RedirAppend:
            case TokenType::RedirErrOut:
            case TokenType::RedirErrAppend:
            case TokenType::RedirErrToOut:
            case TokenType::RedirOutToErr:
                return true;
            default:
                return false;
        }
    }

    static RedirKind kind_of(TokenType t) {
        switch (t) {
            case TokenType::RedirIn: return RedirKind::In;
            case TokenType::RedirOut: return RedirKind::Out;
            case TokenType::RedirAppend: return RedirKind::Append;
            case TokenType::RedirErrOut: return RedirKind::ErrOut;
            case TokenType::RedirErrAppend: return RedirKind::ErrAppend;
            case TokenType::RedirErrToOut: return RedirKind::ErrToOut;
            default: return RedirKind::OutToErr;
        }
    }

    ParseResult& unexpected(ParseResult& r) {
        r.error = "syntax error near unexpected token `" + cur().spelling() + "'";
        return r;
    }

    // Input ended where a command had to follow an operator: the caller may supply more lines.
    ParseResult& need_more(ParseResult& r) {
        r.error = "syntax error: unexpected end of file";
        r.incomplete = true;
        return r;
    }

    bool parse_and_or(AndOrList& list, ParseResult& r) {
        const std::size_t first_tok = pos_;
        Pipeline p;
        if (!parse_pipeline(p, r)) return false;
        list.pipelines.emplace_back(Connector::First, std::move(p));
        while (cur().type == TokenType::And || cur().type == TokenType::Or) {
            const Connector c = cur().type == TokenType::And ? Connector::And : Connector::Or;
            ++pos_;
            if (cur().type == TokenType::End) {
                need_more(r);
                return false;
            }
            Pipeline next;
            if (!parse_pipeline(next, r)) return false;
            list.pipelines.emplace_back(c, std::move(next));
        }
        const std::size_t last_tok = pos_ - 1;
        if (!source_.empty()) {
            list.text = source_.substr(toks_[first_tok].start, toks_[last_tok].end - toks_[first_tok].start);
        }
        return true;
    }

    bool parse_pipeline(Pipeline& p, ParseResult& r) {
        SimpleCommand c;
        if (!parse_command(c, r)) return false;
        p.commands.push_back(std::move(c));
        while (cur().type == TokenType::Pipe) {
            ++pos_;
            if (cur().type == TokenType::End) {
                need_more(r);
                return false;
            }
            SimpleCommand next;
            if (!parse_command(next, r)) return false;
            p.commands.push_back(std::move(next));
        }
        return true;
    }

    bool parse_command(SimpleCommand& c, ParseResult& r) {
        while (true) {
            const TokenType t = cur().type;
            if (t == TokenType::Word) {
                c.words.push_back(cur().parts);
                ++pos_;
            } else if (is_redirect(t)) {
                Redirection redir;
                redir.kind = kind_of(t);
                ++pos_;
                if (redir.kind != RedirKind::ErrToOut && redir.kind != RedirKind::OutToErr) {
                    if (cur().type != TokenType::Word) {
                        unexpected(r);
                        return false;
                    }
                    redir.target = cur().parts;
                    ++pos_;
                }
                c.redirs.push_back(std::move(redir));
            } else {
                break;
            }
        }
        if (c.words.empty() && c.redirs.empty()) {
            unexpected(r);  // e.g. "| x", "&& x" or ";;"
            return false;
        }
        return true;
    }

    const std::vector<Token>& toks_;
    const std::string& source_;
    std::size_t pos_ = 0;
};

}  // namespace

ParseResult Parser::parse(const std::vector<Token>& tokens, const std::string& source) const {
    if (tokens.empty()) return ParseResult{};
    return Impl(tokens, source).run();
}
