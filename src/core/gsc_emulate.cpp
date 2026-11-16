#include "gsc_emulate.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <unordered_map>
#include <utility>

#include "prefab.h"         // Mat3, anglesToMatrix
#include "stock_smodels.h"  // axisToAngles

namespace iw8r {

// Everything of the interpreter lives in gscemu: its short names (Op, Value, text ...) must not meet the iw8r ones.
namespace gscemu {

constexpr double kPi = 3.14159265358979323846;
constexpr double kFrame = 0.05;                // one server frame (20 Hz): the shortest wait
constexpr double kGravity = 800;               // g_gravity, for movegravity
constexpr long kStepsPerResume = 200000;       // a thread that runs this long without waiting is stopped
constexpr long kStepsPerAdvance = 20000000;    // and the whole script, past this much work in one advanceTo
constexpr size_t kMaxFrames = 200;             // call depth
constexpr size_t kMaxThreads = 4096;
constexpr int kMaxNestedStarts = 32;           // `thread f()` / a notify runs the thread at once, up to this depth
constexpr long kStartsPerAdvance = 50000;      // threads started in one advanceTo before the script is stopped
constexpr size_t kMaxArray = 1u << 20;

std::string lower(std::string s) {
    for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

bool isDigit(char c) { return c >= '0' && c <= '9'; }
bool isIdentStart(char c) { return std::isalpha(static_cast<unsigned char>(c)) || c == '_'; }
bool isIdentChar(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; }

// ---------------------------------------------------------------------------------------------------------- lexer

enum class Tk { End, Ident, Num, Str, Punct };
struct Token {
    Tk k = Tk::End;
    std::string s;  // identifiers lower-cased (GSC names are case-insensitive), string text, punctuation
    double n = 0;
    int line = 1;
};

using NoteFn = std::function<void(int, const std::string&)>;

std::vector<Token> lex(const std::string& src, const NoteFn& note) {
    std::vector<Token> out;
    int line = 1;
    size_t i = 0;
    const size_t n = src.size();
    const auto at = [&](size_t k) { return k < n ? src[k] : '\0'; };
    if (n >= 3 && (unsigned char)src[0] == 0xEF && (unsigned char)src[1] == 0xBB && (unsigned char)src[2] == 0xBF) i = 3;
    while (i < n) {
        const char c = src[i];
        if (c == '\n') {
            ++line, ++i;
            continue;
        }
        if (c == ' ' || c == '\t' || c == '\r' || c == '\f' || c == '\v') {
            ++i;
            continue;
        }
        if (c == '/' && at(i + 1) == '/') {
            while (i < n && src[i] != '\n') ++i;
            continue;
        }
        if (c == '/' && (at(i + 1) == '*' || at(i + 1) == '#')) {  // block comments and /# developer blocks #/
            const char close = at(i + 1);
            const int from = line;
            bool closed = false;
            for (i += 2; i < n; ++i) {
                if (src[i] == close && at(i + 1) == '/') {
                    i += 2;
                    closed = true;
                    break;
                }
                if (src[i] == '\n') ++line;
            }
            if (!closed) note(from, close == '*' ? "comment not closed" : "/# block not closed");
            continue;
        }
        if (c == '#' && isIdentStart(at(i + 1))) {  // #include, #using, #using_animtree, #define ...: the whole line
            while (i < n && src[i] != '\n') ++i;
            continue;
        }
        if (c == '"' || ((c == '&' || c == '#') && at(i + 1) == '"')) {
            i += c == '"' ? 1 : 2;
            const int from = line;
            std::string s;
            bool closed = false;
            while (i < n) {
                const char d = src[i];
                if (d == '"') {
                    ++i;
                    closed = true;
                    break;
                }
                if (d == '\n') break;  // a string ends on its line
                if (d == '\\' && i + 1 < n && src[i + 1] != '\n') {
                    const char e = src[i + 1];
                    s += e == 'n' ? '\n' : e == 't' ? '\t' : e;
                    i += 2;
                    continue;
                }
                s += d;
                ++i;
            }
            if (!closed) note(from, "string not closed");
            out.push_back({Tk::Str, s, 0, from});
            continue;
        }
        if (isDigit(c) || (c == '.' && isDigit(at(i + 1)))) {
            size_t j = i;
            double v = 0;
            if (c == '0' && (at(i + 1) == 'x' || at(i + 1) == 'X')) {
                j = i + 2;
                while (j < n && std::isxdigit(static_cast<unsigned char>(src[j]))) ++j;
                v = double(std::strtoll(src.substr(i + 2, j - i - 2).c_str(), nullptr, 16));
            } else {
                while (j < n && isDigit(src[j])) ++j;
                if (j < n && src[j] == '.') {
                    ++j;
                    while (j < n && isDigit(src[j])) ++j;
                }
                if (j < n && (src[j] == 'e' || src[j] == 'E') &&
                    (isDigit(at(j + 1)) || ((at(j + 1) == '-' || at(j + 1) == '+') && isDigit(at(j + 2))))) {
                    j += 2;
                    while (j < n && isDigit(src[j])) ++j;
                }
                v = std::strtod(src.substr(i, j - i).c_str(), nullptr);
            }
            out.push_back({Tk::Num, src.substr(i, j - i), v, line});
            i = j;
            continue;
        }
        if (isIdentStart(c)) {  // a path such as scripts\mp\utility is one name (the call takes the part after ::)
            size_t j = i;
            while (j < n && (isIdentChar(src[j]) || (src[j] == '\\' && isIdentStart(at(j + 1))))) ++j;
            out.push_back({Tk::Ident, lower(src.substr(i, j - i)), 0, line});
            i = j;
            continue;
        }
        static const char* const kTwo[] = {"==", "!=", "<=", ">=", "&&", "||", "++", "--", "+=", "-=",
                                           "*=", "/=", "%=", "|=", "&=", "^=", "<<", ">>", "::"};
        bool two = false;
        for (const char* p : kTwo)
            if (c == p[0] && at(i + 1) == p[1]) {
                out.push_back({Tk::Punct, p, 0, line});
                i += 2;
                two = true;
                break;
            }
        if (two) continue;
        if (std::strchr("{}()[];,.+-*/%<>=!&|^~?:", c)) {
            out.push_back({Tk::Punct, std::string(1, c), 0, line});
            ++i;
            continue;
        }
        note(line, std::string("unexpected character '") + c + "' skipped");
        ++i;
    }
    out.push_back({Tk::End, "", 0, line});
    return out;
}

// ------------------------------------------------------------------------------------------------------------ AST

struct Expr {
    enum K { Num, Str, Var, Level, Self, Game, Anim, Undef, EmptyArr, Vec, Field, Index, Call, Unary, Binary, And, Or, FuncRef } k = Undef;
    int line = 0;
    double num = 0;
    std::string s;  // Str text, Var / Field / Call / FuncRef name, Unary / Binary operator
    std::vector<std::unique_ptr<Expr>> kids;  // Vec x y z; Field obj; Index obj idx; Unary e; Binary a b; Call args
    std::unique_ptr<Expr> obj, ptr;            // Call: the object of a method call; the function of a [[ ]] call
    bool thread = false;
};
using ExprP = std::unique_ptr<Expr>;

struct Stmt {
    enum K { Nop, ExprS, Assign, IncDec, Block, If, While, For, Foreach, Return, Break, Continue, Wait, Switch } k = Nop;
    int line = 0;
    ExprP a, b;               // Assign lhs rhs; IncDec lhs; If / While / For condition; Foreach array; Return; Wait; Switch
    std::string op;           // Assign "=", "+=" ...; IncDec "++" / "--"
    std::vector<std::unique_ptr<Stmt>> list;  // Block, Switch body
    std::unique_ptr<Stmt> s1, s2, s3;         // If then / else; While body; For init / step / body; Foreach body
    std::string v1, v2;                       // Foreach key (may be empty) and value
    struct Case {
        ExprP value;  // null: default
        size_t at = 0;
        int line = 0;
    };
    std::vector<Case> cases;
};
using StmtP = std::unique_ptr<Stmt>;

struct FuncDef {
    std::string name;
    std::vector<std::string> params;
    StmtP body;
    int line = 0;
};

struct ParseFail {
    int line;
    std::string msg;
};

class Parser {
public:
    Parser(const std::vector<Token>& t, NoteFn note) : t_(t), note_(std::move(note)) {}

    std::vector<FuncDef> file() {
        std::vector<FuncDef> out;
        while (peek().k != Tk::End) {
            while (word("private") || word("autoexec") || word("function")) ++i_;
            if (peek().k == Tk::Ident && punct("(", 1)) {
                FuncDef f;
                f.line = peek().line;
                f.name = t_[i_].s;
                i_ += 2;
                try {
                    if (!punct(")")) {
                        do {
                            f.params.push_back(ident());
                            if (accept("=")) {  // a default value (not in IW8): read and left out
                                note_(peek().line, "default value of parameter '" + f.params.back() + "' ignored");
                                expr();
                            }
                        } while (accept(","));
                    }
                    expect(")");
                    if (!punct("{")) fail("expected '{' to open " + f.name + "()");
                    f.body = stmt();
                } catch (const ParseFail& e) {
                    note_(e.line, e.msg + " (function " + f.name + "() skipped)");
                    skipBraces();
                    continue;
                }
                out.push_back(std::move(f));
            } else if (peek().k != Tk::End) {
                note_(peek().line, "unexpected " + describe(peek()) + " outside a function (skipped)");
                skipTopLevel();
            }
        }
        return out;
    }

private:
    const std::vector<Token>& t_;
    NoteFn note_;
    size_t i_ = 0;

    const Token& peek(size_t k = 0) const { return t_[std::min(i_ + k, t_.size() - 1)]; }
    bool punct(const char* p, size_t k = 0) const {
        const Token& x = peek(k);
        return x.k == Tk::Punct && x.s == p;
    }
    bool word(const char* w, size_t k = 0) const {
        const Token& x = peek(k);
        return x.k == Tk::Ident && x.s == w;
    }
    bool accept(const char* p) {
        if (!punct(p)) return false;
        ++i_;
        return true;
    }
    static std::string describe(const Token& x) {
        if (x.k == Tk::End) return "the end of the file";
        if (x.k == Tk::Str) return "a string";
        return "'" + x.s + "'";
    }
    [[noreturn]] void fail(const std::string& m) const { throw ParseFail{peek().line, m}; }
    void expect(const char* p) {
        if (!accept(p)) fail(std::string("expected '") + p + "' before " + describe(peek()));
    }
    std::string ident() {
        if (peek().k != Tk::Ident) fail("expected a name before " + describe(peek()));
        return t_[i_++].s;
    }
    template <typename T>
    static std::unique_ptr<T> make(typename T::K k, int line) {
        auto p = std::make_unique<T>();
        p->k = k;
        p->line = line;
        return p;
    }
    // After a broken function header: past the next balanced { ... } (or to the end).
    void skipBraces() {
        while (peek().k != Tk::End && !punct("{")) {
            if (peek().k == Tk::Ident && punct("(", 1) && i_ > 0 && t_[i_ - 1].k == Tk::Punct && t_[i_ - 1].s == "}") return;
            ++i_;
        }
        int depth = 0;
        while (peek().k != Tk::End) {
            if (punct("{")) ++depth;
            else if (punct("}") && --depth == 0) {
                ++i_;
                return;
            }
            ++i_;
        }
    }
    void skipTopLevel() {
        int depth = 0;
        while (peek().k != Tk::End) {
            if (punct("{")) ++depth;
            else if (punct("}")) {
                if (--depth <= 0) {
                    ++i_;
                    return;
                }
            } else if (punct(";") && depth == 0) {
                ++i_;
                return;
            }
            ++i_;
        }
    }

    // One statement; a statement that does not parse is noted and skipped (to its ';', or to the '}' that closes
    // the block it is in), so the rest of the function still runs.
    StmtP stmtSafe() {
        const size_t start = i_;
        try {
            return stmt();
        } catch (const ParseFail& e) {
            note_(e.line, e.msg + " (statement skipped)");
            i_ = std::max(i_, start);
            int depth = 0;
            while (peek().k != Tk::End) {
                if (punct("{")) {
                    ++depth;
                } else if (punct("}")) {
                    if (depth == 0) break;
                    if (--depth == 0) {
                        ++i_;
                        break;
                    }
                } else if (punct(";") && depth == 0) {
                    ++i_;
                    break;
                }
                ++i_;
            }
            if (i_ == start && peek().k != Tk::End && !punct("}")) ++i_;
            return make<Stmt>(Stmt::Nop, e.line);
        }
    }

    StmtP stmt() {
        const int line = peek().line;
        if (accept(";")) return make<Stmt>(Stmt::Nop, line);
        if (accept("{")) {
            auto b = make<Stmt>(Stmt::Block, line);
            while (!punct("}")) {
                if (peek().k == Tk::End) fail("missing '}' (the block opened on line " + std::to_string(line) + ")");
                b->list.push_back(stmtSafe());
            }
            ++i_;
            return b;
        }
        if (peek().k == Tk::Ident) {
            const std::string w = peek().s;
            if (w == "if") {
                ++i_;
                auto s = make<Stmt>(Stmt::If, line);
                expect("(");
                s->a = expr();
                expect(")");
                s->s1 = stmtSafe();
                if (word("else")) {
                    ++i_;
                    s->s2 = stmtSafe();
                }
                return s;
            }
            if (w == "while") {
                ++i_;
                auto s = make<Stmt>(Stmt::While, line);
                expect("(");
                s->a = expr();
                expect(")");
                s->s1 = stmtSafe();
                return s;
            }
            if (w == "for") {
                ++i_;
                auto s = make<Stmt>(Stmt::For, line);
                expect("(");
                if (!punct(";")) s->s1 = simple();
                expect(";");
                if (!punct(";")) s->a = expr();
                expect(";");
                if (!punct(")")) s->s2 = simple();
                expect(")");
                s->s3 = stmtSafe();
                return s;
            }
            if (w == "foreach") {
                ++i_;
                auto s = make<Stmt>(Stmt::Foreach, line);
                expect("(");
                s->v2 = ident();
                if (accept(",")) {
                    s->v1 = s->v2;
                    s->v2 = ident();
                }
                if (!word("in")) fail("expected 'in' in foreach");
                ++i_;
                s->a = expr();
                expect(")");
                s->s1 = stmtSafe();
                return s;
            }
            if (w == "return") {
                ++i_;
                auto s = make<Stmt>(Stmt::Return, line);
                if (!punct(";")) s->a = expr();
                expect(";");
                return s;
            }
            if (w == "break" || w == "continue") {
                ++i_;
                expect(";");
                return make<Stmt>(w == "break" ? Stmt::Break : Stmt::Continue, line);
            }
            if (w == "wait") {
                ++i_;
                auto s = make<Stmt>(Stmt::Wait, line);
                s->a = expr();
                expect(";");
                return s;
            }
            if (w == "waittillframeend") {  // the end of this frame: no time passes
                ++i_;
                expect(";");
                return make<Stmt>(Stmt::Nop, line);
            }
            if (w == "switch") {
                ++i_;
                auto s = make<Stmt>(Stmt::Switch, line);
                expect("(");
                s->a = expr();
                expect(")");
                expect("{");
                while (!punct("}")) {
                    if (peek().k == Tk::End) fail("missing '}' of the switch on line " + std::to_string(line));
                    if (word("case")) {
                        Stmt::Case c;
                        c.line = peek().line;
                        ++i_;
                        c.value = expr();
                        expect(":");
                        c.at = s->list.size();
                        s->cases.push_back(std::move(c));
                    } else if (word("default")) {
                        Stmt::Case c;
                        c.line = peek().line;
                        ++i_;
                        expect(":");
                        c.at = s->list.size();
                        s->cases.push_back(std::move(c));
                    } else {
                        s->list.push_back(stmtSafe());
                    }
                }
                ++i_;
                return s;
            }
            if (w == "else") fail("'else' without 'if'");
            if (w == "case" || w == "default") fail("'" + w + "' outside a switch");
            if (w == "do") fail("do ... while is not GSC");
        }
        auto s = simple();
        expect(";");
        return s;
    }

    static bool assignable(const Expr& e) { return e.k == Expr::Var || e.k == Expr::Field || e.k == Expr::Index; }

    // An assignment, ++ / --, or a call (no ';').
    StmtP simple() {
        const int line = peek().line;
        if (punct("++") || punct("--")) {
            auto s = make<Stmt>(Stmt::IncDec, line);
            s->op = t_[i_++].s;
            s->a = postfix();
            if (!assignable(*s->a)) fail("cannot " + s->op + " this");
            return s;
        }
        ExprP e = expr();
        static const char* const kAssign[] = {"=", "+=", "-=", "*=", "/=", "%=", "|=", "&=", "^="};
        for (const char* op : kAssign)
            if (punct(op)) {
                if (!assignable(*e)) fail("cannot assign to this");
                ++i_;
                auto s = make<Stmt>(Stmt::Assign, line);
                s->op = op;
                s->a = std::move(e);
                s->b = expr();
                return s;
            }
        if (punct("++") || punct("--")) {
            if (!assignable(*e)) fail("cannot " + peek().s + " this");
            auto s = make<Stmt>(Stmt::IncDec, line);
            s->op = t_[i_++].s;
            s->a = std::move(e);
            return s;
        }
        if (e->k != Expr::Call) fail("a statement that does nothing");
        auto s = make<Stmt>(Stmt::ExprS, line);
        s->a = std::move(e);
        return s;
    }

    ExprP expr() { return binary(0); }

    ExprP binary(size_t level) {
        static const std::vector<std::vector<const char*>> kLevels = {
            {"||"}, {"&&"}, {"|"}, {"^"}, {"&"}, {"==", "!="}, {"<", ">", "<=", ">="}, {"<<", ">>"}, {"+", "-"}, {"*", "/", "%"}};
        if (level == kLevels.size()) return unary();
        ExprP a = binary(level + 1);
        for (;;) {
            const char* hit = nullptr;
            for (const char* p : kLevels[level])
                if (punct(p)) hit = p;
            if (!hit) return a;
            const int line = peek().line;
            ++i_;
            ExprP b = binary(level + 1);
            const std::string op = hit;
            auto e = make<Expr>(op == "||" ? Expr::Or : op == "&&" ? Expr::And : Expr::Binary, line);
            e->s = op;
            e->kids.push_back(std::move(a));
            e->kids.push_back(std::move(b));
            a = std::move(e);
        }
    }

    ExprP unary() {
        const int line = peek().line;
        if (punct("!") || punct("-") || punct("~")) {
            const std::string op = t_[i_++].s;
            ExprP x = unary();
            if (op == "-" && x->k == Expr::Num) {
                x->num = -x->num;
                return x;
            }
            auto e = make<Expr>(Expr::Unary, line);
            e->s = op;
            e->kids.push_back(std::move(x));
            return e;
        }
        return postfix();
    }

    ExprP postfix() {
        ExprP e = primary();
        for (;;) {
            const int line = peek().line;
            if (accept(".")) {
                auto f = make<Expr>(Expr::Field, line);
                f->s = ident();
                f->kids.push_back(std::move(e));
                e = std::move(f);
            } else if (punct("[") && punct("[", 1)) {  // obj [[ f ]](args)
                e = callTail(std::move(e), false, line);
            } else if (accept("[")) {
                auto x = make<Expr>(Expr::Index, line);
                x->kids.push_back(std::move(e));
                x->kids.push_back(expr());
                expect("]");
                e = std::move(x);
            } else if (word("thread") || word("childthread")) {  // obj thread f(args)
                ++i_;
                e = callTail(std::move(e), true, line);
            } else if (peek().k == Tk::Ident && (punct("(", 1) || punct("::", 1))) {  // obj f(args)
                e = callTail(std::move(e), false, line);
            } else {
                return e;
            }
        }
    }

    ExprP callTail(ExprP obj, bool thread, int line) {
        auto c = make<Expr>(Expr::Call, line);
        c->obj = std::move(obj);
        c->thread = thread;
        if (punct("[") && punct("[", 1)) {
            i_ += 2;
            c->ptr = expr();
            expect("]");
            expect("]");
        } else {
            c->s = ident();
            if (accept("::")) c->s = ident();  // scripts\mp\utility::name -> name
        }
        expect("(");
        if (!accept(")")) {
            do c->kids.push_back(expr());
            while (accept(","));
            expect(")");
        }
        return c;
    }

    ExprP primary() {
        const Token& x = peek();
        const int line = x.line;
        if (x.k == Tk::Num) {
            auto e = make<Expr>(Expr::Num, line);
            e->num = x.n;
            ++i_;
            return e;
        }
        if (x.k == Tk::Str) {
            auto e = make<Expr>(Expr::Str, line);
            e->s = x.s;
            ++i_;
            return e;
        }
        if (x.k == Tk::Punct) {
            if (accept("(")) {
                ExprP a = expr();
                if (accept(",")) {
                    auto v = make<Expr>(Expr::Vec, line);
                    v->kids.push_back(std::move(a));
                    v->kids.push_back(expr());
                    expect(",");
                    v->kids.push_back(expr());
                    expect(")");
                    return v;
                }
                expect(")");
                return a;
            }
            if (punct("[") && punct("[", 1)) return callTail(nullptr, false, line);
            if (accept("[")) {
                expect("]");
                return make<Expr>(Expr::EmptyArr, line);
            }
            if (accept("::")) {
                auto f = make<Expr>(Expr::FuncRef, line);
                f->s = ident();
                return f;
            }
            if (accept("%")) {  // %animation: its name
                auto e = make<Expr>(Expr::Str, line);
                e->s = ident();
                return e;
            }
            fail("unexpected " + describe(x));
        }
        if (x.k == Tk::Ident) {
            const std::string w = x.s;
            if (w == "thread" || w == "childthread") {
                ++i_;
                return callTail(nullptr, true, line);
            }
            static const std::map<std::string, Expr::K> kWords = {{"level", Expr::Level}, {"self", Expr::Self}, {"game", Expr::Game},
                                                                  {"anim", Expr::Anim}, {"undefined", Expr::Undef}};
            if (const auto k = kWords.find(w); k != kWords.end()) {
                ++i_;
                return make<Expr>(k->second, line);
            }
            if (w == "true" || w == "false") {
                ++i_;
                auto e = make<Expr>(Expr::Num, line);
                e->num = w == "true" ? 1 : 0;
                return e;
            }
            if (punct("(", 1)) return callTail(nullptr, false, line);
            if (punct("::", 1)) {
                if (peek(2).k == Tk::Ident && punct("(", 3)) return callTail(nullptr, false, line);
                i_ += 2;
                auto f = make<Expr>(Expr::FuncRef, line);
                f->s = ident();
                return f;
            }
            ++i_;
            auto v = make<Expr>(Expr::Var, line);
            v->s = w;
            return v;
        }
        fail("unexpected " + describe(x));
    }
};

// ------------------------------------------------------------------------------------------------------- bytecode

enum class Op : uint8_t {
    PushNum, PushStr, PushUndef, PushLevel, PushSelf, PushGame, PushAnim, PushArr, PushFn,
    Load, Store, LoadArr, Dup, Dup2, Pop,
    GetField, GetFieldArr, SetField, GetIndex, SetIndex,
    MakeVec, Bin, Neg, Not, BitNot, Bool,
    Jmp, Jz, Jnz,
    Call, Builtin, CallPtr, Ret, RetUndef,
    Wait, WaitFrame, WaitTill,
};

enum BinOp { BAdd, BSub, BMul, BDiv, BMod, BEq, BNe, BLt, BGt, BLe, BGe, BBitAnd, BBitOr, BBitXor, BShl, BShr };

struct Ins {
    Op op = Op::Pop;
    int a = 0, b = 0, c = 0;
    int s = -1;  // string pool index (names, string literals)
    double d = 0;
    int line = 0;
};

struct Func {
    std::string name;
    int params = 0, locals = 0, line = 0;
    std::vector<Ins> code;
    std::vector<std::vector<int>> outs;  // waittill output variables (local indices), by WaitTill.a
};

// Builtins. The order is the id.
enum Bi {
    BiUnknown = -1,
    // functions
    BiIsDefined, BiGetEnt, BiGetEntArray, BiGetStruct, BiGetStructArray, BiSpawnStruct, BiSpawn, BiInt, BiFloat, BiAbs, BiMin, BiMax,
    BiFloor, BiCeil, BiSqrt, BiSquared, BiSin, BiCos, BiTan, BiRandomInt, BiRandomFloat, BiRandomIntRange, BiRandomFloatRange,
    BiGetTime, BiDistance, BiDistanceSquared, BiDistance2d, BiLength, BiLengthSquared, BiVectorNormalize, BiVectorDot, BiVectorCross,
    BiVectorToAngles, BiAnglesToForward, BiAnglesToRight, BiAnglesToUp, BiGetArrayKeys, BiIsArray, BiIsString, BiToLower, BiGetDvar,
    BiGetDvarInt, BiGetDvarFloat,
    // methods
    BiRotateYaw, BiRotatePitch, BiRotateRoll, BiRotateTo, BiRotateBy, BiRotateVelocity, BiMoveX, BiMoveY, BiMoveZ, BiMoveTo,
    BiMoveGravity, BiLinkTo, BiUnlink, BiHide, BiShow, BiDelete, BiNotify, BiEndon, BiGetOrigin,
    // known calls without anything to show in the editor
    BiNothing,
};

const std::unordered_map<std::string, int>& builtinIds() {
    static const std::unordered_map<std::string, int> ids = [] {
        std::unordered_map<std::string, int> m = {
            {"isdefined", BiIsDefined}, {"getent", BiGetEnt}, {"getentarray", BiGetEntArray}, {"getstruct", BiGetStruct},
            {"getstructarray", BiGetStructArray}, {"spawnstruct", BiSpawnStruct}, {"spawn", BiSpawn}, {"int", BiInt},
            {"float", BiFloat}, {"abs", BiAbs}, {"min", BiMin}, {"max", BiMax}, {"floor", BiFloor}, {"ceil", BiCeil},
            {"sqrt", BiSqrt}, {"squared", BiSquared}, {"sin", BiSin}, {"cos", BiCos}, {"tan", BiTan},
            {"randomint", BiRandomInt}, {"randomfloat", BiRandomFloat}, {"randomintrange", BiRandomIntRange},
            {"randomfloatrange", BiRandomFloatRange}, {"gettime", BiGetTime}, {"distance", BiDistance},
            {"distancesquared", BiDistanceSquared}, {"distance2d", BiDistance2d}, {"length", BiLength},
            {"lengthsquared", BiLengthSquared}, {"vectornormalize", BiVectorNormalize}, {"vectordot", BiVectorDot},
            {"vectorcross", BiVectorCross}, {"vectortoangles", BiVectorToAngles}, {"anglestoforward", BiAnglesToForward},
            {"anglestoright", BiAnglesToRight}, {"anglestoup", BiAnglesToUp}, {"getarraykeys", BiGetArrayKeys},
            {"isarray", BiIsArray}, {"isstring", BiIsString}, {"tolower", BiToLower}, {"getdvar", BiGetDvar},
            {"getdvarint", BiGetDvarInt}, {"getdvarfloat", BiGetDvarFloat},
            {"rotateyaw", BiRotateYaw}, {"rotatepitch", BiRotatePitch}, {"rotateroll", BiRotateRoll}, {"rotateto", BiRotateTo},
            {"rotateby", BiRotateBy}, {"rotatevelocity", BiRotateVelocity}, {"movex", BiMoveX}, {"movey", BiMoveY},
            {"movez", BiMoveZ}, {"moveto", BiMoveTo}, {"movegravity", BiMoveGravity}, {"linkto", BiLinkTo},
            {"unlink", BiUnlink}, {"hide", BiHide}, {"show", BiShow}, {"delete", BiDelete}, {"notify", BiNotify},
            {"endon", BiEndon}, {"getorigin", BiGetOrigin},
        };
        for (const char* name : {"playsound", "playloopsound", "stoploopsound", "stopsounds", "playsoundatpos", "playfx", "playfxontag",
                                 "stopfxontag", "playloopedfx", "spawnfx", "triggerfx", "loadfx", "precachemodel", "precacheshader",
                                 "precachestring", "precacheitem", "solid", "notsolid", "setcandamage", "disconnectpaths",
                                 "connectpaths", "makeusable", "makeunusable", "sethintstring", "setcursorhint", "setuserange",
                                 "iprintln", "iprintlnbold", "println", "print", "assert", "assertmsg", "assertex", "setdvar",
                                 "setdvarifuninitialized", "enablelinkto", "setcontents", "useanimtree", "scriptmodelclearanim",
                                 "setmovingplatformenabled", "setnonstick", "dontinterpolate"})
            m[name] = BiNothing;
        return m;
    }();
    return ids;
}

// ------------------------------------------------------------------------------------------------------- values

struct Array;
struct Value {
    enum K : uint8_t { Undef, Num, Str, Vec, Obj, Arr, Fn } k = Undef;
    double n = 0;
    Vec3 v;
    int o = -1;  // Obj: object index; Fn: function index
    std::string s;
    std::shared_ptr<Array> a;
};
struct Array {
    std::vector<Value> items;               // keys 0 .. n-1
    std::map<std::string, Value> named;     // string keys
};

Value num(double d) {
    Value v;
    v.k = Value::Num;
    v.n = d;
    return v;
}
Value str(std::string s) {
    Value v;
    v.k = Value::Str;
    v.s = std::move(s);
    return v;
}
Value vec(const Vec3& x) {
    Value v;
    v.k = Value::Vec;
    v.v = x;
    return v;
}
Value obj(int o) {
    Value v;
    v.k = Value::Obj;
    v.o = o;
    return v;
}
Value newArray() {
    Value v;
    v.k = Value::Arr;
    v.a = std::make_shared<Array>();
    return v;
}

std::string numText(double d) {
    if (std::isfinite(d) && d == std::floor(d) && std::fabs(d) < 1e15) {
        char b[32];
        std::snprintf(b, sizeof b, "%lld", (long long)d);
        return b;
    }
    char b[48];
    std::snprintf(b, sizeof b, "%g", d);
    return b;
}

std::string text(const Value& v) {
    switch (v.k) {
    case Value::Num: return numText(v.n);
    case Value::Str: return v.s;
    case Value::Vec: return "(" + numText(v.v.x) + ", " + numText(v.v.y) + ", " + numText(v.v.z) + ")";
    case Value::Undef: return "undefined";
    case Value::Arr: return "[array]";
    case Value::Fn: return "[function]";
    default: return "[object]";
    }
}

double numOf(const Value& v) {
    if (v.k == Value::Num) return v.n;
    if (v.k == Value::Str) return std::atof(v.s.c_str());
    return 0;
}

Vec3 vecOf(const Value& v) {
    if (v.k == Value::Vec) return v.v;
    const double d = numOf(v);
    return {d, d, d};
}

// ------------------------------------------------------------------------------------------------------- motion

// The fraction of a move done `x` seconds into a move of `T` seconds that speeds up for `ta` and slows down for `td`
// (the game's trapezoid: constant acceleration, cruise, constant deceleration).
double moveCurve(double x, double T, double ta, double td) {
    if (!(T > 0) || x >= T) return 1;
    if (x <= 0) return 0;
    ta = std::clamp(ta, 0.0, T);
    td = std::clamp(td, 0.0, T);
    if (ta + td > T) {
        const double k = T / (ta + td);
        ta *= k, td *= k;
    }
    const double v = 1.0 / (T - 0.5 * ta - 0.5 * td);
    if (x < ta) return 0.5 * v * x * x / ta;
    if (x <= T - td) return v * (0.5 * ta + (x - ta));
    const double r = T - x;
    return 1 - 0.5 * v * r * r / td;
}

struct Track {
    int kind = 0;  // 0 still at `from`, 1 from + delta * moveCurve, 2 thrown with velocity `delta` under gravity
    double start = 0, dur = 0, ta = 0, td = 0;
    Vec3 from, delta;
    Vec3 at(double t) const {
        if (kind == 0) return from;
        const double x = std::clamp(t - start, 0.0, dur);
        if (kind == 2) return from + delta * x + Vec3{0, 0, -0.5 * kGravity * x * x};
        return from + delta * moveCurve(x, dur, ta, td);
    }
};

Mat3 transposed(const Mat3& m) {
    Mat3 r;
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) r.m[i][j] = m.m[j][i];
    return r;
}

Vec3 matrixAngles(const Mat3& m) {
    return axisToAngles({Vec3{m.m[0][0], m.m[1][0], m.m[2][0]}, Vec3{m.m[0][1], m.m[1][1], m.m[2][1]}, Vec3{m.m[0][2], m.m[1][2], m.m[2][2]}});
}

double angleDelta(double a) {  // to [-180, 180)
    a = std::fmod(a + 180.0, 360.0);
    if (a < 0) a += 360.0;
    return a - 180.0;
}

Vec3 parse3(const std::string* s) {
    Vec3 v;
    if (s) std::sscanf(s->c_str(), "%lf %lf %lf", &v.x, &v.y, &v.z);
    return v;
}

// -------------------------------------------------------------------------------------------------------- engine

struct Engine {
    struct Ent {
        Id id = kNoId;
        std::string cls, targetname;
        std::vector<std::pair<std::string, std::string>> keys;
        bool brush = false, spawned = false;
        Vec3 o0, a0;
        Track pos, ang;
        int posGen = 0, angGen = 0;
        bool visible = true, deleted = false, animated = false;
        int obj = -1;
        int parent = -1;
        Vec3 linkOffset;
        Mat3 linkRot;
    };
    struct Object {
        int ent = -1;
        std::unordered_map<std::string, Value> fields;
    };
    struct Frame {
        int fn = 0;
        size_t pc = 0, base = 0;
        std::vector<Value> locals;
        Value self;
    };
    struct Thread {
        std::vector<Frame> frames;
        std::vector<Value> stack;
        double wake = 0;
        uint64_t seq = 0;
        bool dead = false, waiting = false;
        int waitObj = -1, waitOuts = -1;
        std::string waitEv;
        std::vector<std::pair<int, std::string>> endons;
    };
    struct Pending {  // a notify sent later: "movedone" / "rotatedone" when a move ends
        double time = 0;
        uint64_t seq = 0;
        int ent = -1, gen = 0;
        bool rotate = false;
    };

