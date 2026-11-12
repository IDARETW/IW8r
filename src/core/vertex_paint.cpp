// IW8 Radiant Suite - per-vertex colour and alpha painting on patch control points. See vertex_paint.h.
#include "vertex_paint.h"
#include "heightmap.h"
#include "patch_ops.h"
#include "patch_tess.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <string>

namespace iw8r {

namespace {

constexpr size_t kNoLine = std::numeric_limits<size_t>::max();

bool isWs(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\v' || c == '\f'; }

// Whitespace-separated tokens with their byte offsets, split the same way as map_text.cpp's tokenizer, so a
// PatchVertex::token index refers to the same token here.
struct Span {
    size_t a = 0, b = 0;  // [a, b) in the line text
    std::string s;
};

std::vector<Span> spansOf(const std::string& t) {
    std::vector<Span> out;
    size_t i = 0;
    while (i < t.size()) {
        while (i < t.size() && isWs(t[i])) ++i;
        if (i >= t.size()) break;
        const size_t a = i;
        while (i < t.size() && !isWs(t[i])) ++i;
        out.push_back({a, i, t.substr(a, i - a)});
    }
    return out;
}

bool parseInt(const std::string& s, long& out) {
    char* end = nullptr;
    const long v = std::strtol(s.c_str(), &end, 10);
    if (end == s.c_str() || *end != '\0') return false;
    out = v;
    return true;
}

// Reads the "c r g b a" block that follows the position of the "v" token at index `v`. False when there is none or
// it is malformed (a value that is not an integer in 0..255).
bool readBlock(const std::vector<Span>& sp, size_t v, Rgba8& out) {
    const size_t c = v + 4;
    if (c + 4 >= sp.size() || sp[c].s != "c") return false;
    for (size_t k = 0; k < 4; ++k) {
        long val = 0;
        if (!parseInt(sp[c + 1 + k].s, val) || val < 0 || val > 255) return false;
        out[k] = static_cast<std::uint8_t>(val);
    }
    return true;
}

std::uint8_t byteOf(double v) {
    if (!std::isfinite(v)) return 0;
    const double r = std::clamp(std::round(v), 0.0, 255.0);
    return static_cast<std::uint8_t>(r);
}

}  // namespace

double brushWeight(const PaintBrush& b, double distance) {
    if (!std::isfinite(distance) || !std::isfinite(b.radius) || !(b.radius > 0.0)) return 0.0;
    if (!(distance < b.radius)) return 0.0;
    const double f = std::isfinite(b.falloff) ? std::clamp(b.falloff, 0.0, 1.0) : 0.0;
    const double inner = b.radius * (1.0 - f);
    if (distance <= inner) return 1.0;
    // inner < distance < radius, so the span is positive.
    const double t = (distance - inner) / (b.radius - inner);
    const double smooth = t * t * (3.0 - 2.0 * t);
    return 1.0 - smooth;
}

std::vector<Rgba8> readPatchColours(const Primitive& p) {
    std::vector<Rgba8> out;
    if (p.kind != PrimKind::Patch) return out;
    out.reserve(p.verts.size());
    size_t cachedLine = kNoLine;
    std::vector<Span> sp;
    for (const PatchVertex& pv : p.verts) {
        if (pv.line >= p.lines.size()) {
            out.push_back(kDefaultVertexRgba);
            continue;
        }
        if (pv.line != cachedLine) {
            sp = spansOf(p.lines[pv.line].text);
            cachedLine = pv.line;
        }
        Rgba8 c = kDefaultVertexRgba;
        readBlock(sp, pv.token, c);
        out.push_back(c);
    }
    return out;
}

bool hasPatchColour(const Primitive& p, size_t index) {
    if (p.kind != PrimKind::Patch || index >= p.verts.size()) return false;
    const PatchVertex& pv = p.verts[index];
    if (pv.line >= p.lines.size()) return false;
    Rgba8 c{};
    return readBlock(spansOf(p.lines[pv.line].text), pv.token, c);
}

bool setPatchVertexColour(Primitive& p, size_t index, const Rgba8& c) {
    if (p.kind != PrimKind::Patch || index >= p.verts.size()) return false;
    const PatchVertex pv = p.verts[index];
    if (pv.line >= p.lines.size()) return false;
    const std::string& t = p.lines[pv.line].text;
    const std::vector<Span> sp = spansOf(t);
    // The "v" token and its three position values must be there.
    if (pv.token + 3 >= sp.size() || sp[pv.token].s != "v") return false;

    const std::string values = std::to_string(c[0]) + " " + std::to_string(c[1]) + " " + std::to_string(c[2]) + " " +
                               std::to_string(c[3]);
    const size_t cIdx = pv.token + 4;
    std::string next;
    if (cIdx < sp.size() && sp[cIdx].s == "c") {
        // An existing block: overwrite its four values, keep the "c" and the spacing around them.
        if (cIdx + 4 >= sp.size()) return false;
        next = t.substr(0, sp[cIdx + 1].a) + values + t.substr(sp[cIdx + 4].b);
    } else {
        // No block: insert " c r g b a" right after the position.
        const size_t at = sp[pv.token + 3].b;
        next = t.substr(0, at) + " c " + values + t.substr(at);
    }
    p.lines[pv.line].text = next;
    p.dirty = true;
    // Token indices of the later control points on this line moved; the parsed views are rebuilt from the text.
    reparsePrimitive(p);
    return true;
}

int paintPatchVertices(Primitive& patch, const Vec3& center, const PaintBrush& b) {
    if (patch.kind != PrimKind::Patch || patch.verts.empty()) return 0;
    if (!std::isfinite(b.radius) || !(b.radius > 0.0)) return 0;
    if (!std::isfinite(b.strength) || !(b.strength > 0.0)) return 0;
    const double strength = std::min(b.strength, 1.0);
    const int mask = b.channelMask & kPaintAll;
    if (mask == 0) return 0;

    std::array<double, 4> target{};
    for (size_t k = 0; k < 4; ++k) {
        const double v = std::isfinite(b.rgba[k]) ? std::clamp(static_cast<double>(b.rgba[k]), 0.0, 1.0) : 0.0;
        target[k] = std::round(v * 255.0);
    }

    // Snapshot of the colours before any change; positions and vertex order do not change while painting.
    const std::vector<Rgba8> before = readPatchColours(patch);
    int changed = 0;
    for (size_t i = 0; i < patch.verts.size() && i < before.size(); ++i) {
        const double w = brushWeight(b, length(patch.verts[i].pos - center));
        if (w <= 0.0) continue;
        const double a = strength * w;
        Rgba8 next = before[i];
        for (size_t k = 0; k < 4; ++k) {
            if (!(mask & (1 << k))) continue;
            const double cur = before[i][k];
            next[k] = byteOf(cur + (target[k] - cur) * a);
        }
        if (next == before[i]) continue;
        if (setPatchVertexColour(patch, i, next)) ++changed;
    }
    return changed;
}

bool patchHasColours(const Primitive& p) {
    if (p.kind != PrimKind::Patch) return false;
    for (size_t i = 0; i < p.verts.size(); ++i)
        if (hasPatchColour(p, i)) return true;
    return false;
}

std::vector<Rgba8> tessellatePatchColours(const Primitive& p, int subdiv) {
    if (p.kind != PrimKind::Patch || p.verts.empty()) return {};
    int rows = 0, cols = 0;
    for (const auto& v : p.verts) {
        if (v.row < 0 || v.col < 0) return {};
        rows = std::max(rows, v.row + 1);
        cols = std::max(cols, v.col + 1);
    }
    if (size_t(rows) * size_t(cols) != p.verts.size()) return {};
    const std::vector<Rgba8> colours = readPatchColours(p);
    if (colours.size() != p.verts.size()) return {};
    // Two passes through the position tessellator: (r, g, b) and (a, 0, 0).
    std::vector<Vec3> rgb(p.verts.size()), alpha(p.verts.size());
    for (size_t i = 0; i < p.verts.size(); ++i) {
        const size_t idx = size_t(p.verts[i].row) * size_t(cols) + size_t(p.verts[i].col);
        rgb[idx] = Vec3{double(colours[i][0]), double(colours[i][1]), double(colours[i][2])};
        alpha[idx] = Vec3{double(colours[i][3]), 0, 0};
    }
    const PatchMesh a = tessellatePatch(rgb, rows, cols, p.patchKeyword, subdiv);
    const PatchMesh b = tessellatePatch(alpha, rows, cols, p.patchKeyword, subdiv);
    if (a.points.size() != b.points.size()) return {};
    std::vector<Rgba8> out(a.points.size());
    auto c8 = [](double x) { return std::uint8_t(std::clamp(std::lround(x), 0L, 255L)); };
    for (size_t i = 0; i < out.size(); ++i)
        out[i] = Rgba8{c8(a.points[i].x), c8(a.points[i].y), c8(a.points[i].z), c8(b.points[i].x)};
    return out;
}

int applySlopeBlendAlpha(Primitive& patch, double startDeg, double endDeg, bool invert) {
    if (patch.kind != PrimKind::Patch) return -1;
    const auto grid = patchGrid(patch);
    if (!grid) return -1;
    const std::vector<float> weights = slopeBlendAlpha(*grid, startDeg, endDeg);
    if (weights.size() != patch.verts.size()) return -1;
    const std::vector<Rgba8> colours = readPatchColours(patch);
    int changed = 0;
    for (size_t i = 0; i < weights.size(); ++i) {
        const double w = std::clamp(double(invert ? 1.0f - weights[i] : weights[i]), 0.0, 1.0);
        Rgba8 c = i < colours.size() ? colours[i] : kDefaultVertexRgba;
        const auto alpha = static_cast<std::uint8_t>(std::lround(w * 255.0));
        if (c[3] == alpha && hasPatchColour(patch, i)) continue;
        c[3] = alpha;
        if (setPatchVertexColour(patch, i, c)) ++changed;
    }
    return changed;
}

}  // namespace iw8r
