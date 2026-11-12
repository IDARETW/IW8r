// IW8 Radiant Suite - command scripts (roadmap 9.10). See command_script.h for the grammar.
#include "command_script.h"

#include <cstdio>

#include <cmath>
#include <cstdlib>
#include <set>
#include <utility>

#include "key_replace.h"
#include "layout_tools.h"
#include "map_queries.h"
#include "map_text.h"

namespace iw8r {
namespace {

// A command failure. Thrown by the command handlers, caught by runCommandScript.
struct Fail {
    std::string msg;
};

[[noreturn]] void fail(const std::string& msg) { throw Fail{msg}; }

bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\r'; }

// Splits one line into tokens. Returns false (with `err`) on a malformed quote.
bool tokenize(const std::string& line, std::vector<std::string>& out, std::string& err) {
    out.clear();
    const size_t n = line.size();
    size_t i = 0;
    while (true) {
        while (i < n && isSpace(line[i])) ++i;
        if (i >= n || line[i] == '#') return true;
        std::string tok;
        if (line[i] == '"') {
            ++i;
            bool closed = false;
            while (i < n) {
                const char c = line[i++];
                if (c == '"') {
                    closed = true;
                    break;
                }
                if (c != '\\') {
                    tok += c;
                    continue;
                }
                if (i >= n) break;
                const char e = line[i++];
                switch (e) {
                    case '"': tok += '"'; break;
                    case '\\': tok += '\\'; break;
                    case 'n': tok += '\n'; break;
                    case 't': tok += '\t'; break;
                    case 'r': tok += '\r'; break;
                    default:
                        err = std::string("unknown escape \\") + e;
                        return false;
                }
            }
            if (!closed) {
                err = "unterminated quoted string";
                return false;
            }
            if (i < n && !isSpace(line[i])) {
                err = "expected a space after a quoted argument";
                return false;
            }
        } else {
            while (i < n && !isSpace(line[i])) {
                if (line[i] == '"') {
                    err = "quote inside a bare word (quote the whole argument)";
                    return false;
                }
                tok += line[i++];
            }
        }
        out.push_back(std::move(tok));
    }
}

double num(const std::string& s) {
    char* end = nullptr;
    const double v = std::strtod(s.c_str(), &end);
    if (s.empty() || end != s.c_str() + s.size() || !std::isfinite(v)) fail("not a number: '" + s + "'");
    return v;
}

int integer(const std::string& s) {
    const double v = num(s);
    if (v != std::floor(v) || std::fabs(v) > 1e6) fail("not a whole number: '" + s + "'");
    return int(v);
}

Vec3 vec3At(const std::vector<std::string>& t, size_t from) { return {num(t[from]), num(t[from + 1]), num(t[from + 2])}; }

std::string coords(const Vec3& v) { return formatNumber(v.x) + " " + formatNumber(v.y) + " " + formatNumber(v.z); }

// Keys, classnames and material names: one token without whitespace or quotes.
void checkName(const std::string& s, const char* what) {
    if (s.empty()) fail(std::string(what) + " cannot be empty");
    for (char c : s)
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '"') fail(std::string(what) + " cannot contain spaces or quotes: '" + s + "'");
}

// Values may contain spaces but not quotes or line breaks (the map text cannot hold them).
void checkValue(const std::string& s) {
    for (char c : s)
        if (c == '"' || c == '\r' || c == '\n') fail("a value cannot contain quotes or line breaks");
}

// Every selectable object of the map, as the select-all selection: each primitive, or the entity for a point entity.
std::vector<SelRef> allRefs(const MapFile& m) {
    std::vector<SelRef> out;
    for (const Entity& e : m.entities) {
        bool any = false;
        for (const EntityItem& it : e.items)
            if (it.isPrimitive()) {
                any = true;
                out.push_back({e.id, it.id});
            }
        if (!any) out.push_back({e.id, kNoId});
    }
    return out;
}

void applySelection(Document& doc, const std::vector<SelRef>& refs, bool additive) {
    if (!additive) doc.deselectAll();
    for (const SelRef& r : refs) doc.select(r, true);
}

void restoreSelection(Document& doc, const std::set<SelRef>& sel) {
    doc.deselectAll();
    for (const SelRef& r : sel) doc.select(r, true);
}

std::set<Id> selectedEntities(const Document& doc) {
    std::set<Id> ids;
    for (const SelRef& r : doc.selection()) ids.insert(r.entity);
    return ids;
}

void requireSelection(const Document& doc) {
    if (doc.selection().empty()) fail("nothing selected");
}

std::string countOf(size_t n, const char* singular, const char* plural) { return std::to_string(n) + " " + (n == 1 ? singular : plural); }

const char* kSelectUsage =
    "select class <classname> [key value] | select key <key> <value> | select all | select none | select add ...";

// select <class|key|all|none> ...; `from` is the index of the selector word.
std::string selectCommand(Document& doc, const std::vector<std::string>& t, size_t from, bool additive) {
    if (t.size() <= from) fail(std::string("usage: ") + kSelectUsage);
    const std::string& what = t[from];
    if (what == "none") {
        if (additive || t.size() != from + 1) fail(std::string("usage: ") + kSelectUsage);
        doc.deselectAll();
        return "selection cleared";
    }
    if (what == "all") {
        if (t.size() != from + 1) fail(std::string("usage: ") + kSelectUsage);
        applySelection(doc, allRefs(doc.map()), additive);
    } else if (what == "class") {
        if (t.size() != from + 2 && t.size() != from + 4) fail(std::string("usage: ") + kSelectUsage);
        std::vector<SelRef> refs = findByClass(doc.map(), t[from + 1]);
        if (t.size() == from + 4) {
            std::set<Id> withKey;
            for (const SelRef& r : findByKeyValue(doc.map(), t[from + 2], t[from + 3], false)) withKey.insert(r.entity);
            std::vector<SelRef> kept;
            for (const SelRef& r : refs)
                if (withKey.count(r.entity)) kept.push_back(r);
            refs.swap(kept);
        }
        applySelection(doc, refs, additive);
    } else if (what == "key") {
        if (t.size() != from + 3) fail(std::string("usage: ") + kSelectUsage);
        applySelection(doc, findByKeyValue(doc.map(), t[from + 1], t[from + 2], false), additive);
    } else {
        fail("unknown selector '" + what + "' (use class, key, all or none)");
    }
    return std::to_string(doc.selection().size()) + " selected";
}

std::string setCommand(Document& doc, const std::vector<std::string>& t) {
    if (t.size() != 3) fail("usage: set <key> <value>");
    checkName(t[1], "key");
    checkValue(t[2]);
    requireSelection(doc);
    size_t changed = 0;
    for (Id e : selectedEntities(doc))
        if (doc.setKeyValue(e, t[1], t[2])) ++changed;
    return countOf(changed, "entity", "entities") + " changed";
}

std::string removeCommand(Document& doc, const std::vector<std::string>& t) {
    if (t.size() != 2) fail("usage: remove <key>");
    checkName(t[1], "key");
    requireSelection(doc);
    size_t removed = 0;
    for (Id e : selectedEntities(doc))
        if (doc.removeKey(e, t[1])) ++removed;
    return countOf(removed, "key", "keys") + " removed";
}

std::string moveCommand(Document& doc, const std::vector<std::string>& t) {
    if (t.size() != 4) fail("usage: move <dx> <dy> <dz>");
    requireSelection(doc);
    doc.translateSelection(vec3At(t, 1));
    return "moved " + countOf(doc.selection().size(), "selected object", "selected objects");  // translate of a zero offset is a no-op
}

std::string cloneCommand(Document& doc, const std::vector<std::string>& t) {
    if (t.size() != 4) fail("usage: clone <dx> <dy> <dz>");
    requireSelection(doc);
    if (!doc.cloneSelection(vec3At(t, 1))) fail("clone made no copies");
    return "cloned " + countOf(doc.selection().size(), "selected object", "selected objects");
}

std::string deleteCommand(Document& doc, const std::vector<std::string>& t) {
    if (t.size() != 1) fail("usage: delete");
    requireSelection(doc);
    if (!doc.deleteSelection()) fail("delete removed nothing");
    return "deleted";
}

std::string addCommand(Document& doc, const std::vector<std::string>& t) {
    if (t.size() < 5 || (t.size() - 5) % 2 != 0) fail("usage: add <classname> <x> <y> <z> [key value]...");
    const Vec3 at = vec3At(t, 2);
    std::vector<std::pair<std::string, std::string>> kv = {{"classname", t[1]}, {"origin", coords(at)}};
    for (size_t i = 5; i < t.size(); i += 2) {
        checkName(t[i], "key");
        checkValue(t[i + 1]);
        bool replaced = false;
        for (auto& e : kv)
            if (e.first == t[i]) {
                e.second = t[i + 1];
                replaced = true;
                break;
            }
        if (!replaced) kv.push_back({t[i], t[i + 1]});
    }
    checkName(kv[0].second, "classname");
    if (kv[0].second == "worldspawn") fail("add cannot create a worldspawn");
    const std::string cls = kv[0].second;
    const Id id = doc.addEntity(kv, {}, "Script add");
    return "added " + cls + " as entity " + std::to_string(id);
}

std::string materialCommand(Document& doc, const std::vector<std::string>& t) {
    if (t.size() != 2) fail("usage: material <name>");
    checkName(t[1], "material");
    std::vector<Document::PrimEdit> edits;
    bool anyPrim = false;
    for (const SelRef& r : doc.selection()) {
        if (r.item == kNoId) continue;
        const EntityItem* it = doc.findItem(r.entity, r.item);
        if (!it || !it->prim()) continue;
        anyPrim = true;
        Primitive p = *it->prim();
        if (p.setMaterial(t[1])) edits.push_back({r.entity, r.item, p});
    }
    if (!anyPrim) fail("material needs selected brushes or patches");
    doc.editPrimitives(edits, "Apply " + t[1]);
    return countOf(edits.size(), "primitive", "primitives") + " changed";
}

std::string arrayCommand(Document& doc, const std::vector<std::string>& t) {
    if (t.size() != 5) fail("usage: array <dx> <dy> <dz> <count>");
    const int count = integer(t[4]);
    if (count < 1 || count > 10000) fail("count must be between 1 and 10000");
    requireSelection(doc);
    const size_t made = arrayDuplicate(doc, vec3At(t, 1), count);
    if (made == 0) fail("array made no copies");
    return countOf(made, "copy", "copies") + " made";
}

std::string alignCommand(Document& doc, const std::vector<std::string>& t) {
    if (t.size() != 3) fail("usage: align <x|y|z> <min|center|max>");
    int axis = -1;
    if (t[1] == "x") axis = 0;
    else if (t[1] == "y") axis = 1;
    else if (t[1] == "z") axis = 2;
    else fail("axis must be x, y or z");
    AlignMode mode = AlignMode::Min;
    if (t[2] == "min") mode = AlignMode::Min;
    else if (t[2] == "center") mode = AlignMode::Center;
    else if (t[2] == "max") mode = AlignMode::Max;
    else fail("alignment must be min, center or max");
    requireSelection(doc);
    return alignSelection(doc, axis, mode) ? "aligned" : "already aligned";
}

std::string replaceKeyCommand(Document& doc, const std::vector<std::string>& t) {
    const std::string usage = "usage: replace-key <key> <old> <new> [substring] [selected]";
    if (t.size() < 4 || t.size() > 6) fail(usage);
    bool substring = false, selected = false;
    for (size_t i = 4; i < t.size(); ++i) {
        if (t[i] == "substring" && !substring) substring = true;
        else if (t[i] == "selected" && !selected) selected = true;
        else fail(usage);
    }
    checkName(t[1], "key");
    if (t[2].empty()) fail("replace-key needs an old value");
    checkValue(t[3]);
    if (selected) requireSelection(doc);
    return countOf(replaceKeyValues(doc, t[1], t[2], t[3], substring, selected), "entity", "entities") + " changed";
}

// Runs one tokenized command. Mutating commands run inside a group named after the command, so each is one undo
// step on its own, and inside a one-step script the outer group's name wins.
std::string execCommand(Document& doc, const std::vector<std::string>& t, bool oneUndoStep) {
    const std::string& cmd = t[0];
    if (cmd == "undo" || cmd == "redo") {
        if (t.size() != 1) fail("usage: " + cmd);
        if (oneUndoStep) fail(cmd + " cannot run inside a one-step script");
        if (!(cmd == "undo" ? doc.undo() : doc.redo())) fail("nothing to " + cmd);
        return cmd + " done";
    }
    struct Group {
        Document& d;
        explicit Group(Document& doc, const std::string& name) : d(doc) { d.beginGroup(name); }
        ~Group() { d.endGroup(); }
    };
    const std::string group = "Script " + cmd;
    if (cmd == "select") {
        Group g(doc, group);
        if (t.size() > 1 && t[1] == "add") return selectCommand(doc, t, 2, true);
        return selectCommand(doc, t, 1, false);
    }
    if (cmd == "set") { Group g(doc, group); return setCommand(doc, t); }
    if (cmd == "remove") { Group g(doc, group); return removeCommand(doc, t); }
    if (cmd == "move") { Group g(doc, group); return moveCommand(doc, t); }
    if (cmd == "clone") { Group g(doc, group); return cloneCommand(doc, t); }
    if (cmd == "delete") { Group g(doc, group); return deleteCommand(doc, t); }
    if (cmd == "add") { Group g(doc, group); return addCommand(doc, t); }
    if (cmd == "material") { Group g(doc, group); return materialCommand(doc, t); }
    if (cmd == "array") { Group g(doc, group); return arrayCommand(doc, t); }
    if (cmd == "align") { Group g(doc, group); return alignCommand(doc, t); }
    if (cmd == "replace-key") { Group g(doc, group); return replaceKeyCommand(doc, t); }
    fail("unknown command '" + cmd + "'");
}

std::vector<std::string> splitLines(const std::string& text) {
    std::vector<std::string> lines;
    size_t start = 0;
    while (true) {
        const size_t nl = text.find('\n', start);
        if (nl == std::string::npos) {
            lines.push_back(text.substr(start));
            break;
        }
        lines.push_back(text.substr(start, nl - start));
        start = nl + 1;
    }
    return lines;
}

}  // namespace