    std::vector<Func> funcs;
    std::unordered_map<std::string, int> funcIndex;
    std::vector<std::string> pool;
    std::vector<Ent> mapEnts;  // as loaded
    std::vector<Ent> ents;     // as the script has them (mapEnts + spawned)
    std::vector<Object> objects;
    std::vector<std::unique_ptr<Thread>> threads;
    std::vector<Pending> pending;
    std::vector<GscPose> poses;
    std::vector<GscNote> notes;
    std::set<std::pair<int, std::string>> noteSeen;
    double now = 0;
    uint64_t seq = 0, rng = 0;
    long steps = 0, starts = 0;
    bool loaded = false, halted = false;
    int mainFn = -1;

    void note(int line, const std::string& t) {
        if (noteSeen.insert({line, t}).second) notes.push_back({line, t});
    }

    // ---------------------------------------------------------------- compile

    struct Compiler {
        Engine& im;
        Func& f;
        std::unordered_map<std::string, int> locals;
        struct Loop {
            std::vector<size_t> breaks, continues;
            bool isSwitch = false;
        };
        std::vector<Loop> loops;
        int hidden = 0;

        size_t emit(Op op, int line, int a = 0, int b = 0, int c = 0, double d = 0, int s = -1) {
            Ins in;
            in.op = op, in.a = a, in.b = b, in.c = c, in.d = d, in.s = s, in.line = line;
            f.code.push_back(in);
            return f.code.size() - 1;
        }
        void patch(size_t at) { f.code[at].a = int(f.code.size()); }
        int local(const std::string& name) {
            if (const auto it = locals.find(name); it != locals.end()) return it->second;
            const int i = f.locals++;
            locals[name] = i;
            return i;
        }
        int hiddenLocal() { return local("$" + std::to_string(hidden++)); }
        int strIndex(const std::string& s) {
            im.pool.push_back(s);
            return int(im.pool.size() - 1);
        }

