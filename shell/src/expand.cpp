#include "expand.h"

#include <glob.h>
#include <unistd.h>

#include <cctype>
#include <cstring>

namespace {

// A field under construction. `pattern` is the same text with every character that came from quoted text
// escaped, so glob(3) only treats the unquoted wildcard characters as wildcards.
struct Field {
    std::string text;
    std::string pattern;
    bool has_glob = false;  // an unquoted * ? [ was seen
    bool force = false;     // a quoted part was seen: keep the field even if it is empty ("" and '' are arguments)
};

bool isNameStart(char c) { return std::isalpha(static_cast<unsigned char>(c)) || c == '_'; }
bool isNameChar(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; }

class Expander {
public:
    Expander(const ExpandContext& ctx, bool split) : ctx_(ctx), split_(split) {}

    std::vector<std::string> run(const Word& word) {
        for (std::size_t i = 0; i < word.size(); ++i) {
            const WordPart& part = word[i];
            switch (part.quote) {
                case Quote::Single:
                    cur_.force = true;
                    literal(part.text);
                    break;
                case Quote::Double:
                    cur_.force = true;
                    expandDollars(part.text, /*quoted=*/true, 0);
                    break;
                case Quote::None:
                    unquoted(part.text, /*first=*/i == 0, /*only=*/word.size() == 1);
                    break;
            }
        }
        if (!split_) {
            fields_.push_back(cur_);  // exactly one field, possibly empty
        } else {
            endField();
        }
        std::vector<std::string> out;
        for (const Field& f : fields_) {
            if (split_ && f.has_glob) {
                globInto(f, out);
            } else {
                out.push_back(f.text);
            }
        }
        return out;
    }

private:
    // Text that must stay exactly as written: quoted text and expansion results inside double quotes.
    void literal(const std::string& s) {
        for (char c : s) {
            cur_.text += c;
            if (std::strchr("*?[]\\", c) != nullptr) cur_.pattern += '\\';
            cur_.pattern += c;
        }
    }

    // One unquoted character: it may be a wildcard.
    void activeChar(char c) {
        cur_.text += c;
        if (c == '*' || c == '?' || c == '[') cur_.has_glob = true;
        if (c == '\\') cur_.pattern += '\\';
        cur_.pattern += c;
    }

    void endField() {
        if (!cur_.text.empty() || cur_.force) fields_.push_back(cur_);
        cur_ = Field{};
    }

    // Result of an unquoted $expansion: split at whitespace into separate fields.
    void addUnquotedValue(const std::string& value) {
        if (!split_) {
            literal(value);
            return;
        }
        for (char c : value) {
            if (c == ' ' || c == '\t' || c == '\n') {
                endField();
            } else {
                activeChar(c);
            }
        }
    }

    // Looks at text[i] == '$'. On success stores the value and returns how many characters it used.
    bool parameter(const std::string& text, std::size_t i, std::string& value, std::size_t& used) {
        if (i + 1 >= text.size()) return false;
        const char c = text[i + 1];
        if (c == '{') {
            const std::size_t close = text.find('}', i + 2);
            if (close == std::string::npos) throw ExpandError("${" + text.substr(i + 2) + ": bad substitution");
            const std::string name = text.substr(i + 2, close - i - 2);
            if (name == "?" || name == "!" || name == "$") {
                value = special(name[0]);
            } else if (Variables::isValidName(name)) {
                value = ctx_.vars.get(name).value_or("");
            } else {
                throw ExpandError("${" + name + "}: bad substitution");
            }
            used = close - i + 1;
            return true;
        }
        if (isNameStart(c)) {
            std::size_t j = i + 1;
            while (j < text.size() && isNameChar(text[j])) ++j;
            value = ctx_.vars.get(text.substr(i + 1, j - i - 1)).value_or("");
            used = j - i;
            return true;
        }
        if (c == '?' || c == '!' || c == '$') {
            value = special(c);
            used = 2;
            return true;
        }
        if (std::isdigit(static_cast<unsigned char>(c))) {  // $1 ...: positional parameters are not supported
            value.clear();
            used = 2;
            return true;
        }
        return false;
    }

    std::string special(char which) const {
        if (which == '?') return std::to_string(ctx_.last_status);
        if (which == '!') return ctx_.last_bg_pid > 0 ? std::to_string(ctx_.last_bg_pid) : "";
        return std::to_string(static_cast<long>(getpid()));
    }

    void expandDollars(const std::string& text, bool quoted, std::size_t start) {
        for (std::size_t i = start; i < text.size(); ++i) {
            std::string value;
            std::size_t used = 0;
            if (text[i] == '$' && parameter(text, i, value, used)) {
                if (quoted) literal(value);
                else addUnquotedValue(value);
                i += used - 1;
            } else if (quoted) {
                literal(std::string(1, text[i]));
            } else {
                activeChar(text[i]);
            }
        }
    }

    void unquoted(const std::string& text, bool first, bool only) {
        std::size_t start = 0;
        // ~ alone or ~/path at the very start of a word. ~user is not supported and stays literal.
        if (first && !text.empty() && text[0] == '~' && (text.size() == 1 ? only : text[1] == '/')) {
            const auto home = ctx_.vars.get("HOME");
            if (home && !home->empty()) {
                literal(*home);
                start = 1;
            }
        }
        expandDollars(text, /*quoted=*/false, start);
    }

    void globInto(const Field& f, std::vector<std::string>& out) {
        glob_t g;
        std::memset(&g, 0, sizeof g);
        if (glob(f.pattern.c_str(), 0, nullptr, &g) == 0 && g.gl_pathc > 0) {
            for (std::size_t i = 0; i < g.gl_pathc; ++i) out.emplace_back(g.gl_pathv[i]);
        } else {
            out.push_back(f.text);  // no match: the word stays as written, like bash
        }
        globfree(&g);
    }

    const ExpandContext& ctx_;
    bool split_;
    Field cur_;
    std::vector<Field> fields_;
};

}  // namespace

std::vector<std::string> expandWord(const Word& word, const ExpandContext& ctx) {
    return Expander(ctx, /*split=*/true).run(word);
}

std::string expandWordSingle(const Word& word, const ExpandContext& ctx) {
    auto fields = Expander(ctx, /*split=*/false).run(word);
    return fields.empty() ? std::string() : fields[0];
}

bool splitAssignment(const Word& word, std::string& name, Word& value) {
    if (word.empty() || word[0].quote != Quote::None) return false;
    const std::string& text = word[0].text;
    const std::size_t eq = text.find('=');
    if (eq == std::string::npos || eq == 0) return false;
    const std::string candidate = text.substr(0, eq);
    if (!Variables::isValidName(candidate)) return false;
    name = candidate;
    value.clear();
    if (eq + 1 < text.size()) value.push_back(WordPart{text.substr(eq + 1), Quote::None});
    value.insert(value.end(), word.begin() + 1, word.end());
    return true;
}
