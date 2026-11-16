// IW8 Radiant - lightmap fields, patch control-point locks and the Surface panel lightmap edit (see lightmap_fields.h).
#include "lightmap_fields.h"
#include <cmath>
#include <algorithm>

#include <cstdlib>
#include <map>
#include <utility>

namespace iw8r {
namespace {

// Suffix token indices (see the header comment).
constexpr size_t kLmMaterialTok = 7;
constexpr size_t kLmFirstTok = 8;  // L1
constexpr size_t kLmNumCount = 6;  // L1..L6
constexpr size_t kLmMinTokens = kLmFirstTok + kLmNumCount;  // 14

bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

struct Span {
    size_t begin, end;
};

// Byte ranges of the whitespace-separated tokens of `s`.
std::vector<Span> tokenSpans(const std::string& s) {
    std::vector<Span> out;
    size_t i = 0;
    while (i < s.size()) {
        while (i < s.size() && isSpace(s[i])) ++i;
        if (i >= s.size()) break;
        const size_t b = i;
        while (i < s.size() && !isSpace(s[i])) ++i;
        out.push_back({b, i});
    }
    return out;
}

bool parseNum(const std::string& s, double& out) {
    if (s.empty()) return false;
    char* end = nullptr;
    out = std::strtod(s.c_str(), &end);
    return end == s.c_str() + s.size();
}

// Reads the lightmap fields from a face suffix. nullopt when the suffix does not have the layout.
std::optional<LightmapParams> readSuffix(const std::string& suffix) {
    const auto sp = tokenSpans(suffix);
    if (sp.size() < kLmMinTokens) return std::nullopt;
    auto tok = [&](size_t k) { return suffix.substr(sp[k].begin, sp[k].end - sp[k].begin); };
    LightmapParams lp;
    lp.material = tok(kLmMaterialTok);
    double v[kLmNumCount] = {};
    double unused = 0;
    if (parseNum(lp.material, unused)) return std::nullopt;  // LMMAT is a name, a number means no lightmap fields
    for (size_t k = 0; k < kLmNumCount; ++k)
        if (!parseNum(tok(kLmFirstTok + k), v[k])) return std::nullopt;
    lp.sx = v[0];
    lp.sy = v[1];
    lp.ox = v[2];
    lp.oy = v[3];
    lp.rot = v[4];  // L6 (v[5]) is not part of LightmapParams and is never written
    return lp;
}

// Writes `want` into token `k` of `suffix` unless its current text already parses to the same value.
// The new text is appended to `reps` as (token index, text).
void planNumber(const std::string& suffix, const std::vector<Span>& sp, size_t k, double want,
                std::vector<std::pair<size_t, std::string>>& reps) {
    double cur = 0;
    const std::string text = suffix.substr(sp[k].begin, sp[k].end - sp[k].begin);
    if (parseNum(text, cur) && cur == want) return;
    reps.emplace_back(k, formatNumber(want));
}

}  // namespace

double lightmapDensityOfSuffix(const std::string& suffix) {
    const auto lp = readSuffix(suffix);
    if (!lp || !(lp->sx > 0) || !(lp->sy > 0)) return 1.0;
    return std::clamp(16384.0 / std::sqrt(lp->sx * lp->sy), 0.125, 8.0);
}

std::optional<LightmapParams> lightmapParamsOfFace(const Primitive& p, size_t face) {
    if (p.kind != PrimKind::Brush || face >= p.faces.size()) return std::nullopt;
    return readSuffix(p.faces[face].suffix);
}

std::optional<Primitive> withLightmapParams(const Primitive& p, size_t face, const LightmapParams& lp) {
    if (p.kind != PrimKind::Brush || lp.material.empty() || lp.material.find_first_of(" \t\r\n") != std::string::npos)
        return std::nullopt;
    if (face != kAllFaces && face >= p.faces.size()) return std::nullopt;

    Primitive out = p;
    bool any = false;      // some text changed
    bool hasFields = false;  // some targeted face has lightmap fields
    for (size_t i = 0; i < out.faces.size(); ++i) {
        if (face != kAllFaces && i != face) continue;
        BrushFace& f = out.faces[i];
        if (!readSuffix(f.suffix)) {
            if (face != kAllFaces) return std::nullopt;
            continue;
        }
        hasFields = true;
        const auto sp = tokenSpans(f.suffix);
        std::vector<std::pair<size_t, std::string>> reps;  // ascending token order
        const std::string curMat = f.suffix.substr(sp[kLmMaterialTok].begin, sp[kLmMaterialTok].end - sp[kLmMaterialTok].begin);
        if (curMat != lp.material) reps.emplace_back(kLmMaterialTok, lp.material);
        const double want[kLmNumCount - 1] = {lp.sx, lp.sy, lp.ox, lp.oy, lp.rot};  // L1..L5; L6 untouched
        for (size_t k = 0; k < kLmNumCount - 1; ++k) planNumber(f.suffix, sp, kLmFirstTok + k, want[k], reps);
        if (reps.empty()) continue;

        // Replace from the last token backwards so the earlier byte offsets stay valid.
        std::string s = f.suffix;
        for (auto it = reps.rbegin(); it != reps.rend(); ++it) {
            const Span& t = sp[it->first];
            s = s.substr(0, t.begin) + it->second + s.substr(t.end);
        }
        std::string& line = out.lines[f.line].text;
        const std::string head = line.substr(0, line.size() - f.suffix.size());
        f.suffix = s;
        line = head + s;
        any = true;
    }
    if (!hasFields) return std::nullopt;  // kAllFaces on a brush with no lightmap fields at all
    if (any) out.dirty = true;
    return out;
}

PatchGrid applyPatchEditWithLocks(const PatchGrid& before, const PatchGrid& after, const std::vector<bool>& locked) {
    if (before.rows != after.rows || before.cols != after.cols || before.pos.size() != after.pos.size()) return after;
    PatchGrid out = after;
    for (size_t i = 0; i < out.pos.size() && i < locked.size(); ++i)
        if (locked[i]) out.pos[i] = before.pos[i];
    return out;
}

std::vector<bool> lockBorder(const PatchGrid& g) {
    if (g.rows < 1 || g.cols < 1 || g.pos.size() != size_t(g.rows) * size_t(g.cols)) return {};
    std::vector<bool> out(g.pos.size(), false);
    for (int r = 0; r < g.rows; ++r)
        for (int c = 0; c < g.cols; ++c)
            out[size_t(r) * size_t(g.cols) + size_t(c)] = r == 0 || r == g.rows - 1 || c == 0 || c == g.cols - 1;
    return out;
}

std::vector<bool> lockCorners(const PatchGrid& g) {
    if (g.rows < 1 || g.cols < 1 || g.pos.size() != size_t(g.rows) * size_t(g.cols)) return {};
    std::vector<bool> out(g.pos.size(), false);
    for (int r = 0; r < g.rows; ++r)
        for (int c = 0; c < g.cols; ++c)
            out[size_t(r) * size_t(g.cols) + size_t(c)] = (r == 0 || r == g.rows - 1) && (c == 0 || c == g.cols - 1);
    return out;
}

bool setLightmapOnSelection(Document& doc, const LightmapParams& lp) {
    // Group the selected faces by their primitive (the face set is sorted by entity, item, face).
    std::map<std::pair<Id, Id>, std::vector<size_t>> byPrim;
    for (const FaceRef& f : doc.faceSelection()) byPrim[{f.entity, f.item}].push_back(f.face);

    std::vector<Document::PrimEdit> edits;
    for (const auto& [key, faces] : byPrim) {
        const EntityItem* item = doc.findItem(key.first, key.second);
        if (!item || !item->isPrimitive()) continue;
        const Primitive& orig = *item->prim();
        Primitive cur = orig;
        for (size_t fi : faces)
            if (auto next = withLightmapParams(cur, fi, lp)) cur = std::move(*next);
        if (cur.text() == orig.text()) continue;
        Document::PrimEdit e;
        e.entity = key.first;
        e.item = key.second;
        e.prim = std::move(cur);
        edits.push_back(std::move(e));
    }
    return doc.editPrimitives(edits, "Set Lightmap", false);
}

}  // namespace iw8r