        void stmt(const Stmt& s) {
            switch (s.k) {
            case Stmt::Nop: break;
            case Stmt::ExprS:
                expr(*s.a);
                emit(Op::Pop, s.line);
                break;
            case Stmt::Assign: assign(*s.a, s.op, s.b.get(), s.line); break;
            case Stmt::IncDec: assign(*s.a, s.op == "++" ? "+=" : "-=", nullptr, s.line); break;
            case Stmt::Block:
                for (const auto& x : s.list) stmt(*x);
                break;
            case Stmt::If: {
                expr(*s.a);
                const size_t jz = emit(Op::Jz, s.line);
                stmt(*s.s1);
                if (s.s2) {
                    const size_t jend = emit(Op::Jmp, s.line);
                    patch(jz);
                    stmt(*s.s2);
                    patch(jend);
                } else {
                    patch(jz);
                }
                break;
            }
            case Stmt::While: {
                const int top = int(f.code.size());
                expr(*s.a);
                const size_t jz = emit(Op::Jz, s.line);
                loops.push_back({});
                stmt(*s.s1);
                emit(Op::Jmp, s.line, top);
                endLoop(top);
                patch(jz);
                break;
            }
            case Stmt::For: {
                if (s.s1) stmt(*s.s1);
                const int top = int(f.code.size());
                size_t jz = SIZE_MAX;
                if (s.a) {
                    expr(*s.a);
                    jz = emit(Op::Jz, s.line);
                }
                loops.push_back({});
                stmt(*s.s3);
                const int cont = int(f.code.size());
                if (s.s2) stmt(*s.s2);
                emit(Op::Jmp, s.line, top);
                endLoop(cont);
                if (jz != SIZE_MAX) patch(jz);
                break;
            }
            case Stmt::Foreach: {
                const int arr = hiddenLocal(), keys = hiddenLocal(), idx = hiddenLocal();
                expr(*s.a);
                emit(Op::Store, s.line, arr);
                emit(Op::Load, s.line, arr);
                emit(Op::Builtin, s.line, BiGetArrayKeys, 1, 0, 0, strIndex("getarraykeys"));
                emit(Op::Store, s.line, keys);
                emit(Op::PushNum, s.line, 0, 0, 0, 0);
                emit(Op::Store, s.line, idx);
                const int top = int(f.code.size());
                emit(Op::Load, s.line, idx);
                emit(Op::Load, s.line, keys);
                emit(Op::GetField, s.line, 0, 0, 0, 0, strIndex("size"));
                emit(Op::Bin, s.line, BLt);
                const size_t jz = emit(Op::Jz, s.line);
                emit(Op::Load, s.line, keys);
                emit(Op::Load, s.line, idx);
                emit(Op::GetIndex, s.line);
                if (!s.v1.empty()) {
                    emit(Op::Dup, s.line);
                    emit(Op::Store, s.line, local(s.v1));
                }
                const int key = hiddenLocal();
                emit(Op::Store, s.line, key);
                emit(Op::Load, s.line, arr);
                emit(Op::Load, s.line, key);
                emit(Op::GetIndex, s.line);
                emit(Op::Store, s.line, local(s.v2));
                loops.push_back({});
                stmt(*s.s1);
                const int cont = int(f.code.size());
                emit(Op::Load, s.line, idx);
                emit(Op::PushNum, s.line, 0, 0, 0, 1);
                emit(Op::Bin, s.line, BAdd);
                emit(Op::Store, s.line, idx);
                emit(Op::Jmp, s.line, top);
                endLoop(cont);
                patch(jz);
                break;
            }
            case Stmt::Return:
                if (s.a) {
                    expr(*s.a);
                    emit(Op::Ret, s.line);
                } else {
                    emit(Op::RetUndef, s.line);
                }
                break;
            case Stmt::Break:
            case Stmt::Continue: {
                Loop* target = nullptr;
                for (auto it = loops.rbegin(); it != loops.rend(); ++it)
                    if (s.k == Stmt::Break || !it->isSwitch) {
                        target = &*it;
                        break;
                    }
                if (!target) {
                    im.note(s.line, std::string(s.k == Stmt::Break ? "break" : "continue") + " outside a loop (skipped)");
                    break;
                }
                const size_t j = emit(Op::Jmp, s.line);
                (s.k == Stmt::Break ? target->breaks : target->continues).push_back(j);
                break;
            }
            case Stmt::Wait:
                expr(*s.a);
                emit(Op::Wait, s.line);
                break;
            case Stmt::Switch: {
                const int v = hiddenLocal();
                expr(*s.a);
                emit(Op::Store, s.line, v);
                std::vector<size_t> jumps(s.cases.size(), SIZE_MAX);
                int defaultCase = -1;
                for (size_t c = 0; c < s.cases.size(); ++c) {
                    if (!s.cases[c].value) {
                        defaultCase = int(c);
                        continue;
                    }
                    emit(Op::Load, s.cases[c].line, v);
                    expr(*s.cases[c].value);
                    emit(Op::Bin, s.cases[c].line, BEq);
                    jumps[c] = emit(Op::Jnz, s.cases[c].line);
                }
                const size_t toDefault = emit(Op::Jmp, s.line);
                loops.push_back({});
                loops.back().isSwitch = true;
                std::vector<int> caseAt(s.cases.size(), -1);
                for (size_t k = 0; k <= s.list.size(); ++k) {
                    for (size_t c = 0; c < s.cases.size(); ++c)
                        if (s.cases[c].at == k) caseAt[c] = int(f.code.size());
                    if (k < s.list.size()) stmt(*s.list[k]);
                }
                const int end = int(f.code.size());
                for (size_t c = 0; c < s.cases.size(); ++c)
                    if (jumps[c] != SIZE_MAX) f.code[jumps[c]].a = caseAt[c] >= 0 ? caseAt[c] : end;
                f.code[toDefault].a = defaultCase >= 0 && caseAt[size_t(defaultCase)] >= 0 ? caseAt[size_t(defaultCase)] : end;
                for (size_t j : loops.back().breaks) f.code[j].a = end;
                loops.pop_back();  // (a continue inside the switch went to the loop around it)
                break;
            }
            }
        }