ScriptResult runCommandScript(Document& doc, const std::string& text, bool oneUndoStep, std::vector<std::string>* log) {
    ScriptResult res;
    if (doc.inGroup()) {
        res.ok = false;
        res.message = "a script cannot run inside an open undo group";
        return res;
    }
    const std::string before = doc.text();
    const std::set<SelRef> selBefore = doc.selection();
    if (oneUndoStep) doc.beginGroup("Script");

    const std::vector<std::string> lines = splitLines(text);
    size_t executed = 0;
    bool failed = false;
    int failLine = 0;
    std::string failMsg;
    for (size_t i = 0; i < lines.size() && !failed; ++i) {
        const int lineNo = int(i + 1);
        std::vector<std::string> tok;
        std::string err;
        if (!tokenize(lines[i], tok, err)) {
            failed = true;
            failLine = lineNo;
            failMsg = err;
            break;
        }
        if (tok.empty()) continue;
        try {
            const std::string result = execCommand(doc, tok, oneUndoStep);
            ++executed;
            if (log) log->push_back("line " + std::to_string(lineNo) + ": " + result);
        } catch (const Fail& f) {
            failed = true;
            failLine = lineNo;
            failMsg = f.msg;
        }
    }
    if (oneUndoStep) doc.endGroup();

    res.commands = executed;
    if (!failed) {
        res.ok = true;
        res.message = "ran " + countOf(executed, "command", "commands");
        return res;
    }
    res.ok = false;
    res.line = failLine;
    res.message = "line " + std::to_string(failLine) + ": " + failMsg;
    if (oneUndoStep) {
        // The group is the newest undo entry: undoing it restores the exact prior text. Then the selection the
        // script started from comes back, since the undo only restores the selection of the group's first edit.
        if (doc.text() != before && doc.canUndo()) {
            doc.undo();
            res.message += " (script rolled back)";
        }
        restoreSelection(doc, selBefore);
    }
    return res;
}

