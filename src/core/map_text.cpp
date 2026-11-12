#include "map_text.h"

#include <charconv>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <unordered_map>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace iw8r {

namespace {

bool isWs(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\v' || c == '\f'; }

std::string_view trim(std::string_view s) {
    size_t a = 0, b = s.size();
    while (a < b && isWs(s[a])) ++a;
    while (b > a && isWs(s[b - 1])) --b;
    return s.substr(a, b - a);
}

std::vector<TextLine> splitLines(const std::string& s) {
    std::vector<TextLine> out;
    size_t i = 0;
    while (i < s.size()) {
        const size_t nl = s.find('\n', i);
        if (nl == std::string::npos) {
            out.push_back({s.substr(i), ""});
            break;
        }
        size_t end = nl;
        std::string eol = "\n";
        if (end > i && s[end - 1] == '\r') {
            --end;
            eol = "\r\n";
        }
        out.push_back({s.substr(i, end - i), eol});
        i = nl + 1;
    }
    return out;
}

// Net brace depth change of a line, ignoring quoted text and // comments.
int braceDelta(std::string_view t) {
    int d = 0;
    bool inQuote = false;
    for (size_t i = 0; i < t.size(); ++i) {
        const char c = t[i];
        if (c == '"') inQuote = !inQuote;
        if (inQuote) continue;
        if (c == '/' && i + 1 < t.size() && t[i + 1] == '/') break;
        if (c == '{') ++d;
        else if (c == '}') --d;
    }
    return d;
}

bool parseNumber(std::string_view tok, double& out) {
    if (tok.empty()) return false;
    const char* first = tok.data();
    const char* last = tok.data() + tok.size();
    if (*first == '+') ++first;
    auto r = std::from_chars(first, last, out);
    return r.ec == std::errc() && r.ptr == last;
}

struct Tokens {
    std::vector<std::string> ws;   // ws[i] precedes tok[i]; ws.back() is trailing whitespace
    std::vector<std::string> tok;
    std::string join() const {
        std::string s;
        for (size_t i = 0; i < tok.size(); ++i) s += ws[i] + tok[i];
        s += ws.back();
        return s;
    }
};

Tokens tokenize(const std::string& t) {
    Tokens r;
    size_t i = 0;
    while (true) {
        size_t a = i;
        while (i < t.size() && isWs(t[i])) ++i;
        r.ws.push_back(t.substr(a, i - a));
        if (i >= t.size()) break;
        a = i;
        while (i < t.size() && !isWs(t[i])) ++i;
        r.tok.push_back(t.substr(a, i - a));
    }
    return r;
}

bool parseFaceLine(const std::string& t, BrushFace& f) {
    size_t i = 0;
    auto skipWs = [&] { while (i < t.size() && isWs(t[i])) ++i; };
    skipWs();
    f.indent = t.substr(0, i);
    for (int k = 0; k < 3; ++k) {
        skipWs();
        if (i >= t.size() || t[i] != '(') return false;
        ++i;
        for (int a = 0; a < 3; ++a) {
            skipWs();
            const size_t s = i;
            while (i < t.size() && !isWs(t[i]) && t[i] != ')') ++i;
            double v = 0;
            if (!parseNumber(std::string_view(t).substr(s, i - s), v)) return false;
            f.p[k][a] = v;
        }
        skipWs();
        if (i >= t.size() || t[i] != ')') return false;
        ++i;
    }
    f.suffix = t.substr(i);
    return true;
}

std::string faceLineText(const BrushFace& f) {
    std::string s = f.indent;
    for (int k = 0; k < 3; ++k) {
        if (k) s += ' ';
        s += "( " + formatNumber(f.p[k].x) + " " + formatNumber(f.p[k].y) + " " + formatNumber(f.p[k].z) + " )";
    }
    s += f.suffix;
    return s;
}

bool parseKeyValueLine(const std::string& t, KeyValueLine& kv) {
    size_t i = 0;
    while (i < t.size() && isWs(t[i])) ++i;
    if (i >= t.size()) return false;
    auto skipWs = [&](size_t j) { while (j < t.size() && isWs(t[j])) ++j; return j; };
    size_t valOpen = std::string::npos;
    if (t[i] == '"') {
        const size_t keyEnd = t.find('"', i + 1);
        if (keyEnd == std::string::npos) return false;
        kv.form = KeyForm::Quoted;
        kv.key = t.substr(i + 1, keyEnd - i - 1);
        valOpen = skipWs(keyEnd + 1);
    } else if (t[i] >= '0' && t[i] <= '9') {
        size_t j = i;
        while (j < t.size() && t[j] >= '0' && t[j] <= '9') ++j;
        if (j == i || j >= t.size() || !isWs(t[j])) return false;
        const std::string digits = t.substr(i, j - i);
        size_t k = skipWs(j);
        if (digits == "0" && k < t.size()) {
            // `0 name "value"` or `0 "name" "value"`: a non-canonical script string key.
            size_t nameStart = k, nameEnd;
            if (t[k] == '"') {
                nameEnd = t.find('"', k + 1);
                if (nameEnd == std::string::npos) return false;
                kv.key = t.substr(k + 1, nameEnd - k - 1);
                ++nameEnd;
            } else {
                nameEnd = k;
                while (nameEnd < t.size() && !isWs(t[nameEnd])) ++nameEnd;
                kv.key = t.substr(nameStart, nameEnd - nameStart);
            }
            const size_t v = skipWs(nameEnd);
            if (v < t.size() && t[v] == '"' && t.rfind('"') > v) {
                kv.form = KeyForm::ScriptString;
                valOpen = v;
            }
        }
        if (valOpen == std::string::npos) {
            kv.form = KeyForm::Canonical;
            kv.key = digits;
            // Stop as soon as the id leaves uint32_t range: checking only at the end lets 20+ digits wrap
            // (e.g. 2^64 became 0). Such a line is not a canonical key, so it stays an opaque line.
            uint64_t id = 0;
            for (char c : digits) {
                id = id * 10 + uint64_t(c - '0');
                if (id > 0xFFFFFFFFull) return false;
            }
            kv.keyId = uint32_t(id);
            valOpen = k;
        }
    } else {
        return false;
    }
    if (valOpen >= t.size() || t[valOpen] != '"') return false;
    const size_t valEnd = t.rfind('"');
    if (valEnd == std::string::npos || valEnd <= valOpen) return false;
    for (size_t k = valEnd + 1; k < t.size(); ++k)
        if (!isWs(t[k])) return false;
    kv.value = t.substr(valOpen + 1, valEnd - valOpen - 1);
    kv.prefix = t.substr(0, valOpen);
    return true;
}
bool isCommentOrBlank(std::string_view t) {
    const auto s = trim(t);
    return s.empty() || (s.size() >= 2 && s[0] == '/' && s[1] == '/');
}

}  // namespace

std::vector<std::string> splitTokens(const std::string& line) { return tokenize(line).tok; }

std::string formatNumber(double v) {
    if (v == 0) return "0";
    char buf[512];
    auto r = std::to_chars(buf, buf + sizeof(buf), v, std::chars_format::fixed);
    if (r.ec != std::errc()) {
        std::snprintf(buf, sizeof(buf), "%.17g", v);
        return buf;
    }
    std::string s(buf, r.ptr);
    // Regenerated coordinates carry at most 6 decimals (Radiant's own precision), so binary
    // noise from adding a grid delta (e.g. 16.100000000000001) never reaches the file.
    const size_t dot = s.find('.');
    if (dot != std::string::npos && s.size() - dot - 1 > 6) {
        r = std::to_chars(buf, buf + sizeof(buf), v, std::chars_format::fixed, 6);
        s.assign(buf, r.ptr);
        while (!s.empty() && s.back() == '0') s.pop_back();
        if (!s.empty() && s.back() == '.') s.pop_back();
        if (s == "-0") s = "0";
    }
    return s;
}

std::string formatFloat32(float v) {
    if (v == 0) return "0";
    char buf[128];
    auto r = std::to_chars(buf, buf + sizeof(buf), v, std::chars_format::fixed);
    return r.ec == std::errc() ? std::string(buf, r.ptr) : formatNumber(v);
}

std::string BrushFace::material() const {
    size_t i = 0;
    while (i < suffix.size() && isWs(suffix[i])) ++i;
    size_t j = i;
    while (j < suffix.size() && !isWs(suffix[j])) ++j;
    return suffix.substr(i, j - i);
}

std::string Primitive::text() const {
    std::string s;
    for (const auto& l : lines) s += l.text + l.eol;
    return s;
}

void reparsePrimitive(Primitive& prim) {
    prim.faces.clear();
    prim.verts.clear();
    prim.patchKeyword.clear();
    prim.patchMaterial.clear();
    prim.kind = PrimKind::Unknown;

    // Relative depth before each line: the primitive's own "{" takes 0 -> 1.
    int depth = 0;
    int patchDepth = -1;  // depth of the patch sub-block body
    int row = -1, col = 0;
    for (size_t li = 0; li < prim.lines.size(); ++li) {
        const std::string& t = prim.lines[li].text;
        const auto tt = trim(t);
        const int delta = braceDelta(t);
        const int before = depth;
        depth += delta;
        if (before == 1 && delta == 0 && !tt.empty() && tt[0] == '(') {
            BrushFace f;
            if (parseFaceLine(t, f)) {
                f.line = li;
                prim.faces.push_back(std::move(f));
            }
            continue;
        }
        if (before == 1 && delta == 0 && prim.patchKeyword.empty() && !tt.empty() &&
            tt.find_first_of(" \t;(\"") == std::string_view::npos && li + 1 < prim.lines.size() &&
            trim(prim.lines[li + 1].text) == "{") {
            prim.patchKeyword = std::string(tt);
            patchDepth = 2;
            continue;
        }
        if (patchDepth > 0 && before == patchDepth && delta == 0) {
            if (tt == "(") { ++row; col = 0; continue; }
            if (tt == ")") continue;
            if (prim.patchMaterial.empty() && !tt.empty() && tt.find_first_of(" \t;") == std::string_view::npos) {
                prim.patchMaterial = std::string(tt);
                continue;
            }
            const Tokens tk = tokenize(t);
            for (size_t k = 0; k + 3 < tk.tok.size(); ++k) {
                if (tk.tok[k] != "v") continue;
                PatchVertex pv;
                double x, y, z;
                if (parseNumber(tk.tok[k + 1], x) && parseNumber(tk.tok[k + 2], y) && parseNumber(tk.tok[k + 3], z)) {
                    pv.line = li;
                    pv.token = k;
                    pv.row = row < 0 ? 0 : row;
                    pv.col = col++;
                    pv.pos = {x, y, z};
                    prim.verts.push_back(pv);
                    k += 3;
                }
            }
        }
    }
    if (!prim.patchKeyword.empty()) prim.kind = PrimKind::Patch;
    else if (!prim.faces.empty()) prim.kind = PrimKind::Brush;
}

void Primitive::translate(const Vec3& delta) {
    if (delta == Vec3{}) return;
    if (kind == PrimKind::Brush) {
        for (auto& f : faces) {
            for (auto& p : f.p) p += delta;
            lines[f.line].text = faceLineText(f);
        }
        dirty = true;
    } else if (kind == PrimKind::Patch) {
        std::map<size_t, Tokens> touched;
        for (auto& v : verts) {
            v.pos += delta;
            auto it = touched.find(v.line);
            if (it == touched.end()) it = touched.emplace(v.line, tokenize(lines[v.line].text)).first;
            it->second.tok[v.token + 1] = formatNumber(v.pos.x);
            it->second.tok[v.token + 2] = formatNumber(v.pos.y);
            it->second.tok[v.token + 3] = formatNumber(v.pos.z);
        }
        for (auto& [li, tk] : touched) lines[li].text = tk.join();
        dirty = true;
    }
}

bool Primitive::setMaterial(const std::string& material, int faceIndex) {
    if (material.empty() || material.find_first_of(" \t\r\n") != std::string::npos) return false;
    bool changed = false;
    if (kind == PrimKind::Brush) {
        for (size_t i = 0; i < faces.size(); ++i) {
            if (faceIndex >= 0 && size_t(faceIndex) != i) continue;
            BrushFace& f = faces[i];
            size_t a = 0;
            while (a < f.suffix.size() && isWs(f.suffix[a])) ++a;
            size_t b = a;
            while (b < f.suffix.size() && !isWs(f.suffix[b])) ++b;
            if (f.suffix.compare(a, b - a, material) == 0) continue;
            // Keep the original point text; only the material token changes.
            std::string& text = lines[f.line].text;
            const std::string head = text.substr(0, text.size() - f.suffix.size());
            f.suffix = f.suffix.substr(0, a) + material + f.suffix.substr(b);
            text = head + f.suffix;
            changed = true;
        }
    } else if (kind == PrimKind::Patch && !patchMaterial.empty() && patchMaterial != material) {
        for (auto& l : lines) {
            const auto tt = trim(l.text);
            if (tt == patchMaterial) {
                const size_t at = l.text.find(patchMaterial);
                l.text = l.text.substr(0, at) + material + l.text.substr(at + patchMaterial.size());
                patchMaterial = material;
                changed = true;
                break;
            }
        }
    }
    if (changed) dirty = true;
    return changed;
}

bool Primitive::setTextureFields(const TexFields& tf, int faceIndex) {
    if (kind != PrimKind::Brush) return false;
    bool changed = false;
    for (size_t i = 0; i < faces.size(); ++i) {
        if (faceIndex >= 0 && size_t(faceIndex) != i) continue;
        BrushFace& f = faces[i];
        Tokens tk = tokenize(f.suffix);
        if (tk.tok.size() < 6) continue;  // material + 5 numbers
        const std::optional<double>* want[5] = {&tf.width, &tf.height, &tf.xoff, &tf.yoff, &tf.rot};
        bool faceChanged = false;
        for (int k = 0; k < 5; ++k) {
            if (!*want[k]) continue;
            const std::string text = formatNumber(**want[k]);
            if (tk.tok[size_t(k + 1)] != text) {
                tk.tok[size_t(k + 1)] = text;
                faceChanged = true;
            }
        }
        if (!faceChanged) continue;
        std::string& line = lines[f.line].text;
        const std::string head = line.substr(0, line.size() - f.suffix.size());
        f.suffix = tk.join();
        line = head + f.suffix;
        changed = true;
    }
    if (changed) dirty = true;
    return changed;
}

std::optional<std::array<double, 5>> Primitive::textureFields(size_t faceIndex) const {
    if (faceIndex >= faces.size()) return std::nullopt;
    const Tokens tk = tokenize(faces[faceIndex].suffix);
    if (tk.tok.size() < 6) return std::nullopt;
    std::array<double, 5> out{};
    for (int k = 0; k < 5; ++k)
        if (!parseNumber(tk.tok[size_t(k + 1)], out[size_t(k)])) return std::nullopt;
    return out;
}

std::vector<Plane> Primitive::planes() const {
    std::vector<Plane> out;
    out.reserve(faces.size());
    for (const auto& f : faces)
        if (auto pl = planeFromPoints(f.p[0], f.p[1], f.p[2])) out.push_back(*pl);
    return out;
}

AABB Primitive::bounds() const {
    AABB b;
    if (kind == PrimKind::Brush) {
        for (const auto& w : buildBrushWindings(planes()))
            for (const auto& p : w) b.add(p);
        if (!b.valid())
            for (const auto& f : faces)
                for (const auto& p : f.p) b.add(p);
    } else if (kind == PrimKind::Patch) {
        for (const auto& v : verts) b.add(v.pos);
    }
    return b;
}

void KeyValueLine::set(const std::string& newValue) {
    value = newValue;
    line.text = prefix + "\"" + value + "\"";
    dirty = true;
}

std::string KeyValueLine::name() const {
    if (form == KeyForm::Canonical)
        if (const std::string* n = KeyTable::global().name(keyId)) return *n;
    return key;
}

KeyValueLine makeKeyValue(const std::string& key, const std::string& value, const std::string& eol, bool canonical) {
    KeyValueLine kv;
    kv.value = value;
    std::optional<uint32_t> id = canonical ? KeyTable::global().id(key) : std::nullopt;
    if (id) {
        kv.form = KeyForm::Canonical;
        kv.keyId = *id;
        kv.key = std::to_string(*id);
        kv.prefix = kv.key + " ";
    } else {
        kv.form = KeyForm::Quoted;
        kv.key = key;
        kv.prefix = "\"" + key + "\" ";
    }
    kv.line = {kv.prefix + "\"" + value + "\"", eol};
    kv.dirty = true;
    return kv;
}

// ---- IW8 canonical key table

struct KeyTable::Impl {
    std::unordered_map<uint32_t, std::string> byId;
    std::unordered_map<std::string, uint32_t> byName;
    void add(uint32_t id, const std::string& n) {
        byId[id] = n;
        byName.emplace(n, id);
    }
};

KeyTable::KeyTable() : impl_(new Impl) {
    // Replay 1.20 canonical ids the editor needs even without the generated table.
    // Evidence: gsc-tool src/gsc/engine/iw8_token.cpp (0x50, 0xD4, 0x28C, 0x2C5, 0x34D, 0x42E, 0x430)
    // and x64-zt src/games/iw8/zone/replay_spawns.h (fields 212, 709, 80, 1070, 845), both matching
    // the stock mp_hideout MapEnts entity string.
    impl_->add(0x50, "angles");
    impl_->add(0xD4, "classname");
    impl_->add(0x28C, "model");
    impl_->add(0x2C5, "origin");
    impl_->add(0x34D, "script_noteworthy");
    impl_->add(0x42E, "target");
    impl_->add(0x430, "targetname");
}

KeyTable& KeyTable::global() {
    static KeyTable t;
    return t;
}

bool KeyTable::loadTsv(const std::string& path, std::string* err) {
    std::string text;
    if (!readFile(path, text)) {
        if (err) *err = "cannot read " + path;
        return false;
    }
    size_t n = 0;
    for (const auto& l : splitLines(text)) {
        if (l.text.empty() || l.text[0] == '#') continue;
        const size_t tab = l.text.find('\t');
        if (tab == std::string::npos) continue;
        const std::string idText = l.text.substr(0, tab);
        const std::string name = l.text.substr(tab + 1);
        char* end = nullptr;
        const unsigned long id = std::strtoul(idText.c_str(), &end, 0);
        if (!end || *end || name.empty()) continue;
        impl_->add(uint32_t(id), name);
        ++n;
    }
    if (n == 0) {
        if (err) *err = "no id<TAB>name rows in " + path;
        return false;
    }
    return true;
}

const std::string* KeyTable::name(uint32_t id) const {
    auto it = impl_->byId.find(id);
    return it == impl_->byId.end() ? nullptr : &it->second;
}

std::optional<uint32_t> KeyTable::id(const std::string& name) const {
    auto it = impl_->byName.find(name);
    if (it == impl_->byName.end()) return std::nullopt;
    return it->second;
}

size_t KeyTable::size() const { return impl_->byId.size(); }
std::string EntityItem::text() const {
    if (auto* p = std::get_if<Primitive>(&v)) return p->text();
    if (auto* k = std::get_if<KeyValueLine>(&v)) return k->line.text + k->line.eol;
    const auto& o = std::get<OpaqueLine>(v);
    return o.line.text + o.line.eol;
}

const std::string* Entity::value(const std::string& key) const {
    for (const auto& it : items)
        if (const auto* kv = it.kv(); kv && kv->name() == key) return &kv->value;
    return nullptr;
}

bool Entity::usesCanonicalKeys() const {
    for (const auto& it : items)
        if (const auto* kv = it.kv(); kv && kv->form == KeyForm::Canonical) return true;
    return false;
}

std::string Entity::classname() const {
    const std::string* v = value("classname");
    return v ? *v : std::string();
}

std::optional<Vec3> Entity::origin() const {
    const std::string* v = value("origin");
    if (!v) return std::nullopt;
    const Tokens tk = tokenize(*v);
    if (tk.tok.size() < 3) return std::nullopt;
    Vec3 o;
    for (int a = 0; a < 3; ++a)
        if (!parseNumber(tk.tok[a], o[a])) return std::nullopt;
    return o;
}

std::string Entity::text() const {
    std::string s = prefix + open.text + open.eol;
    for (const auto& it : items) s += it.text();
    s += close.text + close.eol;
    return s;
}

std::string MapFile::text() const {
    std::string s = header;
    for (const auto& e : entities) s += e.text();
    s += trailer;
    return s;
}

ParseResult parseMap(const std::string& text, Id& nextId) {
    ParseResult res;
    MapFile map;
    const auto lines = splitLines(text);

    size_t crlf = 0, lf = 0;
    for (const auto& l : lines) {
        if (l.eol == "\r\n") ++crlf;
        else if (l.eol == "\n") ++lf;
    }
    map.eol = (lf > crlf) ? "\n" : "\r\n";

    auto fail = [&](size_t li, const std::string& msg) {
        res.error = ParseError{li + 1, msg};
        return res;
    };

    std::vector<TextLine> pending;  // depth-0 lines not yet owned
    auto joinLines = [](const std::vector<TextLine>& v, size_t from, size_t to) {
        std::string s;
        for (size_t i = from; i < to; ++i) s += v[i].text + v[i].eol;
        return s;
    };

    size_t li = 0;
    while (li < lines.size()) {
        const TextLine& line = lines[li];
        const int delta = braceDelta(line.text);
        if (delta <= 0) {
            if (delta < 0) return fail(li, "unexpected '}' at top level");
            pending.push_back(line);
            ++li;
            continue;
        }
        if (delta != 1 || trim(line.text) != "{") return fail(li, "expected '{' on its own line to open an entity");

        Entity ent;
        ent.id = nextId++;
        if (map.entities.empty()) {
            // Header keeps everything except the trailing comment run ("// entity 0").
            size_t split = pending.size();
            while (split > 0 && isCommentOrBlank(pending[split - 1].text) && !trim(pending[split - 1].text).empty())
                --split;
            map.header = joinLines(pending, 0, split);
            ent.prefix = joinLines(pending, split, pending.size());
        } else {
            ent.prefix = joinLines(pending, 0, pending.size());
        }
        pending.clear();
        ent.open = line;
        ++li;

        std::vector<TextLine> lead;  // comments/blank lines inside the entity awaiting an owner
        auto flushLead = [&] {
            for (auto& l : lead) {
                EntityItem it;
                it.id = nextId++;
                it.v = OpaqueLine{l};
                ent.items.push_back(std::move(it));
            }
            lead.clear();
        };

        bool closed = false;
        while (li < lines.size()) {
            const TextLine& l = lines[li];
            const auto tt = trim(l.text);
            const int d = braceDelta(l.text);
            if (d < 0) {
                if (d != -1 || tt != "}") return fail(li, "malformed entity close");
                flushLead();
                ent.close = l;
                ++li;
                closed = true;
                break;
            }
            if (d > 0) {
                if (d != 1 || tt != "{") return fail(li, "expected '{' on its own line to open a brush or patch");
                Primitive prim;
                prim.id = nextId;  // item id doubles as primitive id
                prim.lines = std::move(lead);
                lead.clear();
                prim.lines.push_back(l);
                int depth = 1;
                ++li;
                while (li < lines.size() && depth > 0) {
                    depth += braceDelta(lines[li].text);
                    prim.lines.push_back(lines[li]);
                    ++li;
                }
                if (depth != 0) return fail(li - 1, "unterminated brush or patch");
                reparsePrimitive(prim);
                EntityItem it;
                it.id = nextId++;
                it.v = std::move(prim);
                ent.items.push_back(std::move(it));
                continue;
            }
            if (!tt.empty() && (tt[0] == '"' || (tt[0] >= '0' && tt[0] <= '9'))) {
                KeyValueLine kv;
                kv.line = l;
                flushLead();
                EntityItem it;
                it.id = nextId++;
                if (parseKeyValueLine(l.text, kv)) it.v = std::move(kv);
                else it.v = OpaqueLine{l};
                ent.items.push_back(std::move(it));
                ++li;
                continue;
            }
            if (isCommentOrBlank(l.text)) {
                lead.push_back(l);
                ++li;
                continue;
            }
            flushLead();
            EntityItem it;
            it.id = nextId++;
            it.v = OpaqueLine{l};
            ent.items.push_back(std::move(it));
            ++li;
        }
        if (!closed) return fail(lines.empty() ? 0 : lines.size() - 1, "unterminated entity");
        map.entities.push_back(std::move(ent));
    }

    if (map.entities.empty()) map.header = joinLines(pending, 0, pending.size());
    else map.trailer = joinLines(pending, 0, pending.size());

    // Self check: the partition must reproduce the input exactly.
    if (map.text() != text) return fail(0, "internal error: lossless partition check failed");
    res.map = std::move(map);
    return res;
}

#ifdef _WIN32
namespace {
std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
    return w;
}
}  // namespace
#endif

bool readFile(const std::string& path, std::string& out) {
#ifdef _WIN32
    std::ifstream f(widen(path), std::ios::binary);
#else
    std::ifstream f(path, std::ios::binary);
#endif
    if (!f) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

bool writeFileAtomic(const std::string& path, const std::string& data, std::string* err) {
    const std::string tmp = path + ".iw8r-tmp";
    {
#ifdef _WIN32
        std::ofstream f(widen(tmp), std::ios::binary | std::ios::trunc);
#else
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
#endif
        if (!f) {
            if (err) *err = "cannot create " + tmp;
            return false;
        }
        f.write(data.data(), (std::streamsize)data.size());
        f.flush();
        if (!f) {
            if (err) *err = "write failed for " + tmp;
            return false;
        }
    }
#ifdef _WIN32
    if (!MoveFileExW(widen(tmp).c_str(), widen(path).c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        if (err) *err = "cannot replace " + path + " (error " + std::to_string(GetLastError()) + ")";
        DeleteFileW(widen(tmp).c_str());
        return false;
    }
#else
    if (std::rename(tmp.c_str(), path.c_str()) != 0) {
        if (err) *err = "cannot replace " + path;
        return false;
    }
#endif
    return true;
}

}  // namespace iw8r