        // Called right after the loop's last instruction: breaks go to the next one (where the loop's exit lands).
        void endLoop(int cont) {
            Loop l = std::move(loops.back());
            loops.pop_back();
            for (size_t j : l.continues) f.code[j].a = cont;
            for (size_t j : l.breaks) f.code[j].a = int(f.code.size());
        }

        // lhs op= rhs (rhs null: ++ / --, adds or takes 1).
        void assign(const Expr& lhs, const std::string& op, const Expr* rhs, int line) {
            static const std::map<std::string, int> kOps = {{"+=", BAdd}, {"-=", BSub}, {"*=", BMul}, {"/=", BDiv}, {"%=", BMod},
                                                            {"|=", BBitOr}, {"&=", BBitAnd}, {"^=", BBitXor}};
            const bool plain = op == "=";
            const int bop = plain ? 0 : kOps.at(op);
            const auto value = [&] {
                if (rhs) expr(*rhs);
                else emit(Op::PushNum, line, 0, 0, 0, 1);
            };
            if (lhs.k == Expr::Var) {
                const int l = local(lhs.s);
                if (!plain) emit(Op::Load, line, l);
                value();
                if (!plain) emit(Op::Bin, line, bop);
                emit(Op::Store, line, l);
            } else if (lhs.k == Expr::Field) {
                expr(*lhs.kids[0]);
                const int name = strIndex(lhs.s);
                if (!plain) {
                    emit(Op::Dup, line);
                    emit(Op::GetField, line, 0, 0, 0, 0, name);
                }
                value();
                if (!plain) emit(Op::Bin, line, bop);
                emit(Op::SetField, line, 0, 0, 0, 0, name);
            } else {  // Index: the array is made when the variable or field is still undefined (a[0] = x)
                const Expr& base = *lhs.kids[0];
                if (base.k == Expr::Var) {
                    emit(Op::LoadArr, line, local(base.s));
                } else if (base.k == Expr::Field) {
                    expr(*base.kids[0]);
                    emit(Op::GetFieldArr, line, 0, 0, 0, 0, strIndex(base.s));
                } else {
                    expr(base);
                }
                expr(*lhs.kids[1]);
                if (!plain) {
                    emit(Op::Dup2, line);
                    emit(Op::GetIndex, line);
                }
                value();
                if (!plain) emit(Op::Bin, line, bop);
                emit(Op::SetIndex, line);
            }
        }

