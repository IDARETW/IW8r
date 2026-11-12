#include "map_queries.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <set>

namespace iw8r {

namespace {

std::string lowerAscii(std::string s) {
    for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

bool matchMaterial(const std::string& mat, const std::string& find, bool substring) {
    if (find.empty()) return false;
    if (substring) return lowerAscii(mat).find(lowerAscii(find)) != std::string::npos;
    return lowerAscii(mat) == lowerAscii(find);
}

// Every occurrence of `find` (ASCII case-insensitive) replaced by `repl`.
std::string replaceAllCI(const std::string& s, const std::string& find, const std::string& repl) {
    const std::string ls = lowerAscii(s), lf = lowerAscii(find);
    std::string out;
    size_t pos = 0;
    while (true) {
        const size_t at = ls.find(lf, pos);
        if (at == std::string::npos) break;
        out += s.substr(pos, at - pos) + repl;
        pos = at + lf.size();
    }
    return out + s.substr(pos);
}

std::string mapMaterial(const std::string& mat, const std::string& find, const std::string& repl, bool substring) {
    if (!matchMaterial(mat, find, substring)) return mat;
    return substring ? replaceAllCI(mat, find, repl) : repl;
}

// Selection units of an entity: its primitives, or the entity itself when it has none.
std::vector<SelRef> refsOf(const Entity& e) {
    std::vector<SelRef> out;
    for (const auto& it : e.items)
        if (it.isPrimitive()) out.push_back({e.id, it.id});
    if (out.empty()) out.push_back({e.id, kNoId});
    return out;
}

std::string joinIndices(const std::vector<size_t>& v) {
    std::string s;
    for (size_t i = 0; i < v.size(); ++i) s += (i ? ", " : "") + std::to_string(v[i]);
    return s;
}

// Control points spanning at most one axis: their bounding box is flat (zero extent) on two or more axes.
bool patchDegenerate(const Primitive& p) {
    if (p.verts.empty()) return true;
    const AABB b = p.bounds();
    const Vec3 ext = b.maxs - b.mins;
    constexpr double eps = 1e-9;
    int flat = 0;
    for (int a = 0; a < 3; ++a) flat += ext[a] <= eps;
    return flat >= 2;
}

}  // namespace

std::vector<SelRef> findByMaterial(const MapFile& m, const std::string& material, bool substring) {
    std::vector<SelRef> out;
    if (material.empty()) return out;
    for (const Entity& e : m.entities)
        for (const auto& it : e.items) {
            const Primitive* p = it.prim();
            if (!p) continue;
            bool hit = false;
            if (p->kind == PrimKind::Brush) {
                for (const auto& f : p->faces) hit = hit || matchMaterial(f.material(), material, substring);
            } else if (p->kind == PrimKind::Patch) {
                hit = matchMaterial(p->patchMaterial, material, substring);
            }
            if (hit) out.push_back({e.id, it.id});
        }
    return out;
}

std::vector<SelRef> findByClass(const MapFile& m, const std::string& classname) {
    std::vector<SelRef> out;
    if (classname.empty()) return out;
    for (const Entity& e : m.entities)
        if (e.classname() == classname) {
            const auto refs = refsOf(e);
            out.insert(out.end(), refs.begin(), refs.end());
        }
    return out;
}

std::vector<SelRef> findByKeyValue(const MapFile& m, const std::string& key, const std::string& value, bool substring) {
    std::vector<SelRef> out;
    if (key.empty()) return out;
    for (const Entity& e : m.entities) {
        bool hit = false;
        for (const auto& it : e.items) {
            const KeyValueLine* kv = it.kv();
            if (!kv || kv->name() != key) continue;
            if (value.empty()) hit = true;
            else if (substring) hit = hit || kv->value.find(value) != std::string::npos;
            else hit = hit || kv->value == value;
        }
        if (hit) {
            const auto refs = refsOf(e);
            out.insert(out.end(), refs.begin(), refs.end());
        }
    }
    return out;
}

std::vector<MaterialUse> materialUsage(const MapFile& m) {
    std::map<std::string, MaterialUse> acc;
    for (const Entity& e : m.entities)
        for (const auto& it : e.items) {
            const Primitive* p = it.prim();
            if (!p) continue;
            if (p->kind == PrimKind::Brush) {
                std::set<std::string> seen;
                for (const auto& f : p->faces) {
                    const std::string mat = f.material();
                    if (mat.empty()) continue;
                    MaterialUse& u = acc[mat];
                    u.material = mat;
                    ++u.faces;
                    seen.insert(mat);
                }
                for (const auto& mat : seen) ++acc[mat].primitives;
            } else if (p->kind == PrimKind::Patch && !p->patchMaterial.empty()) {
                MaterialUse& u = acc[p->patchMaterial];
                u.material = p->patchMaterial;
                ++u.faces;
                ++u.primitives;
            }
        }
    std::vector<MaterialUse> out;
    out.reserve(acc.size());
    for (auto& [name, u] : acc) out.push_back(std::move(u));
    std::stable_sort(out.begin(), out.end(), [](const MaterialUse& a, const MaterialUse& b) {
        return a.faces != b.faces ? a.faces > b.faces : a.material < b.material;
    });
    return out;
}

size_t replaceMaterial(Document& doc, const std::string& find, const std::string& replace, bool substring, bool selectionOnly) {
    if (find.empty() || replace.empty() || replace.find_first_of(" \t\r\n") != std::string::npos) return 0;
    std::vector<Document::PrimEdit> edits;
    size_t faces = 0;
    for (const Entity& e : doc.map().entities)
        for (const auto& it : e.items) {
            const Primitive* p = it.prim();
            if (!p) continue;
            if (selectionOnly && !doc.isSelected({e.id, it.id})) continue;
            Primitive out = *p;
            size_t changed = 0;
            if (p->kind == PrimKind::Brush) {
                for (size_t i = 0; i < p->faces.size(); ++i) {
                    const std::string mat = p->faces[i].material();
                    const std::string nm = mapMaterial(mat, find, replace, substring);
                    if (nm != mat && out.setMaterial(nm, int(i))) ++changed;
                }
            } else if (p->kind == PrimKind::Patch) {
                const std::string nm = mapMaterial(p->patchMaterial, find, replace, substring);
                if (nm != p->patchMaterial && out.setMaterial(nm, -1)) changed = 1;
            }
            if (changed == 0) continue;
            faces += changed;
            edits.push_back({e.id, it.id, std::move(out)});
        }
    if (edits.empty()) return 0;
    doc.editPrimitives(edits, "Replace Material");
    return faces;
}

std::vector<std::string> mapProblems(const MapFile& m) {
    std::vector<std::string> out;

    // Targetnames: which entities define each name.
    std::map<std::string, std::vector<size_t>> targetnames;
    for (size_t i = 0; i < m.entities.size(); ++i)
        if (const std::string* t = m.entities[i].value("targetname"); t && !t->empty()) targetnames[*t].push_back(i);
    for (const auto& [name, ids] : targetnames)
        if (ids.size() > 1) out.push_back("duplicate targetname \"" + name + "\" on entities " + joinIndices(ids));

    for (size_t i = 0; i < m.entities.size(); ++i) {
        const Entity& e = m.entities[i];
        const std::string cls = e.classname();
        const std::string where = "entity " + std::to_string(i) + (cls.empty() ? "" : " (" + cls + ")");
        const std::string* cn = e.value("classname");
        if (!cn || cn->empty()) out.push_back(where + " has no classname");

        size_t brushIndex = 0;
        bool hasPrim = false;
        for (const auto& it : e.items) {
            if (const KeyValueLine* kv = it.kv(); kv && kv->name() == "target" && !kv->value.empty() &&
                                                  !targetnames.count(kv->value))
                out.push_back(where + " targets \"" + kv->value + "\" but no entity has that targetname");
            const Primitive* p = it.prim();
            if (!p) continue;
            hasPrim = true;
            const size_t k = brushIndex++;
            if (p->kind == PrimKind::Brush && p->planes().size() < 4)
                out.push_back(where + " brush " + std::to_string(k) + " has " + std::to_string(p->planes().size()) +
                              " valid planes (needs 4)");
            else if (p->kind == PrimKind::Patch && patchDegenerate(*p))
                out.push_back(where + " patch " + std::to_string(k) + " is degenerate");
        }
        if (!hasPrim && !e.isWorldspawn() && !e.origin()) out.push_back(where + " is a point entity without a valid origin");
    }
    return out;
}

}  // namespace iw8r
