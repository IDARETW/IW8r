#include "layers.h"

namespace iw8r {

namespace {

std::vector<std::string> splitLines(const std::string& s) {
    std::vector<std::string> out;
    size_t start = 0;
    while (start < s.size()) {
        size_t nl = s.find('\n', start);
        if (nl == std::string::npos) nl = s.size();
        std::string line = s.substr(start, nl - start);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        out.push_back(line);
        start = nl + 1;
    }
    return out;
}

// `layer "NAME"` (leading whitespace allowed): returns NAME, or nullopt-like empty with ok=false.
bool parseLayerLine(const std::string& text, std::string& name) {
    const size_t a = text.find_first_not_of(" \t");
    if (a == std::string::npos || text.compare(a, 7, "layer \"") != 0) return false;
    const size_t q = text.find('"', a + 7);
    if (q == std::string::npos) return false;
    name = text.substr(a + 7, q - (a + 7));
    return true;
}

bool isOpenBrace(const std::string& text) {
    const size_t a = text.find_first_not_of(" \t");
    return a != std::string::npos && text[a] == '{' && text.find_first_not_of(" \t", a + 1) == std::string::npos;
}

}  // namespace

std::vector<LayerDecl> declaredLayers(const std::string& header) {
    std::vector<LayerDecl> out;
    for (const std::string& line : splitLines(header)) {
        if (line.size() < 2 || line[0] != '"') continue;
        const size_t q = line.find('"', 1);
        if (q == std::string::npos || line.compare(q + 1, 6, " flags") != 0) continue;
        out.push_back({line.substr(1, q - 1), line.substr(q + 7)});
    }
    return out;
}

bool isValidLayerName(const std::string& name) {
    return !name.empty() && name.find_first_of("\"\r\n") == std::string::npos;
}

std::string headerWithLayer(const std::string& header, const std::string& name, const std::string& eol) {
    if (!isValidLayerName(name)) return header;
    for (const auto& d : declaredLayers(header))
        if (d.name == name) return header;
    // Insert after the last declaration (or after the `iwmap 4` line), keeping every other byte.
    size_t insertAt = std::string::npos;
    size_t pos = 0;
    while (pos < header.size()) {
        size_t nl = header.find('\n', pos);
        const size_t end = nl == std::string::npos ? header.size() : nl + 1;
        const std::string line = header.substr(pos, end - pos);
        if ((!line.empty() && line[0] == '"' && line.find(" flags") != std::string::npos) || line.rfind("iwmap", 0) == 0) insertAt = end;
        pos = end;
    }
    if (insertAt == std::string::npos) insertAt = header.size();
    std::string out = header;
    // A header that does not end with a line break gets one before the new line.
    std::string prefix;
    if (insertAt > 0 && out[insertAt - 1] != '\n') prefix = eol;
    out.insert(insertAt, prefix + "\"" + name + "\" flags " + eol);
    return out;
}

std::string primitiveLayer(const Primitive& p) {
    std::string name;
    for (const auto& l : p.lines)
        if (parseLayerLine(l.text, name)) return name;
    return {};
}

bool setPrimitiveLayer(Primitive& p, const std::string& name, const std::string& eol) {
    if (!name.empty() && !isValidLayerName(name)) return false;
    std::string cur;
    for (size_t i = 0; i < p.lines.size(); ++i) {
        if (!parseLayerLine(p.lines[i].text, cur)) continue;
        if (cur == name) return false;
        if (name.empty()) p.lines.erase(p.lines.begin() + long(i));
        else p.lines[i].text = "layer \"" + name + "\"";
        p.dirty = true;
        reparsePrimitive(p);
        return true;
    }
    if (name.empty()) return false;
    // The directive is the first line after the opening `{` of the block that owns it: the brush's `{`,
    // or for a patch the inner `{` after its `curve` / `mesh` keyword (docs/research/02 §1).
    size_t open = std::string::npos;
    int braces = 0;
    for (size_t i = 0; i < p.lines.size(); ++i)
        if (isOpenBrace(p.lines[i].text)) {
            ++braces;
            open = i;
            if (p.kind == PrimKind::Brush || braces == 2) break;
        }
    if (open == std::string::npos) return false;
    p.lines.insert(p.lines.begin() + long(open + 1), TextLine{"layer \"" + name + "\"", eol});
    p.dirty = true;
    reparsePrimitive(p);
    return true;
}

}  // namespace iw8r