        void expr(const Expr& e) {
            switch (e.k) {
            case Expr::Num: emit(Op::PushNum, e.line, 0, 0, 0, e.num); break;
            case Expr::Str: emit(Op::PushStr, e.line, 0, 0, 0, 0, strIndex(e.s)); break;
            case Expr::Var: emit(Op::Load, e.line, local(e.s)); break;
            case Expr::Level: emit(Op::PushLevel, e.line); break;
            case Expr::Self: emit(Op::PushSelf, e.line); break;
            case Expr::Game: emit(Op::PushGame, e.line); break;
            case Expr::Anim: emit(Op::PushAnim, e.line); break;
            case Expr::Undef: emit(Op::PushUndef, e.line); break;
            case Expr::EmptyArr: emit(Op::PushArr, e.line); break;
            case Expr::Vec:
                for (const auto& k : e.kids) expr(*k);
                emit(Op::MakeVec, e.line);
                break;
            case Expr::Field:
                expr(*e.kids[0]);
                emit(Op::GetField, e.line, 0, 0, 0, 0, strIndex(e.s));
                break;
            case Expr::Index:
                expr(*e.kids[0]);
                expr(*e.kids[1]);
                emit(Op::GetIndex, e.line);
                break;
            case Expr::Unary:
                expr(*e.kids[0]);
                emit(e.s == "!" ? Op::Not : e.s == "-" ? Op::Neg : Op::BitNot, e.line);
                break;
            case Expr::Binary: {
                static const std::map<std::string, int> kOps = {
                    {"+", BAdd}, {"-", BSub}, {"*", BMul}, {"/", BDiv}, {"%", BMod}, {"==", BEq}, {"!=", BNe}, {"<", BLt}, {">", BGt},
                    {"<=", BLe}, {">=", BGe}, {"&", BBitAnd}, {"|", BBitOr}, {"^", BBitXor}, {"<<", BShl}, {">>", BShr}};
                expr(*e.kids[0]);
                expr(*e.kids[1]);
                emit(Op::Bin, e.line, kOps.at(e.s));
                break;
            }
            case Expr::And:
            case Expr::Or: {
                expr(*e.kids[0]);
                emit(Op::Dup, e.line);
                const size_t j = emit(e.k == Expr::And ? Op::Jz : Op::Jnz, e.line);
                emit(Op::Pop, e.line);
                expr(*e.kids[1]);
                patch(j);
                emit(Op::Bool, e.line);
                break;
            }
            case Expr::FuncRef: {
                const auto it = im.funcIndex.find(e.s);
                if (it == im.funcIndex.end()) im.note(e.line, "::" + e.s + " is not a function of this script");
                emit(Op::PushFn, e.line, it == im.funcIndex.end() ? -1 : it->second);
                break;
            }
            case Expr::Call: call(e); break;
            }
        }

        void call(const Expr& c) {
            const int flags = (c.obj ? 1 : 0) | (c.thread ? 2 : 0);
            const int argc = int(c.kids.size());
            if (c.ptr) {
                if (c.obj) expr(*c.obj);
                for (const auto& a : c.kids) expr(*a);
                expr(*c.ptr);
                emit(Op::CallPtr, c.line, 0, argc, flags);
                return;
            }
            const std::string& name = c.s;
            if (name == "waittill" && !c.thread) {
                if (c.obj) expr(*c.obj);
                else emit(Op::PushSelf, c.line);
                if (c.kids.empty()) emit(Op::PushUndef, c.line);
                else expr(*c.kids[0]);
                std::vector<int> outs;
                for (size_t k = 1; k < c.kids.size(); ++k) {
                    if (c.kids[k]->k == Expr::Var) outs.push_back(local(c.kids[k]->s));
                    else im.note(c.line, "waittill: argument " + std::to_string(k + 1) + " is not a variable (ignored)");
                }
                f.outs.push_back(outs);
                emit(Op::WaitTill, c.line, int(f.outs.size() - 1));
                return;
            }
            if (name == "waitframe" && !c.obj) {
                emit(Op::WaitFrame, c.line);
                return;
            }
            if (const auto it = im.funcIndex.find(name); it != im.funcIndex.end()) {
                if (c.obj) expr(*c.obj);
                for (const auto& a : c.kids) expr(*a);
                emit(Op::Call, c.line, it->second, argc, flags);
                return;
            }
            const auto& ids = builtinIds();
            const auto bi = ids.find(name);
            if (bi == ids.end()) im.note(c.line, name + "() is not emulated in the editor (skipped)");
            if (c.obj) expr(*c.obj);
            for (const auto& a : c.kids) expr(*a);
            emit(Op::Builtin, c.line, bi == ids.end() ? BiUnknown : bi->second, argc, flags, 0, strIndex(name));
        }
    };

    bool compile(const std::string& source) {
        const NoteFn noteFn = [this](int line, const std::string& t) { note(line, t); };
        const std::vector<Token> toks = lex(source, noteFn);
        Parser parser(toks, noteFn);
        std::vector<FuncDef> defs = parser.file();
        for (const FuncDef& d : defs) {
            if (funcIndex.count(d.name)) {
                note(d.line, d.name + "() is defined twice (the first one is used)");
                continue;
            }
            funcIndex[d.name] = int(funcs.size());
            Func f;
            f.name = d.name;
            f.line = d.line;
            f.params = int(d.params.size());
            funcs.push_back(std::move(f));
        }
        std::set<std::string> done;
        for (const FuncDef& d : defs) {
            if (!done.insert(d.name).second) continue;
            Func& f = funcs[size_t(funcIndex[d.name])];
            Compiler c{*this, f, {}, {}, 0};
            for (const std::string& p : d.params) c.local(p);  // parameters are locals 0 .. n-1
            c.stmt(*d.body);
            c.emit(Op::RetUndef, d.line);
        }
        const auto m = funcIndex.find("user_main");
        mainFn = m == funcIndex.end() ? -1 : m->second;
        if (mainFn < 0) note(0, "the script has no user_main(): nothing runs");
        return mainFn >= 0;
    }

    // ---------------------------------------------------------------- entities

    void readMap(const MapFile& map) {
        mapEnts.clear();
        for (const Entity& e : map.entities) {
            if (e.isWorldspawn()) continue;
            Ent x;
            x.id = e.id;
            for (const auto& it : e.items) {
                if (const KeyValueLine* kv = it.kv()) x.keys.emplace_back(kv->name(), kv->value);
                else if (const Primitive* p = it.prim(); p && (p->kind == PrimKind::Brush || p->kind == PrimKind::Patch)) x.brush = true;
            }
            x.cls = e.classname();
            if (const std::string* tn = e.value("targetname")) x.targetname = *tn;
            if (const std::string* o = e.value("origin")) x.o0 = parse3(o);
            if (const std::string* a = e.value("angles")) x.a0 = parse3(a);
            else if (const std::string* a1 = e.value("angle")) x.a0 = Vec3{0, std::atof(a1->c_str()), 0};
            mapEnts.push_back(std::move(x));
        }
    }

    const std::string* key(const Ent& e, const std::string& k) const {
        for (const auto& [name, value] : e.keys)
            if (name == k) return &value;
        return nullptr;
    }

    int entOf(const Value& v) const {
        if (v.k != Value::Obj || v.o < 0 || size_t(v.o) >= objects.size()) return -1;
        const int e = objects[size_t(v.o)].ent;
        return e >= 0 && !ents[size_t(e)].deleted ? e : -1;
    }
    Value entValue(int e) {
        Ent& x = ents[size_t(e)];
        if (x.obj < 0) {
            x.obj = int(objects.size());
            objects.push_back({e, {}});
        }
        return obj(x.obj);
    }
    void worldPose(int e, double t, Vec3& o, Mat3& R, int depth = 0) const {
        const Ent& x = ents[size_t(e)];
        if (x.parent >= 0 && depth < 16) {
            Vec3 po;
            Mat3 pr;
            worldPose(x.parent, t, po, pr, depth + 1);
            o = po + pr * x.linkOffset;
            R = pr * x.linkRot;
            return;
        }
        o = x.pos.at(t);
        R = anglesToMatrix(x.ang.at(t));
    }
    Vec3 worldOrigin(int e) const {
        Vec3 o;
        Mat3 R;
        worldPose(e, now, o, R);
        return o;
    }
    Vec3 worldAngles(int e) const {
        const Ent& x = ents[size_t(e)];
        if (x.parent < 0) return x.ang.at(now);
        Vec3 o;
        Mat3 R;
        worldPose(e, now, o, R);
        return matrixAngles(R);
    }

    // A move or turn from where the entity is now; `kind` 1 eased, 2 thrown. Ends with "movedone" / "rotatedone".
    void startTrack(int e, bool rotate, int kind, const Vec3& delta, double time, double ta, double td, int line) {
        Ent& x = ents[size_t(e)];
        Track& tr = rotate ? x.ang : x.pos;
        const Vec3 cur = tr.at(now);
        if (!std::isfinite(time) || time < 0) time = 0;
        if (time > 0 && ta + td > time + 1e-9) note(line, "accel + decel time is more than the move's time (scaled to fit)");
        tr = Track{};
        tr.from = cur;
        if (time <= 0) {
            tr.kind = 0;
            tr.from = kind == 2 ? cur : cur + delta;
        } else {
            tr.kind = kind, tr.start = now, tr.dur = time, tr.ta = std::max(0.0, ta), tr.td = std::max(0.0, td), tr.delta = delta;
        }
        x.animated = true;
        const int gen = rotate ? ++x.angGen : ++x.posGen;
        pending.push_back({now + std::max(0.0, time), ++seq, e, gen, rotate});
    }

    // ---------------------------------------------------------------- threads

    Thread* newThread(int fn, std::vector<Value> args, const Value& self) {
        if (fn < 0 || size_t(fn) >= funcs.size()) return nullptr;
        size_t alive = 0;
        for (const auto& t : threads) alive += t->dead ? 0 : 1;
        if (alive >= kMaxThreads) {
            note(funcs[size_t(fn)].line, "too many threads: " + funcs[size_t(fn)].name + "() not started");
            return nullptr;
        }
        if (++starts > kStartsPerAdvance) {
            note(funcs[size_t(fn)].line, "threads start threads without waiting: emulation stopped");
            halted = true;
            return nullptr;
        }
        auto th = std::make_unique<Thread>();
        pushFrame(*th, fn, std::move(args), self);
        th->wake = now;
        th->seq = ++seq;
        threads.push_back(std::move(th));
        return threads.back().get();
    }

    void pushFrame(Thread& th, int fn, std::vector<Value> args, const Value& self) {
        const Func& f = funcs[size_t(fn)];
        Frame fr;
        fr.fn = fn;
        fr.base = th.stack.size();
        fr.self = self;
        fr.locals.resize(size_t(std::max(f.locals, f.params)));
        for (size_t k = 0; k < args.size() && k < size_t(f.params); ++k) fr.locals[k] = std::move(args[k]);
        th.frames.push_back(std::move(fr));
    }

    void returnFrom(Thread& th, Value v) {
        const size_t base = th.frames.back().base;
        th.frames.pop_back();
        th.stack.resize(std::min(base, th.stack.size()));
        if (th.frames.empty()) th.dead = true;
        else th.stack.push_back(std::move(v));
    }

    int objIndex(const Value& v) const {
        if (v.k != Value::Obj || v.o < 0 || size_t(v.o) >= objects.size()) return -1;
        const int e = objects[size_t(v.o)].ent;
        if (e >= 0 && ents[size_t(e)].deleted) return -1;
        return v.o;
    }

