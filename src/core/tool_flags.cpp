// IW8 Radiant Suite - brush `toolFlags` lines (see tool_flags.h).
#include "tool_flags.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <utility>

namespace iw8r {
namespace {

std::string lowerCase(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string trimmed(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

// "toolFlags splitGeo;" -> "splitGeo". Keyword is case-insensitive, like the contents directive.
// Empty when the line is not a toolFlags directive or has no name.
std::string toolFlagName(const std::string& lineText) {
    const std::string t = trimmed(lineText);
    const std::string kw = "toolFlags";
    if (t.size() <= kw.size() || lowerCase(t.substr(0, kw.size())) != lowerCase(kw)) return {};
    if (!std::isspace(static_cast<unsigned char>(t[kw.size()]))) return {};
    std::string rest = trimmed(t.substr(kw.size()));
    if (!rest.empty() && rest.back() == ';') rest.pop_back();
    return trimmed(rest);
}

bool isLayerLine(const std::string& lineText) {
    const std::string t = trimmed(lineText);
    const std::string kw = "layer";
    if (t.size() <= kw.size() || lowerCase(t.substr(0, kw.size())) != kw) return false;
    return std::isspace(static_cast<unsigned char>(t[kw.size()])) || t[kw.size()] == '"';
}

// Name is written as a single directive word: no ';' and no line breaks, not empty.
bool writableName(const std::string& name) {
    return !name.empty() && name.find_first_of(";\r\n") == std::string::npos;
}

// Index of the first line after the opening brace and any `layer` lines that follow it; npos without a brace.
size_t insertionPoint(const std::vector<TextLine>& lines) {
    for (size_t i = 0; i < lines.size(); ++i) {
        if (trimmed(lines[i].text) != "{") continue;
        size_t at = i + 1;
        while (at < lines.size() && isLayerLine(lines[at].text)) ++at;
        return at;
    }
    return std::string::npos;
}

// Entity ids of the primitives of worldspawn and brush entities are looked up by SelRef.
const Primitive* primitiveOf(const MapFile& map, const SelRef& r) {
    for (const Entity& e : map.entities) {
        if (e.id != r.entity) continue;
        for (const EntityItem& it : e.items)
            if (it.id == r.item) return it.prim();
        return nullptr;
    }
    return nullptr;
}

}  // namespace

const std::vector<ToolFlagDef>& knownToolFlags() {
    static const std::vector<ToolFlagDef> defs = {
        {"splitGeo",
         "Split-geometry marker on stock brushes and patches; the editor keeps it, the compiler does not read it.",
         "docs/research/02_iwmap4_format.md, brush-level directives (926 in the Radiant corpus); "
         "samples/iw8/iw8_sample_room.map and tests/data/iw8_sample_room.map (toolFlags splitGeo;)"},
    };
    return defs;
}

std::vector<std::string> readToolFlags(const Primitive& brush) {
    std::vector<std::string> out;
    for (const TextLine& l : brush.lines) {
        std::string name = toolFlagName(l.text);
        if (!name.empty()) out.push_back(std::move(name));
    }
    return out;
}

std::optional<Primitive> withToolFlags(const Primitive& brush, const std::vector<std::string>& flags, const std::string& eol) {
    if (brush.kind != PrimKind::Brush) return std::nullopt;

    // Requested names: trimmed, first occurrence kept, unwritable names reject the whole request.
    std::vector<std::string> want;
    for (const std::string& f : flags) {
        const std::string n = trimmed(f);
        if (!writableName(n)) return std::nullopt;
        if (std::find(want.begin(), want.end(), n) == want.end()) want.push_back(n);
    }

    // Existing toolFlags lines.
    std::vector<size_t> old;
    std::vector<bool> isOld(brush.lines.size(), false);
    for (size_t i = 0; i < brush.lines.size(); ++i) {
        if (toolFlagName(brush.lines[i].text).empty()) continue;
        old.push_back(i);
        isOld[i] = true;
    }

    // Same names in the same order (no duplicates): nothing to rewrite.
    bool same = old.size() == want.size();
    for (size_t k = 0; same && k < old.size(); ++k) same = toolFlagName(brush.lines[old[k]].text) == want[k];
    if (same) return brush;

    size_t insertAt = old.empty() ? insertionPoint(brush.lines) : old.front();
    if (insertAt == std::string::npos) return std::nullopt;  // no body: not a brush text

    // The new block: a wanted name keeps its old line (text and terminator) when one exists.
    std::vector<bool> used(old.size(), false);
    std::vector<TextLine> block;
    for (const std::string& name : want) {
        TextLine line{" toolFlags " + name + ";", eol};
        for (size_t k = 0; k < old.size(); ++k) {
            if (used[k] || toolFlagName(brush.lines[old[k]].text) != name) continue;
            used[k] = true;
            line = brush.lines[old[k]];
            break;
        }
        block.push_back(std::move(line));
    }

    std::vector<TextLine> kept;
    kept.reserve(brush.lines.size() + block.size());
    for (size_t i = 0; i <= brush.lines.size(); ++i) {
        if (i == insertAt) kept.insert(kept.end(), block.begin(), block.end());
        if (i == brush.lines.size()) break;
        if (!isOld[i]) kept.push_back(brush.lines[i]);
    }

    Primitive out = brush;
    out.lines = std::move(kept);
    out.dirty = true;
    reparsePrimitive(out);
    return out;
}

bool setToolFlagOnSelection(Document& doc, const std::string& flag, bool on) {
    const std::string name = trimmed(flag);
    if (!writableName(name)) return false;

    std::vector<Document::PrimEdit> edits;
    for (const SelRef& r : doc.selection()) {
        if (r.item == kNoId) continue;  // point entity
        const Primitive* p = primitiveOf(doc.map(), r);
        if (!p || p->kind != PrimKind::Brush) continue;

        std::vector<std::string> flags = readToolFlags(*p);
        const bool has = std::find(flags.begin(), flags.end(), name) != flags.end();
        if (on == has) continue;
        if (on) {
            flags.push_back(name);
        } else {
            flags.erase(std::remove(flags.begin(), flags.end(), name), flags.end());
        }
        auto out = withToolFlags(*p, flags, doc.eol());
        if (out) edits.push_back({r.entity, r.item, std::move(*out)});
    }
    if (edits.empty()) return false;
    return doc.editPrimitives(edits, "Set Tool Flag " + name, false);
}

}  // namespace iw8r