void MacroRecorder::record(const std::string& line) {
    size_t b = 0, e = line.size();
    while (b < e && isSpace(line[b])) ++b;
    while (e > b && isSpace(line[e - 1])) --e;
    if (b == e) return;
    std::string l = line.substr(b, e - b);
    // A drag reports many small moves: consecutive "move" lines merge into one, and a repeated "set" of the same
    // key (one per selected entity) keeps only the last value.
    double d[2][3];
    if (!lines_.empty() && std::sscanf(lines_.back().c_str(), "move %lf %lf %lf", &d[0][0], &d[0][1], &d[0][2]) == 3 &&
        std::sscanf(l.c_str(), "move %lf %lf %lf", &d[1][0], &d[1][1], &d[1][2]) == 3) {
        lines_.back() = cmdMove(Vec3{d[0][0] + d[1][0], d[0][1] + d[1][1], d[0][2] + d[1][2]});
        return;
    }
    if (!lines_.empty() && l == lines_.back()) return;
    lines_.push_back(std::move(l));
}

std::string MacroRecorder::text() const {
    std::string out;
    for (const std::string& l : lines_) out += l + "\n";
    return out;
}

std::string quoteArg(const std::string& s) {
    bool bare = !s.empty() && s[0] != '#';
    for (char c : s)
        if (isSpace(c) || c == '\n' || c == '\t' || c == '"' || c == '\\') bare = false;
    if (bare) return s;
    std::string out = "\"";
    for (char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\t': out += "\\t"; break;
            case '\r': out += "\\r"; break;
            default: out += c;
        }
    }
    return out + "\"";
}

std::string cmdMove(const Vec3& delta) { return "move " + coords(delta); }

std::string cmdClone(const Vec3& delta) { return "clone " + coords(delta); }

std::string cmdDelete() { return "delete"; }

std::string cmdSet(const std::string& key, const std::string& value) { return "set " + quoteArg(key) + " " + quoteArg(value); }

std::string cmdRemove(const std::string& key) { return "remove " + quoteArg(key); }

std::string cmdSelectEntity(const std::string& classname, const std::string& targetname) {
    std::string out = "select class " + quoteArg(classname);
    if (!targetname.empty()) out += " targetname " + quoteArg(targetname);
    return out;
}

}  // namespace iw8r