    // Ends the threads that endon this, and resumes the ones waiting for it at once, before the notifier goes on (as
    // the game's VM resumes them inside notify).
    void notify(int o, const std::string& ev, const std::vector<Value>& args, int depth) {
        std::vector<Thread*> woken;
        for (auto& tp : threads) {
            Thread& t = *tp;
            if (t.dead) continue;
            for (const auto& [eo, ee] : t.endons)
                if (eo == o && ee == ev) {
                    t.dead = true;
                    break;
                }
            if (t.dead || !t.waiting || t.waitObj != o || t.waitEv != ev) continue;
            t.waiting = false;
            t.wake = now;
            t.seq = ++seq;
            if (!t.frames.empty() && t.waitOuts >= 0) {
                Frame& fr = t.frames.back();
                const auto& outs = funcs[size_t(fr.fn)].outs[size_t(t.waitOuts)];
                for (size_t k = 0; k < outs.size(); ++k)
                    if (size_t(outs[k]) < fr.locals.size()) fr.locals[size_t(outs[k])] = k < args.size() ? args[k] : Value{};
            }
            woken.push_back(&t);
        }
        for (Thread* t : woken)
            if (!t->dead && !t->waiting && depth < kMaxNestedStarts && !halted) run(*t, depth + 1);
    }

    bool truthy(const Value& v) const {
        switch (v.k) {
        case Value::Undef: return false;
        case Value::Num: return v.n != 0;
        case Value::Str: return !v.s.empty();
        case Value::Obj: return objIndex(v) >= 0;
        default: return true;
        }
    }

    bool same(const Value& a, const Value& b) const {
        const bool ua = a.k == Value::Undef || (a.k == Value::Obj && objIndex(a) < 0);
        const bool ub = b.k == Value::Undef || (b.k == Value::Obj && objIndex(b) < 0);
        if (ua || ub) return ua && ub;
        if (a.k != b.k) {
            if ((a.k == Value::Num && b.k == Value::Str) || (a.k == Value::Str && b.k == Value::Num)) return text(a) == text(b);
            return false;
        }
        switch (a.k) {
        case Value::Num: return a.n == b.n;
        case Value::Str: return a.s == b.s;
        case Value::Vec: return a.v == b.v;
        case Value::Obj: return a.o == b.o;
        case Value::Arr: return a.a == b.a;
        case Value::Fn: return a.o == b.o;
        default: return false;
        }
    }

    Value binary(int op, const Value& a, const Value& b, int line) {
        if (op == BEq) return num(same(a, b) ? 1 : 0);
        if (op == BNe) return num(same(a, b) ? 0 : 1);
        if (op == BAdd && (a.k == Value::Str || b.k == Value::Str)) return str(text(a) + text(b));
        const bool va = a.k == Value::Vec, vb = b.k == Value::Vec;
        if (va || vb) {
            const Vec3 x = vecOf(a), y = vecOf(b);
            switch (op) {
            case BAdd: if (va && vb) return vec(x + y); break;
            case BSub: if (va && vb) return vec(x - y); break;
            case BMul: return vec(Vec3{x.x * y.x, x.y * y.y, x.z * y.z});
            case BDiv:
                if (!vb && numOf(b) != 0) return vec(x * (1.0 / numOf(b)));
                break;
            default: break;
            }
            note(line, "this operation on a vector is not possible (gives undefined)");
            return {};
        }
        if ((a.k != Value::Num && a.k != Value::Str) || (b.k != Value::Num && b.k != Value::Str)) {
            note(line, std::string("arithmetic on ") + (a.k == Value::Undef || b.k == Value::Undef ? "undefined" : "a non-number") +
                           " (gives undefined)");
            return {};
        }
        const double x = numOf(a), y = numOf(b);
        const auto ints = [](double d) { return (long long)d; };
        switch (op) {
        case BAdd: return num(x + y);
        case BSub: return num(x - y);
        case BMul: return num(x * y);
        case BDiv:
            if (y == 0) {
                note(line, "division by zero (gives 0)");
                return num(0);
            }
            return num(x / y);
        case BMod:
            if (y == 0) {
                note(line, "% by zero (gives 0)");
                return num(0);
            }
            return num(std::fmod(x, y));
        case BLt: return num(x < y);
        case BGt: return num(x > y);
        case BLe: return num(x <= y);
        case BGe: return num(x >= y);
        case BBitAnd: return num(double(ints(x) & ints(y)));
        case BBitOr: return num(double(ints(x) | ints(y)));
        case BBitXor: return num(double(ints(x) ^ ints(y)));
        case BShl: return num(double(ints(x) << (ints(y) & 63)));
        case BShr: return num(double(ints(x) >> (ints(y) & 63)));
        default: return {};
        }
    }

    Value getField(const Value& o, const std::string& name, int line) {
        if (name == "size") {
            if (o.k == Value::Arr) return num(double(o.a->items.size() + o.a->named.size()));
            if (o.k == Value::Str) return num(double(o.s.size()));
        }
        if (o.k == Value::Obj && o.o >= 0 && size_t(o.o) < objects.size()) {
            Object& ob = objects[size_t(o.o)];
            if (ob.ent >= 0) {
                if (ents[size_t(ob.ent)].deleted) {
                    note(line, "." + name + " of a deleted entity (gives undefined)");
                    return {};
                }
                if (name == "origin") return vec(worldOrigin(ob.ent));
                if (name == "angles") return vec(worldAngles(ob.ent));
            }
            if (const auto f = ob.fields.find(name); f != ob.fields.end()) return f->second;
            if (ob.ent >= 0)
                if (const std::string* v = key(ents[size_t(ob.ent)], name)) return str(*v);
            return {};
        }
        if (o.k == Value::Undef) note(line, "." + name + " of undefined (gives undefined)");
        return {};
    }

    void setField(const Value& o, const std::string& name, Value v, int line) {
        if (o.k != Value::Obj || o.o < 0 || size_t(o.o) >= objects.size()) {
            note(line, "cannot set ." + name + " here: not an object");
            return;
        }
        Object& ob = objects[size_t(o.o)];
        if (ob.ent >= 0) {
            Ent& x = ents[size_t(ob.ent)];
            if (x.deleted) return;
            if (name == "origin" || name == "angles") {
                if (v.k != Value::Vec) {
                    note(line, "." + name + " must be a vector");
                    return;
                }
                Track& tr = name == "origin" ? x.pos : x.ang;
                tr = Track{};
                tr.from = v.v;
                ++(name == "origin" ? x.posGen : x.angGen);
                x.animated = true;
                return;
            }
        }
        ob.fields[name] = std::move(v);
    }

    Value getIndex(const Value& o, const Value& k, int line) {
        if (o.k == Value::Arr) {
            if (k.k == Value::Num) {
                const double d = k.n;
                if (d >= 0 && d < double(o.a->items.size())) return o.a->items[size_t(d)];
                return {};
            }
            if (k.k == Value::Str) {
                const auto it = o.a->named.find(k.s);
                return it == o.a->named.end() ? Value{} : it->second;
            }
            return {};
        }
        if (o.k == Value::Vec && k.k == Value::Num && k.n >= 0 && k.n < 3) return num(o.v[int(k.n)]);
        if (o.k == Value::Str && k.k == Value::Num && k.n >= 0 && k.n < double(o.s.size())) return str(std::string(1, o.s[size_t(k.n)]));
        if (o.k == Value::Undef) note(line, "[] of undefined (gives undefined)");
        return {};
    }

    void setIndex(const Value& o, const Value& k, Value v, int line) {
        if (o.k != Value::Arr) {
            note(line, "[] = on something that is not an array (skipped)");
            return;
        }
        if (k.k == Value::Num && k.n >= 0) {
            const size_t i = size_t(k.n);
            if (i >= kMaxArray) {
                note(line, "array index too large (skipped)");
                return;
            }
            if (i >= o.a->items.size()) o.a->items.resize(i + 1);
            o.a->items[i] = std::move(v);
        } else if (k.k == Value::Str) {
            o.a->named[k.s] = std::move(v);
        } else {
            note(line, "array key must be a number or a string (skipped)");
        }
    }

    double random01() {
        rng = rng * 6364136223846793005ull + 1442695040888963407ull;
        return double(rng >> 11) / 9007199254740992.0;
    }

    Value lookup(const std::vector<Value>& a, bool many, int line) {
        if (a.size() < 2 || a[0].k == Value::Undef) {
            if (a.size() < 2) note(line, "getent / getentarray need a value and a key");
            return many ? newArray() : Value{};
        }
        const std::string value = text(a[0]), k = text(a[1]);
        Value out = many ? newArray() : Value{};
        for (size_t e = 0; e < ents.size(); ++e) {
            const Ent& x = ents[e];
            if (x.deleted) continue;
            const std::string* v = k == "classname" ? &x.cls : key(x, k);
            if (!v || *v != value) continue;
            if (!many) return entValue(int(e));
            out.a->items.push_back(entValue(int(e)));
        }
        return out;
    }

