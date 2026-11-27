#include "auto_caulk.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <cmath>
#include <vector>

#include "brush_ops.h"

namespace iw8r {

namespace {

std::string lastPart(const std::string& m) {
    std::string s;
    for (char c : m) s += char(std::tolower(static_cast<unsigned char>(c)));
    const size_t slash = s.find_last_of("/\\");
    return slash == std::string::npos ? s : s.substr(slash + 1);
}

std::vector<std::string> nameTokens(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (std::isalnum(static_cast<unsigned char>(c))) {
            cur.push_back(c);
        } else if (!cur.empty()) {
            out.push_back(cur);
            cur.clear();
        }
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

// The name is the tool word or starts with it and a separator or digit ("clip_player", "water2"): stock surfaces such
// as "watertank_ladder_cover_01" are not tool materials.
bool startsWithWord(const std::string& s, const char* word) {
    const size_t n = std::strlen(word);
    if (s.rfind(word, 0) != 0) return false;
    return s.size() == n || s[n] == '_' || std::isdigit(static_cast<unsigned char>(s[n]));
}

bool isToolMaterial(const std::string& m) {
    const std::string s = lastPart(m);
    if (s.rfind("clip", 0) == 0) return true;
    for (const char* t : {"caulk", "nodraw", "clip", "trigger", "hint", "skip", "water", "sky", "ladder", "portal", "origin", "nav_blocker"})
        if (startsWithWord(s, t)) return true;
    return false;
}

bool isNonOccludingMaterial(const std::string& m) {
    const std::string s = lastPart(m);
    if (s.rfind("clip", 0) == 0) return true;
    if (s.find("glass") != std::string::npos) return true;
    for (const char* t : {"trigger", "clip", "hint", "skip", "water", "sky", "portal", "origin", "nav_blocker"})
        if (startsWithWord(s, t)) return true;
    for (const std::string& tok : nameTokens(s)) {
        for (const char* w : {"glass", "transparent", "translucent", "alpha", "alphatest", "blend", "blended", "decal",
                              "water", "trigger", "portal", "hint", "skip", "origin", "clip", "playerclip",
                              "weaponclip", "clipshot", "missileclip", "vehicleclip", "aiclip", "navblocker"}) {
            if (tok == w) return true;
        }
    }
    return false;
}

// A brush that hides what it touches: some face is a regular material or caulk/nodraw, and none is see-through.
bool occludes(const Primitive& p) {
    if (p.kind != PrimKind::Brush || p.faces.empty()) return false;
    for (const auto& f : p.faces)
        if (isNonOccludingMaterial(f.material())) return false;
    for (const auto& line : p.lines) {
        std::string low = line.text;
        for (char& c : low) c = char(std::tolower(static_cast<unsigned char>(c)));
        if (low.find("noncolliding") != std::string::npos) return false;
        if (low.find("contents water") != std::string::npos) return false;
        if (low.find("contents foliage") != std::string::npos) return false;
    }
    return true;
}

struct Face {
    Plane pl;
    Winding w;
    AABB box;
};

// Every vertex of `inner` lies inside the convex polygon `outer` (both on the plane with normal n), with tolerance.
bool contained(const Winding& inner, const Winding& outer, const Vec3& outerNormal) {
    if (outer.size() < 3) return false;
    for (const Vec3& p : inner)
        for (size_t k = 0; k < outer.size(); ++k) {
            const Vec3& a = outer[k];
            const Vec3& b = outer[(k + 1) % outer.size()];
            // Inside of an edge of a counter-clockwise polygon (seen along -n): cross(b - a, p - a) . n >= 0.
            if (dot(cross(b - a, p - a), outerNormal) < -0.05 * length(b - a)) return false;
        }
    return true;
}

}  // namespace

size_t autoCaulk(Document& doc, bool selectionOnly, const std::string& caulkMaterial) {
    struct BrushInfo {
        Id entity, item;
        const Primitive* prim;
        std::vector<Face> faces;
        AABB box;
        bool occluder;
        bool candidate;
    };
    std::vector<BrushInfo> brushes;
    for (const auto& e : doc.map().entities)
        for (const auto& it : e.items) {
            const Primitive* p = it.prim();
            if (!p || p->kind != PrimKind::Brush) continue;
            BrushInfo b{e.id, it.id, p, {}, {}, occludes(*p), !selectionOnly || doc.isSelected({e.id, it.id})};
            const auto wins = faceWindingsOf(*p);
            for (size_t i = 0; i < p->faces.size(); ++i) {
                Face f;
                if (const auto pl = facePlane(*p, i)) f.pl = *pl;
                if (i < wins.size()) f.w = wins[i];
                for (const Vec3& v : f.w) f.box.add(v), b.box.add(v);
                b.faces.push_back(std::move(f));
            }
            brushes.push_back(std::move(b));
        }
    std::vector<Document::PrimEdit> edits;
    size_t changed = 0;
    for (size_t bi = 0; bi < brushes.size(); ++bi) {
        const BrushInfo& b = brushes[bi];
        if (!b.candidate || !b.box.valid()) continue;
        Primitive out = *b.prim;
        size_t here = 0;
        for (size_t fi = 0; fi < b.faces.size(); ++fi) {
            const Face& f = b.faces[fi];
            if (f.w.size() < 3 || isToolMaterial(b.prim->faces[fi].material()) || isNonOccludingMaterial(b.prim->faces[fi].material()))
                continue;
            bool hidden = false;
            for (size_t oi = 0; oi < brushes.size() && !hidden; ++oi) {
                const BrushInfo& o = brushes[oi];
                if (oi == bi || !o.occluder || !o.box.valid()) continue;
                // Bounding boxes must touch.
                bool touch = true;
                for (int a = 0; a < 3; ++a) touch &= f.box.mins[a] <= o.box.maxs[a] + 0.1 && f.box.maxs[a] >= o.box.mins[a] - 0.1;
                if (!touch) continue;
                for (const Face& of : o.faces) {
                    if (of.w.size() < 3 || dot(of.pl.n, f.pl.n) > -0.9999 || std::fabs(of.pl.d + f.pl.d) > 0.05) continue;
                    if (contained(f.w, of.w, of.pl.n)) {
                        hidden = true;
                        break;
                    }
                }
            }
            if (hidden && out.setMaterial(caulkMaterial, int(fi))) ++here;
        }
        if (here) {
            changed += here;
            edits.push_back({b.entity, b.item, std::move(out)});
        }
    }
    if (!edits.empty()) doc.editPrimitives(edits, "Auto Caulk");
    return changed;
}

}  // namespace iw8r
