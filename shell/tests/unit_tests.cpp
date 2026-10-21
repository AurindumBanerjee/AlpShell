// Unit tests for the lexer, parser, expansion and variables. No framework: TEST() registers a function,
// CHECK() records a failure and carries on, and main() runs everything and returns non-zero on failure.
#include <glob.h>
#include <stdlib.h>
#include <unistd.h>

#include <cstdio>
#include <fstream>
#include <functional>
#include <iostream>
#include <string>
#include <vector>

#include "expand.h"
#include "lexer.h"
#include "parser.h"
#include "variables.h"

// ------------------------------------------------------------------ tiny test framework

struct TestCase {
    const char* name;
    std::function<void()> run;
};
static std::vector<TestCase>& registry() {
    static std::vector<TestCase> tests;
    return tests;
}
static int g_failures = 0;
static const char* g_current = "";

struct Registrar {
    Registrar(const char* name, std::function<void()> fn) { registry().push_back({name, std::move(fn)}); }
};
#define TEST(name)                                  \
    static void name();                             \
    static Registrar registrar_##name(#name, name); \
    static void name()

#define CHECK(cond)                                                                              \
    do {                                                                                         \
        if (!(cond)) {                                                                           \
            ++g_failures;                                                                        \
            std::printf("  FAIL %s: %s:%d: CHECK(%s)\n", g_current, __FILE__, __LINE__, #cond);  \
        }                                                                                        \
    } while (0)

#define CHECK_EQ(a, b)                                                                                      \
    do {                                                                                                    \
        const auto va = (a);                                                                                \
        const auto vb = (b);                                                                                \
        if (!(va == vb)) {                                                                                  \
            ++g_failures;                                                                                   \
            std::cout << "  FAIL " << g_current << ": " << __FILE__ << ":" << __LINE__ << ": " << #a       \
                      << " == " << #b << "\n      left:  " << show(va) << "\n      right: " << show(vb)    \
                      << std::endl;                                                                         \
        }                                                                                                   \
    } while (0)

static std::string show(const std::string& s) { return "\"" + s + "\""; }
[[maybe_unused]] static std::string show(const char* s) { return std::string("\"") + s + "\""; }
[[maybe_unused]] static std::string show(bool b) { return b ? "true" : "false"; }
[[maybe_unused]] static std::string show(int v) { return std::to_string(v); }
static std::string show(size_t v) { return std::to_string(v); }
static std::string show(const std::vector<std::string>& v) {
    std::string out = "[";
    for (size_t i = 0; i < v.size(); ++i) out += (i ? ", " : "") + show(v[i]);
    return out + "]";
}

// ------------------------------------------------------------------ helpers

static LexResult lex(const std::string& s) { return Lexer().tokenize(s); }

// "Word(a)" / "|" / "&&" ...: one entry per token, End excluded.
static std::vector<std::string> kinds(const std::string& s) {
    std::vector<std::string> out;
    for (const Token& t : lex(s).tokens) {
        if (t.type == TokenType::End) continue;
        out.push_back(t.type == TokenType::Word ? "W:" + t.spelling() : t.spelling());
    }
    return out;
}

static ParseResult parse(const std::string& s) {
    const LexResult l = lex(s);
    return Parser().parse(l.tokens, s);
}

static std::string wordText(const Word& w) {
    std::string s;
    for (const WordPart& p : w) {
        const char* open = p.quote == Quote::Single ? "'" : p.quote == Quote::Double ? "\"" : "";
        s += std::string(open) + p.text + open;
    }
    return s;
}

// The words of the first command of the first pipeline of the first list.
static std::vector<std::string> firstWords(const ParseResult& r) {
    std::vector<std::string> out;
    for (const Word& w : r.line.lists.at(0).pipelines.at(0).second.commands.at(0).words) out.push_back(wordText(w));
    return out;
}

// ------------------------------------------------------------------ lexer

TEST(lexer_splits_words_and_operators) {
    using V = std::vector<std::string>;
    CHECK_EQ(kinds("ls -l"), (V{"W:ls", "W:-l"}));
    CHECK_EQ(kinds("a | b"), (V{"W:a", "|", "W:b"}));
    CHECK_EQ(kinds("a|b"), (V{"W:a", "|", "W:b"}));
    CHECK_EQ(kinds("a && b || c"), (V{"W:a", "&&", "W:b", "||", "W:c"}));
    CHECK_EQ(kinds("a;b"), (V{"W:a", ";", "W:b"}));
    CHECK_EQ(kinds("a &"), (V{"W:a", "&"}));
    CHECK_EQ(kinds("sleep 5&"), (V{"W:sleep", "W:5", "&"}));
    CHECK_EQ(kinds("a < in > out >> log"), (V{"W:a", "<", "W:in", ">", "W:out", ">>", "W:log"}));
    CHECK_EQ(kinds("   "), V{});
}

TEST(lexer_recognises_descriptor_redirections) {
    using V = std::vector<std::string>;
    CHECK_EQ(kinds("a 2> e"), (V{"W:a", "2>", "W:e"}));
    CHECK_EQ(kinds("a 2>> e"), (V{"W:a", "2>>", "W:e"}));
    CHECK_EQ(kinds("a 2>&1"), (V{"W:a", "2>&1"}));
    CHECK_EQ(kinds("a >&2"), (V{"W:a", ">&2"}));
    CHECK_EQ(kinds("a 1>&2"), (V{"W:a", ">&2"}));
    CHECK_EQ(kinds("a 1> f"), (V{"W:a", ">", "W:f"}));
    CHECK_EQ(kinds("a 1>> f"), (V{"W:a", ">>", "W:f"}));
    // a digit is only a descriptor when it stands alone in front of '>'
    CHECK_EQ(kinds("echo 2"), (V{"W:echo", "W:2"}));
    CHECK_EQ(kinds("echo a2>f"), (V{"W:echo", "W:a2", ">", "W:f"}));
    CHECK_EQ(kinds("echo 22>f"), (V{"W:echo", "W:22", ">", "W:f"}));
}

TEST(lexer_keeps_quoting_per_part) {
    const LexResult r = lex("a\"b c\"'d e'f");
    CHECK(r.ok());
    CHECK_EQ(r.tokens.size(), static_cast<size_t>(2));  // one word + End
    const Word& w = r.tokens[0].parts;
    CHECK_EQ(w.size(), static_cast<size_t>(4));
    CHECK_EQ(wordText(w), std::string("a\"b c\"'d e'f"));
    CHECK(w[0].quote == Quote::None && w[1].quote == Quote::Double && w[2].quote == Quote::Single && w[3].quote == Quote::None);
}

TEST(lexer_empty_quotes_are_words) {
    const LexResult r = lex("echo '' \"\"");
    CHECK_EQ(r.tokens.size(), static_cast<size_t>(4));  // echo, '', "", End
    CHECK(r.tokens[1].type == TokenType::Word && r.tokens[1].parts.size() == 1 && r.tokens[1].parts[0].text.empty());
    CHECK(r.tokens[2].type == TokenType::Word && r.tokens[2].parts.size() == 1 && r.tokens[2].parts[0].quote == Quote::Double);
}

TEST(lexer_operators_inside_quotes_are_text) {
    using V = std::vector<std::string>;
    CHECK_EQ(kinds("echo \">\" '|' \"a && b\""), (V{"W:echo", "W:>", "W:|", "W:a && b"}));
    CHECK_EQ(kinds("echo a\">\"b"), (V{"W:echo", "W:a>b"}));
}

TEST(lexer_backslash_escapes) {
    using V = std::vector<std::string>;
    CHECK_EQ(kinds("echo a\\ b"), (V{"W:echo", "W:a b"}));
    CHECK_EQ(kinds("echo \\| \\> \\&"), (V{"W:echo", "W:|", "W:>", "W:&"}));
    // inside double quotes only $ " \ ` and newline can be escaped
    CHECK_EQ(kinds("echo \"a\\\"b\" \"c\\nd\""), (V{"W:echo", "W:a\"b", "W:c\\nd"}));
    // an escaped character is literal: it must not be expanded or globbed later
    const LexResult r = lex("\\$HOME");
    CHECK(r.tokens[0].parts[0].quote == Quote::Single);
}

TEST(lexer_comments) {
    using V = std::vector<std::string>;
    CHECK_EQ(kinds("echo a # comment | with > stuff"), (V{"W:echo", "W:a"}));
    CHECK_EQ(kinds("# only a comment"), V{});
    CHECK_EQ(kinds("echo a#b"), (V{"W:echo", "W:a#b"}));
    CHECK_EQ(kinds("echo \"#x\" '#y'"), (V{"W:echo", "W:#x", "W:#y"}));
}

TEST(lexer_line_continuation) {
    using V = std::vector<std::string>;
    CHECK_EQ(kinds("echo a\\\nb"), (V{"W:echo", "W:ab"}));
    CHECK_EQ(kinds("echo a \\\n b"), (V{"W:echo", "W:a", "W:b"}));
}

TEST(lexer_incomplete_input_is_reported_not_crashed_on) {
    LexResult r = lex("echo \"abc");
    CHECK(!r.ok() && r.incomplete);
    r = lex("echo 'abc");
    CHECK(!r.ok() && r.incomplete);
    r = lex("echo abc\\");
    CHECK(!r.ok() && r.incomplete);
    CHECK(lex("echo \"abc\"").ok());
}

TEST(lexer_token_positions) {
    const LexResult r = lex("ab  | c");
    CHECK_EQ(r.tokens[0].start, static_cast<size_t>(0));
    CHECK_EQ(r.tokens[0].end, static_cast<size_t>(2));
    CHECK_EQ(r.tokens[1].start, static_cast<size_t>(4));
    CHECK_EQ(r.tokens[1].end, static_cast<size_t>(5));
}

// ------------------------------------------------------------------ parser

TEST(parser_simple_command_and_pipeline) {
    ParseResult r = parse("ls -l /tmp");
    CHECK(r.ok());
    CHECK_EQ(r.line.lists.size(), static_cast<size_t>(1));
    CHECK_EQ(firstWords(r), (std::vector<std::string>{"ls", "-l", "/tmp"}));

    r = parse("a | b | c");
    CHECK(r.ok());
    CHECK_EQ(r.line.lists.at(0).pipelines.at(0).second.commands.size(), static_cast<size_t>(3));
}

TEST(parser_and_or_lists_keep_their_connectors) {
    const ParseResult r = parse("a && b || c");
    CHECK(r.ok());
    const auto& p = r.line.lists.at(0).pipelines;
    CHECK_EQ(p.size(), static_cast<size_t>(3));
    CHECK(p[0].first == Connector::First && p[1].first == Connector::And && p[2].first == Connector::Or);
}

TEST(parser_separators_and_background) {
    ParseResult r = parse("a; b ; c");
    CHECK(r.ok());
    CHECK_EQ(r.line.lists.size(), static_cast<size_t>(3));
    CHECK(!r.line.lists[0].background);

    r = parse("a & b");
    CHECK(r.ok());
    CHECK_EQ(r.line.lists.size(), static_cast<size_t>(2));
    CHECK(r.line.lists[0].background && !r.line.lists[1].background);

    r = parse("a && b &");
    CHECK(r.ok());
    CHECK_EQ(r.line.lists.size(), static_cast<size_t>(1));
    CHECK(r.line.lists[0].background);
    CHECK_EQ(r.line.lists[0].pipelines.size(), static_cast<size_t>(2));

    r = parse("a;");  // a trailing ; is fine
    CHECK(r.ok());
    CHECK_EQ(r.line.lists.size(), static_cast<size_t>(1));
}

TEST(parser_list_text_is_the_source_span) {
    const ParseResult r = parse("  sleep 5 && echo done &  ");
    CHECK(r.ok());
    CHECK_EQ(r.line.lists.at(0).text, std::string("sleep 5 && echo done"));
    const ParseResult s = parse("a;  b c");
    CHECK_EQ(s.line.lists.at(0).text, std::string("a"));
    CHECK_EQ(s.line.lists.at(1).text, std::string("b c"));
}

TEST(parser_redirections_keep_their_order) {
    ParseResult r = parse("cmd > out 2>&1");
    CHECK(r.ok());
    const SimpleCommand& c = r.line.lists.at(0).pipelines.at(0).second.commands.at(0);
    CHECK_EQ(c.words.size(), static_cast<size_t>(1));
    CHECK_EQ(c.redirs.size(), static_cast<size_t>(2));
    CHECK(c.redirs[0].kind == RedirKind::Out && c.redirs[1].kind == RedirKind::ErrToOut);

    r = parse("cmd 2>&1 > out");
    const SimpleCommand& d = r.line.lists.at(0).pipelines.at(0).second.commands.at(0);
    CHECK(d.redirs[0].kind == RedirKind::ErrToOut && d.redirs[1].kind == RedirKind::Out);

    r = parse("< in cat >> log 2> err");  // redirections may come before the command name
    const SimpleCommand& e = r.line.lists.at(0).pipelines.at(0).second.commands.at(0);
    CHECK_EQ(wordText(e.words.at(0)), std::string("cat"));
    CHECK(e.redirs.size() == 3 && e.redirs[0].kind == RedirKind::In && e.redirs[1].kind == RedirKind::Append &&
          e.redirs[2].kind == RedirKind::ErrOut);
}

TEST(parser_a_command_may_be_only_redirections) {
    const ParseResult r = parse("> created");
    CHECK(r.ok());
    CHECK(r.line.lists.at(0).pipelines.at(0).second.commands.at(0).words.empty());
}

TEST(parser_empty_input_is_an_empty_line) {
    CHECK(parse("").ok() && parse("").line.lists.empty());
    CHECK(parse("   # nothing").ok() && parse("   # nothing").line.lists.empty());
}

TEST(parser_syntax_errors_have_messages) {
    ParseResult r = parse("| a");
    CHECK(!r.ok() && !r.incomplete);
    CHECK_EQ(r.error, std::string("syntax error near unexpected token `|'"));
    r = parse("a ; ; b");
    CHECK(!r.ok() && !r.incomplete);
    r = parse("&& a");
    CHECK_EQ(r.error, std::string("syntax error near unexpected token `&&'"));
    r = parse("a >");
    CHECK_EQ(r.error, std::string("syntax error near unexpected token `newline'"));
    r = parse("a > | b");
    CHECK_EQ(r.error, std::string("syntax error near unexpected token `|'"));
    r = parse("a | | b");
    CHECK(!r.ok() && !r.incomplete);
    r = parse("; a");
    CHECK(!r.ok());
}

TEST(parser_trailing_operators_ask_for_more_input) {
    CHECK(parse("a |").incomplete);
    CHECK(parse("a &&").incomplete);
    CHECK(parse("a ||").incomplete);
    CHECK(!parse("a &").incomplete);
}

// ------------------------------------------------------------------ variables

TEST(variables_set_get_unset) {
    Variables v;
    CHECK(!v.get("ALP_T1").has_value());
    v.set("ALP_T1", "x");
    CHECK_EQ(v.get("ALP_T1").value_or("?"), std::string("x"));
    CHECK(getenv("ALP_T1") == nullptr);  // a plain shell variable is not exported
    v.exportExisting("ALP_T1");
    CHECK(getenv("ALP_T1") != nullptr && std::string(getenv("ALP_T1")) == "x");
    v.set("ALP_T1", "y");  // an exported variable stays exported
    CHECK(std::string(getenv("ALP_T1")) == "y");
    v.unset("ALP_T1");
    CHECK(!v.get("ALP_T1").has_value() && getenv("ALP_T1") == nullptr);
}

TEST(variables_export_value_and_names) {
    Variables v;
    v.exportValue("ALP_T2", "1");
    CHECK(getenv("ALP_T2") != nullptr);
    bool found = false;
    for (const auto& kv : v.exported()) found = found || (kv.first == "ALP_T2" && kv.second == "1");
    CHECK(found);
    v.unset("ALP_T2");
    CHECK(Variables::isValidName("abc_1") && Variables::isValidName("_x"));
    CHECK(!Variables::isValidName("") && !Variables::isValidName("1a") && !Variables::isValidName("a-b"));
}

// ------------------------------------------------------------------ expansion

static std::vector<std::string> expand(const std::string& line, Variables& vars, int status = 0) {
    const ParseResult r = parse(line);
    std::vector<std::string> out;
    const ExpandContext ctx{vars, status, 0};
    for (const Word& w : r.line.lists.at(0).pipelines.at(0).second.commands.at(0).words) {
        const auto fields = expandWord(w, ctx);
        out.insert(out.end(), fields.begin(), fields.end());
    }
    return out;
}

TEST(expand_variables_by_quoting_rule) {
    Variables v;
    v.set("ALP_A", "1");
    using V = std::vector<std::string>;
    CHECK_EQ(expand("x $ALP_A ${ALP_A} \"$ALP_A\" '$ALP_A' \\$ALP_A", v), (V{"x", "1", "1", "1", "$ALP_A", "$ALP_A"}));
    CHECK_EQ(expand("x$ALP_A$ALP_A", v), (V{"x11"}));
    CHECK_EQ(expand("$ALP_A-$ALP_A", v), (V{"1-1"}));
    CHECK_EQ(expand("x ${ALP_A}z", v), (V{"x", "1z"}));
}

TEST(expand_unset_and_empty_variables) {
    Variables v;
    v.set("ALP_EMPTY", "");
    using V = std::vector<std::string>;
    CHECK_EQ(expand("a $ALP_NOPE b", v), (V{"a", "b"}));              // unquoted: the argument disappears
    CHECK_EQ(expand("a \"$ALP_NOPE\" b", v), (V{"a", "", "b"}));      // quoted: an empty argument stays
    CHECK_EQ(expand("a $ALP_EMPTY\"\" b", v), (V{"a", "", "b"}));
    CHECK_EQ(expand("'' \"\"", v), (V{"", ""}));
}

TEST(expand_splits_unquoted_values_only) {
    Variables v;
    v.set("ALP_L", "a b   c");
    v.set("ALP_T", " lead  trail ");
    using V = std::vector<std::string>;
    CHECK_EQ(expand("$ALP_L", v), (V{"a", "b", "c"}));
    CHECK_EQ(expand("\"$ALP_L\"", v), (V{"a b   c"}));
    CHECK_EQ(expand("x$ALP_L", v), (V{"xa", "b", "c"}));
    CHECK_EQ(expand("$ALP_L\"y\"", v), (V{"a", "b", "cy"}));
    CHECK_EQ(expand("$ALP_T", v), (V{"lead", "trail"}));
}

TEST(expand_special_parameters) {
    Variables v;
    using V = std::vector<std::string>;
    CHECK_EQ(expand("$? ${?} \"$?\"", v, 42), (V{"42", "42", "42"}));
    CHECK_EQ(expand("$", v), (V{"$"}));
    CHECK_EQ(expand("a$ b$%", v), (V{"a$", "b$%"}));
    CHECK_EQ(expand("$1 x", v), (V{"x"}));  // positional parameters are not supported: they expand to nothing
}

TEST(expand_bad_substitution_throws) {
    Variables v;
    bool threw = false;
    try {
        expand("${ALP_A:-x}", v);
    } catch (const ExpandError&) {
        threw = true;
    }
    CHECK(threw);
    threw = false;
    try {
        expand("${ALP_A", v);
    } catch (const ExpandError&) {
        threw = true;
    }
    CHECK(threw);
}

TEST(expand_tilde_only_at_the_start_of_a_word) {
    Variables v;
    v.set("HOME", "/home/alp");
    using V = std::vector<std::string>;
    CHECK_EQ(expand("~ ~/x", v), (V{"/home/alp", "/home/alp/x"}));
    CHECK_EQ(expand("\"~\" '~' a~ ~x \\~", v), (V{"~", "~", "a~", "~x", "~"}));
    CHECK_EQ(expand("a=~", v), (V{"a=~"}));
}

struct TempDir {
    std::string path;
    TempDir() {
        char tmpl[] = "/tmp/alpshell_unit_XXXXXX";
        path = mkdtemp(tmpl);
    }
    ~TempDir() {
        std::string cmd = "rm -rf '" + path + "'";
        if (system(cmd.c_str()) != 0) std::fprintf(stderr, "could not remove %s\n", path.c_str());
    }
    void touch(const std::string& name) const { std::ofstream(path + "/" + name) << "x"; }
};

TEST(expand_globbing) {
    TempDir dir;
    for (const char* f : {"a.txt", "b.txt", "c.log", ".hidden", "a b", "star*"}) dir.touch(f);
    Variables v;
    using V = std::vector<std::string>;
    const std::string d = dir.path + "/";
    CHECK_EQ(expand(d + "*.txt", v), (V{d + "a.txt", d + "b.txt"}));
    CHECK_EQ(expand(d + "?.log", v), (V{d + "c.log"}));
    CHECK_EQ(expand(d + "[ab].txt", v), (V{d + "a.txt", d + "b.txt"}));
    CHECK_EQ(expand(d + "*.none", v), (V{d + "*.none"}));  // no match: the word stays as written
    CHECK_EQ(expand("'" + d + "*.txt'", v), (V{d + "*.txt"}));  // quoted: no globbing
    CHECK_EQ(expand(d + "\\*.txt", v), (V{d + "*.txt"}));
    CHECK_EQ(expand(d + ".h*", v), (V{d + ".hidden"}));
    // a quoted wildcard inside a pattern is literal, an unquoted one is not
    CHECK_EQ(expand(d + "star'*'", v), (V{d + "star*"}));
    CHECK_EQ(expand(d + "a*", v), (V{d + "a b", d + "a.txt"}));
    // the result of a variable is globbed too, unless quoted
    v.set("ALP_G", d + "*.txt");
    CHECK_EQ(expand("$ALP_G", v), (V{d + "a.txt", d + "b.txt"}));
    CHECK_EQ(expand("\"$ALP_G\"", v), (V{d + "*.txt"}));
}

TEST(expand_single_never_splits_or_globs) {
    Variables v;
    v.set("ALP_L", "a  b");
    const ParseResult r = parse("x");
    const ExpandContext ctx{v, 0, 0};
    const Word word = lex("$ALP_L*").tokens[0].parts;
    CHECK_EQ(expandWordSingle(word, ctx), std::string("a  b*"));
    const Word none = lex("$ALP_NOPE").tokens[0].parts;
    CHECK_EQ(expandWordSingle(none, ctx), std::string(""));
    (void)r;
}

TEST(expand_assignment_words) {
    std::string name;
    Word value;
    CHECK(splitAssignment(lex("A=1").tokens[0].parts, name, value) && name == "A" && wordText(value) == "1");
    CHECK(splitAssignment(lex("A=").tokens[0].parts, name, value) && value.empty());
    CHECK(splitAssignment(lex("A=\"x y\"z").tokens[0].parts, name, value) && wordText(value) == "\"x y\"z");
    CHECK(!splitAssignment(lex("=1").tokens[0].parts, name, value));
    CHECK(!splitAssignment(lex("1A=1").tokens[0].parts, name, value));
    CHECK(!splitAssignment(lex("a-b=1").tokens[0].parts, name, value));
    CHECK(!splitAssignment(lex("\"A\"=1").tokens[0].parts, name, value));  // the name must be unquoted
    CHECK(!splitAssignment(lex("plain").tokens[0].parts, name, value));
}

// ------------------------------------------------------------------ main

int main() {
    int run = 0;
    for (const TestCase& t : registry()) {
        g_current = t.name;
        const int before = g_failures;
        t.run();
        ++run;
        std::printf("%s %s\n", g_failures == before ? "ok  " : "FAIL", t.name);
    }
    std::printf("\n%d tests, %d failed checks\n", run, g_failures);
    return g_failures == 0 ? 0 : 1;
}