    // A call of the engine (or one this file stands in for). `self` is the object of a method call.
    Value builtin(Thread& th, int id, const std::string& name, std::vector<Value>& a, const Value* self, int line, int depth) {
        const auto arg = [&](size_t k) -> const Value& {
            static const Value none;
            return k < a.size() ? a[k] : none;
        };
        const auto n = [&](size_t k) { return numOf(arg(k)); };
        const auto v = [&](size_t k) { return vecOf(arg(k)); };
        const auto needEnt = [&]() -> int {
            if (!self) {
                note(line, name + "() needs an entity: ent " + name + "(...)");
                return -1;
            }
            const int e = entOf(*self);
            if (e < 0) note(line, name + "() on " + (self->k == Value::Obj ? "a deleted entity" : "something that is not an entity") + " (skipped)");
            return e;
        };
        switch (id) {
        case BiUnknown:
        case BiNothing: return {};
        case BiIsDefined: return num(arg(0).k == Value::Undef ? 0 : arg(0).k == Value::Obj ? (objIndex(arg(0)) >= 0 ? 1 : 0) : 1);
        case BiGetEnt:
        case BiGetStruct: return lookup(a, false, line);
        case BiGetEntArray:
        case BiGetStructArray: return lookup(a, true, line);
        case BiSpawnStruct: objects.push_back({}); return obj(int(objects.size() - 1));
        case BiSpawn: {
            Ent x;
            x.spawned = true;
            x.cls = text(arg(0));
            x.o0 = v(1);
            x.pos.from = x.o0;
            ents.push_back(std::move(x));
            return entValue(int(ents.size() - 1));
        }
        case BiInt: return num(std::trunc(n(0)));
        case BiFloat: return num(n(0));
        case BiAbs: return num(std::fabs(n(0)));
        case BiMin: return num(std::min(n(0), n(1)));
        case BiMax: return num(std::max(n(0), n(1)));
        case BiFloor: return num(std::floor(n(0)));
        case BiCeil: return num(std::ceil(n(0)));
        case BiSqrt: return num(std::sqrt(std::max(0.0, n(0))));
        case BiSquared: return num(n(0) * n(0));
        case BiSin: return num(std::sin(n(0) * kPi / 180));
        case BiCos: return num(std::cos(n(0) * kPi / 180));
        case BiTan: return num(std::tan(n(0) * kPi / 180));
        case BiRandomInt: return num(n(0) > 0 ? std::floor(random01() * std::floor(n(0))) : 0);
        case BiRandomFloat: return num(random01() * n(0));
        case BiRandomIntRange: {
            const double lo = std::floor(n(0)), hi = std::floor(n(1));
            return num(hi > lo ? lo + std::floor(random01() * (hi - lo)) : lo);
        }
        case BiRandomFloatRange: return num(n(0) + random01() * (n(1) - n(0)));
        case BiGetTime: return num(std::floor(now * 1000 + 1e-6));
        case BiDistance: return num(length(v(0) - v(1)));
        case BiDistanceSquared: {
            const Vec3 d = v(0) - v(1);
            return num(dot(d, d));
        }
        case BiDistance2d: {
            const Vec3 d = v(0) - v(1);
            return num(std::sqrt(d.x * d.x + d.y * d.y));
        }
        case BiLength: return num(length(v(0)));
        case BiLengthSquared: return num(dot(v(0), v(0)));
        case BiVectorNormalize: {
            const double l = length(v(0));
            return vec(l > 0 ? v(0) * (1.0 / l) : Vec3{});
        }
        case BiVectorDot: return num(dot(v(0), v(1)));
        case BiVectorCross: return vec(cross(v(0), v(1)));
        case BiVectorToAngles: {
            const Vec3 d = v(0);
            const double yaw = std::atan2(d.y, d.x) * 180 / kPi, flat = std::sqrt(d.x * d.x + d.y * d.y);
            const double pitch = -std::atan2(d.z, flat) * 180 / kPi;
            return vec(Vec3{pitch < 0 ? pitch + 360 : pitch, yaw < 0 ? yaw + 360 : yaw, 0});
        }
        case BiAnglesToForward:
        case BiAnglesToRight:
        case BiAnglesToUp: {
            const Mat3 R = anglesToMatrix(v(0));
            const int col = id == BiAnglesToForward ? 0 : id == BiAnglesToRight ? 1 : 2;
            const Vec3 c{R.m[0][col], R.m[1][col], R.m[2][col]};
            return vec(id == BiAnglesToRight ? -c : c);  // right is -Y of the model axes (left-handed "right")
        }
        case BiGetArrayKeys: {
            Value out = newArray();
            if (arg(0).k == Value::Arr) {
                for (size_t k = 0; k < arg(0).a->items.size(); ++k) out.a->items.push_back(num(double(k)));
                for (const auto& [k, x] : arg(0).a->named) out.a->items.push_back(str(k));
            }
            return out;
        }
        case BiIsArray: return num(arg(0).k == Value::Arr);
        case BiIsString: return num(arg(0).k == Value::Str);
        case BiToLower: return str(lower(text(arg(0))));
        case BiGetDvar: return str("");
        case BiGetDvarInt:
        case BiGetDvarFloat: return num(0);
        case BiRotateYaw:
        case BiRotatePitch:
        case BiRotateRoll: {
            const int e = needEnt();
            if (e < 0) return {};
            const double d = n(0);
            const Vec3 delta = id == BiRotateYaw ? Vec3{0, d, 0} : id == BiRotatePitch ? Vec3{d, 0, 0} : Vec3{0, 0, d};
            startTrack(e, true, 1, delta, n(1), n(2), n(3), line);
            return {};
        }
        case BiRotateTo:
        case BiRotateBy: {
            const int e = needEnt();
            if (e < 0) return {};
            Vec3 delta = v(0);
            if (id == BiRotateTo) {
                const Vec3 cur = ents[size_t(e)].ang.at(now);
                delta = Vec3{angleDelta(delta.x - cur.x), angleDelta(delta.y - cur.y), angleDelta(delta.z - cur.z)};
            }
            startTrack(e, true, 1, delta, n(1), n(2), n(3), line);
            return {};
        }
        case BiRotateVelocity: {
            const int e = needEnt();
            if (e < 0) return {};
            double T = std::max(0.0, n(1)), ta = std::clamp(n(2), 0.0, T), td = std::clamp(n(3), 0.0, T);
            if (ta + td > T && T > 0) {
                const double k = T / (ta + td);
                ta *= k, td *= k;
            }
            startTrack(e, true, 1, v(0) * (T - 0.5 * ta - 0.5 * td), T, ta, td, line);
            return {};
        }
        case BiMoveX:
        case BiMoveY:
        case BiMoveZ:
        case BiMoveTo: {
            const int e = needEnt();
            if (e < 0) return {};
            Vec3 delta;
            if (id == BiMoveTo) delta = v(0) - ents[size_t(e)].pos.at(now);
            else (id == BiMoveX ? delta.x : id == BiMoveY ? delta.y : delta.z) = n(0);
            startTrack(e, false, 1, delta, n(1), n(2), n(3), line);
            return {};
        }
        case BiMoveGravity: {
            const int e = needEnt();
            if (e < 0) return {};
            startTrack(e, false, 2, v(0), n(1), 0, 0, line);
            return {};
        }
        case BiLinkTo: {
            const int e = needEnt();
            if (e < 0) return {};
            const int p = entOf(arg(0));
            if (p < 0) {
                note(line, "linkto: not an entity (skipped)");
                return {};
            }
            for (int q = p, depth = 0; q >= 0 && depth < 32; q = ents[size_t(q)].parent, ++depth)
                if (q == e) {
                    note(line, "linkto would link an entity to itself (skipped)");
                    return {};
                }
            Vec3 oc, op;
            Mat3 rc, rp;
            worldPose(e, now, oc, rc);
            worldPose(p, now, op, rp);
            const Mat3 inv = transposed(rp);
            Ent& x = ents[size_t(e)];
            x.linkOffset = inv * (oc - op);
            x.linkRot = inv * rc;
            x.parent = p;
            x.animated = true;
            return {};
        }
        case BiUnlink: {
            const int e = needEnt();
            if (e < 0 || ents[size_t(e)].parent < 0) return {};
            Vec3 o;
            Mat3 R;
            worldPose(e, now, o, R);
            Ent& x = ents[size_t(e)];
            x.parent = -1;
            x.pos = Track{};
            x.pos.from = o;
            x.ang = Track{};
            x.ang.from = matrixAngles(R);
            ++x.posGen, ++x.angGen;
            return {};
        }
        case BiHide:
        case BiShow: {
            const int e = needEnt();
            if (e < 0) return {};
            ents[size_t(e)].visible = id == BiShow;
            ents[size_t(e)].animated = true;
            return {};
        }
        case BiDelete: {
            const int e = needEnt();
            if (e < 0) return {};
            const int o = ents[size_t(e)].obj;
            ents[size_t(e)].deleted = true;
            ents[size_t(e)].visible = false;
            ents[size_t(e)].animated = true;
            if (o >= 0) notify(o, "death", {}, depth);
            return {};
        }
        case BiNotify:
        case BiEndon: {
            const int o = self ? objIndex(*self) : objIndex(th.frames.back().self);
            if (o < 0 || a.empty()) return {};
            if (id == BiEndon) {
                th.endons.emplace_back(o, text(a[0]));
                return {};
            }
            notify(o, text(a[0]), std::vector<Value>(a.begin() + 1, a.end()), depth);
            return {};
        }
        case BiGetOrigin: {
            const int e = needEnt();
            return e < 0 ? Value{} : vec(worldOrigin(e));
        }
        default: return {};
        }
    }

    // Runs one thread until it waits or ends. `depth`: how many `thread f()` starts this run is inside.
    void run(Thread& th, int depth) {
        long local = 0;
        while (!th.dead && !th.frames.empty() && !halted) {
            Frame& fr = th.frames.back();
            const Func& fn = funcs[size_t(fr.fn)];
            if (fr.pc >= fn.code.size()) {
                returnFrom(th, {});
                continue;
            }
            const Ins& in = fn.code[fr.pc++];
            if (++local > kStepsPerResume) {
                note(in.line, "a loop runs without a wait: " + fn.name + "() stopped");
                th.dead = true;
                return;
            }
            if (++steps > kStepsPerAdvance) {
                note(0, "the script does too much work: emulation stopped");
                halted = true;
                return;
            }
            auto& st = th.stack;
            const auto pop = [&]() {
                Value x = std::move(st.back());
                st.pop_back();
                return x;
            };
            switch (in.op) {
            case Op::PushNum: st.push_back(num(in.d)); break;
            case Op::PushStr: st.push_back(str(pool[size_t(in.s)])); break;
            case Op::PushUndef: st.emplace_back(); break;
            case Op::PushLevel: st.push_back(obj(0)); break;
            case Op::PushGame: st.push_back(obj(1)); break;
            case Op::PushAnim: st.push_back(obj(2)); break;
            case Op::PushSelf: st.push_back(fr.self); break;
            case Op::PushArr: st.push_back(newArray()); break;
            case Op::PushFn: {
                Value f;
                if (in.a >= 0) {
                    f.k = Value::Fn;
                    f.o = in.a;
                }
                st.push_back(f);
                break;
            }
            case Op::Load: st.push_back(fr.locals[size_t(in.a)]); break;
            case Op::Store: fr.locals[size_t(in.a)] = pop(); break;
            case Op::LoadArr: {
                Value& l = fr.locals[size_t(in.a)];
                if (l.k == Value::Undef) l = newArray();
                st.push_back(l);
                break;
            }
            case Op::Dup: st.push_back(st.back()); break;
            case Op::Dup2: {
                const Value a = st[st.size() - 2], b = st.back();
                st.push_back(a);
                st.push_back(b);
                break;
            }
            case Op::Pop: st.pop_back(); break;
            case Op::GetField: {
                const Value o = pop();
                st.push_back(getField(o, pool[size_t(in.s)], in.line));
                break;
            }
            case Op::GetFieldArr: {
                const Value o = pop();
                Value f = getField(o, pool[size_t(in.s)], in.line);
                if (f.k == Value::Undef) {
                    f = newArray();
                    setField(o, pool[size_t(in.s)], f, in.line);
                }
                st.push_back(std::move(f));
                break;
            }
            case Op::SetField: {
                Value x = pop();
                const Value o = pop();
                setField(o, pool[size_t(in.s)], std::move(x), in.line);
                break;
            }
            case Op::GetIndex: {
                const Value k = pop();
                const Value o = pop();
                st.push_back(getIndex(o, k, in.line));
                break;
            }
            case Op::SetIndex: {
                Value x = pop();
                const Value k = pop();
                const Value o = pop();
                setIndex(o, k, std::move(x), in.line);
                break;
            }
            case Op::MakeVec: {
                const Value z = pop(), y = pop(), x = pop();
                st.push_back(vec(Vec3{numOf(x), numOf(y), numOf(z)}));
                break;
            }
            case Op::Bin: {
                const Value b = pop();
                const Value a = pop();
                st.push_back(binary(in.a, a, b, in.line));
                break;
            }
            case Op::Neg: {
                const Value a = pop();
                st.push_back(a.k == Value::Vec ? vec(-a.v) : a.k == Value::Num ? num(-a.n) : Value{});
                break;
            }
            case Op::Not: st.push_back(num(truthy(pop()) ? 0 : 1)); break;
            case Op::BitNot: st.push_back(num(double(~(long long)numOf(pop())))); break;
            case Op::Bool: st.push_back(num(truthy(pop()) ? 1 : 0)); break;
            case Op::Jmp: fr.pc = size_t(in.a); break;
            case Op::Jz:
                if (!truthy(pop())) fr.pc = size_t(in.a);
                break;
            case Op::Jnz:
                if (truthy(pop())) fr.pc = size_t(in.a);
                break;
            case Op::Call:
            case Op::CallPtr: {
                int target = in.a;
                if (in.op == Op::CallPtr) {
                    const Value f = pop();
                    target = f.k == Value::Fn ? f.o : -1;
                }
                std::vector<Value> args(st.end() - in.b, st.end());
                st.resize(st.size() - size_t(in.b));
                Value self = fr.self;
                if (in.c & 1) self = pop();
                if (target < 0) {
                    note(in.line, "call through a function reference that is not a function of this script (skipped)");
                    st.emplace_back();
                    break;
                }
                if (in.c & 2) {
                    st.emplace_back();
                    Thread* t = newThread(target, std::move(args), self);
                    if (t && depth < kMaxNestedStarts) run(*t, depth + 1);  // a new thread runs at once, up to its first wait
                    break;
                }
                if (th.frames.size() >= kMaxFrames) {
                    note(in.line, "calls nest too deep: " + funcs[size_t(target)].name + "() stopped");
                    th.dead = true;
                    return;
                }
                pushFrame(th, target, std::move(args), self);  // `fr` is not used after this
                break;
            }
            case Op::Builtin: {
                std::vector<Value> args(st.end() - in.b, st.end());
                st.resize(st.size() - size_t(in.b));
                Value self;
                const bool method = in.c & 1;
                if (method) self = pop();
                Value r = builtin(th, in.a, pool[size_t(in.s)], args, method ? &self : nullptr, in.line, depth);
                th.stack.push_back(std::move(r));
                break;
            }
            case Op::Ret: {
                Value r = pop();
                returnFrom(th, std::move(r));
                break;
            }
            case Op::RetUndef: returnFrom(th, {}); break;
            case Op::Wait: {
                double s = numOf(pop());
                if (!std::isfinite(s) || s <= 0) s = kFrame;
                th.wake = now + s;
                th.seq = ++seq;
                return;
            }
            case Op::WaitFrame:
                st.emplace_back();
                th.wake = now + kFrame;
                th.seq = ++seq;
                return;
            case Op::WaitTill: {
                const Value ev = pop();
                const Value o = pop();
                st.emplace_back();
                const int oi = objIndex(o);
                if (oi < 0) {
                    note(in.line, "waittill on undefined: the thread ends (as in game)");
                    th.dead = true;
                    return;
                }
                th.waiting = true;
                th.waitObj = oi;
                th.waitEv = text(ev);
                th.waitOuts = in.a;
                th.seq = ++seq;
                return;
            }
            }
        }
    }

    void restart() {
        ents = mapEnts;
        for (Ent& e : ents) {
            e.pos = Track{};
            e.pos.from = e.o0;
            e.ang = Track{};
            e.ang.from = e.a0;
        }
        objects.assign(3, Object{});  // level, game, anim
        threads.clear();
        pending.clear();
        poses.clear();
        now = 0;
        seq = 0;
        rng = 0x2545F4914F6CDD1Dull;
        halted = false;
        if (mainFn >= 0) newThread(mainFn, {}, obj(0));
    }

    void advanceTo(double T) {
        if (!loaded) return;
        if (!std::isfinite(T) || T < 0) T = 0;
        if (T < now - 1e-9) restart();
        steps = 0;
        starts = 0;
        constexpr double kEps = 1e-9;
        while (!halted) {
            // The next thing due: a notify of a move that ended, else a thread whose wait is over (time, then order).
            double best = std::numeric_limits<double>::infinity();
            uint64_t bestSeq = UINT64_MAX;
            int kind = -1;
            size_t at = 0;
            for (size_t i = 0; i < pending.size(); ++i)
                if (pending[i].time <= T + kEps && (pending[i].time < best || (pending[i].time == best && pending[i].seq < bestSeq))) {
                    best = pending[i].time, bestSeq = pending[i].seq, kind = 0, at = i;
                }
            for (size_t i = 0; i < threads.size(); ++i) {
                const Thread& t = *threads[i];
                if (t.dead || t.waiting || t.wake > T + kEps) continue;
                if (t.wake < best - kEps || (std::fabs(t.wake - best) <= kEps && kind == 1 && t.seq < bestSeq)) {
                    best = t.wake, bestSeq = t.seq, kind = 1, at = i;
                }
            }
            if (kind < 0) break;
            now = std::max(now, best);
            if (kind == 0) {
                const Pending p = pending[at];
                pending.erase(pending.begin() + std::ptrdiff_t(at));
                const Ent& x = ents[size_t(p.ent)];
                if (!x.deleted && (p.rotate ? x.angGen : x.posGen) == p.gen && x.obj >= 0)
                    notify(x.obj, p.rotate ? "rotatedone" : "movedone", {}, 0);
            } else {
                run(*threads[at], 0);
            }
            if (threads.size() > 256) {
                size_t live = 0;
                for (const auto& t : threads) live += t->dead ? 0 : 1;
                if (live * 2 < threads.size())
                    threads.erase(std::remove_if(threads.begin(), threads.end(), [](const auto& t) { return t->dead; }), threads.end());
            }
        }
        now = std::max(now, T);
        computePoses();
    }

    void computePoses() {
        poses.clear();
        for (size_t e = 0; e < ents.size(); ++e) {
            const Ent& x = ents[e];
            if (!x.animated || x.spawned || x.id == kNoId) continue;
            GscPose p;
            p.entity = x.id;
            p.targetname = x.targetname;
            p.brush = x.brush;
            p.restOrigin = x.o0;
            p.restAngles = x.a0;
            Vec3 o;
            Mat3 R;
            worldPose(int(e), now, o, R);
            p.origin = o;
            p.angles = x.parent >= 0 ? matrixAngles(R) : x.ang.at(now);
            p.visible = x.visible && !x.deleted;
            p.deleted = x.deleted;
            const Mat3 rel = R * transposed(anglesToMatrix(x.a0));
            for (int i = 0; i < 3; ++i)
                for (int j = 0; j < 3; ++j) p.R[i][j] = rel.m[i][j];
            p.pivot = x.o0;
            p.move = o - x.o0;
            poses.push_back(std::move(p));
        }
    }
};

}  // namespace gscemu

struct GscEmulation::Impl : gscemu::Engine {};

GscEmulation::GscEmulation() : impl_(std::make_unique<Impl>()) {}
GscEmulation::~GscEmulation() = default;
GscEmulation::GscEmulation(GscEmulation&&) noexcept = default;
GscEmulation& GscEmulation::operator=(GscEmulation&&) noexcept = default;

bool GscEmulation::load(const std::string& source, const MapFile& map) {
    impl_ = std::make_unique<Impl>();
    if (source.empty()) {
        impl_->note(0, "no script");
        return false;
    }
    try {
        impl_->loaded = impl_->compile(source);
    } catch (const std::exception& e) {
        impl_->note(0, std::string("the script could not be read: ") + e.what());
        impl_->loaded = false;
    }
    impl_->readMap(map);
    impl_->restart();
    impl_->computePoses();
    return impl_->loaded;
}

void GscEmulation::clear() { impl_ = std::make_unique<Impl>(); }
bool GscEmulation::loaded() const { return impl_->loaded; }

void GscEmulation::advanceTo(double t) {
    try {
        impl_->advanceTo(t);
    } catch (const std::exception& e) {  // never into the editor
        impl_->note(0, std::string("emulation stopped: ") + e.what());
        impl_->halted = true;
    }
}

double GscEmulation::time() const { return impl_->now; }
const std::vector<GscPose>& GscEmulation::poses() const { return impl_->poses; }
const GscPose* GscEmulation::pose(const std::string& targetname) const {
    for (const GscPose& p : impl_->poses)
        if (p.targetname == targetname) return &p;
    return nullptr;
}
const std::vector<GscNote>& GscEmulation::notes() const { return impl_->notes; }
size_t GscEmulation::functionCount() const { return impl_->funcs.size(); }
size_t GscEmulation::threadCount() const {
    size_t n = 0;
    for (const auto& t : impl_->threads) n += t->dead ? 0 : 1;
    return n;
}

// ------------------------------------------------------------------------------------------------------- files

namespace {

std::filesystem::path scriptPathOf(const MapFile& map, const std::string& mapPath) {
    for (const Entity& e : map.entities) {
        if (!e.isWorldspawn()) continue;
        const std::string* v = e.value("iw8r_script");
        if (!v || v->empty()) return {};
        std::filesystem::path p(*v);
        if (p.is_relative()) {
            if (mapPath.empty()) return {};
            p = std::filesystem::path(mapPath).parent_path() / p;
        }
        return p;
    }
    return {};
}

uint64_t fnv(uint64_t h, const std::string& s) {
    for (unsigned char c : s) h = (h ^ c) * 1099511628211ull;
    return (h ^ 0xff) * 1099511628211ull;
}

}  // namespace

std::string readMapScript(const MapFile& map, const std::string& mapPath, std::string* path) {
    const std::filesystem::path p = scriptPathOf(map, mapPath);
    if (path) *path = p.empty() ? std::string() : p.string();
    if (p.empty()) return {};
    std::ifstream f(p, std::ios::binary);
    if (!f) return {};
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

void MapScriptRunner::clear() {
    emu_.clear();
    path_.clear();
    text_.clear();
    revision_ = ~0ull;
    mapKey_ = fileKey_ = 0;
    lastCheck_ = -1e9;
    notesShown_ = 0;
}

void MapScriptRunner::update(const MapFile& map, const std::string& mapPath, uint64_t revision, double t, double recheckSeconds) {
    bool reload = false;
    if (revision != revision_) {
        revision_ = revision;
        // What the script can see of the map: the script key and every entity's keys (and whether it has brushes).
        uint64_t k = 1469598103934665603ull;
        bool hasScript = false;
        for (const Entity& e : map.entities)
            if (e.isWorldspawn())
                if (const std::string* v = e.value("iw8r_script"); v && !v->empty()) hasScript = true;
        if (hasScript) {
            k = fnv(k, mapPath);
            for (const Entity& e : map.entities) {
                k = (k ^ e.id) * 1099511628211ull;
                for (const auto& it : e.items) {
                    if (const KeyValueLine* kv = it.kv()) k = fnv(fnv(k, kv->key), kv->value);
                    else if (it.prim()) k = (k ^ 0x9e37) * 1099511628211ull;
                }
            }
        } else {
            k = 0;
        }
        if (k != mapKey_) {
            mapKey_ = k;
            reload = true;
        }
    }
    if (mapKey_ == 0) {  // no script key: nothing runs
        if (emu_.loaded() || !path_.empty()) {
            clear();
            revision_ = revision;
        }
        return;
    }
    if (reload || t < lastCheck_ || t - lastCheck_ >= recheckSeconds) {
        lastCheck_ = t;
        const std::filesystem::path p = scriptPathOf(map, mapPath);
        uint64_t fk = fnv(1469598103934665603ull, p.string());
        std::error_code ec;
        const auto size = std::filesystem::file_size(p, ec);
        if (!ec) fk = fnv(fk, std::to_string(size));
        const auto when = std::filesystem::last_write_time(p, ec);
        if (!ec) fk = fnv(fk, std::to_string((long long)when.time_since_epoch().count()));
        if (fk != fileKey_) {
            fileKey_ = fk;
            reload = true;
        }
    }
    if (reload) {
        std::string p;
        const std::string text = readMapScript(map, mapPath, &p);
        if (text != text_ || p != path_) notesShown_ = 0;  // a new script: its notes are new
        text_ = text;
        path_ = p;
        if (text.empty()) emu_.clear();
        else emu_.load(text, map);
    }
    if (emu_.loaded()) emu_.advanceTo(t);
}

std::vector<GscNote> MapScriptRunner::takeNewNotes() {
    std::vector<GscNote> out;
    const auto& all = emu_.notes();
    for (size_t i = notesShown_; i < all.size(); ++i) out.push_back(all[i]);
    notesShown_ = std::max(notesShown_, all.size());
    return out;
}

}  // namespace iw8r
