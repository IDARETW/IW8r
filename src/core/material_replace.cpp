#include "material_replace.h"

#include <cctype>
#include <set>

namespace iw8r {

namespace {

std::string lowerAscii(std::string s) {
    for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

bool isMaterialKey(const std::string& key) {
    return key == "iw8r_material" || key.rfind("iw8r_mat:", 0) == 0;
}

}  // namespace

MaterialReplaceResult planMaterialReplace(const MapFile& map, const std::string& from, const std::string& to) {
    MaterialReplaceResult r;
    if (from.empty() || to.empty() || from == to) return r;
    const std::string want = lowerAscii(from);

    for (const Entity& e : map.entities) {
        for (const EntityItem& item : e.items) {
            if (const KeyValueLine* kv = item.kv()) {
                const std::string key = kv->name();
                if (isMaterialKey(key) && lowerAscii(kv->value) == want) r.keys.push_back({e.id, key});
                continue;
            }
            const Primitive* p = item.prim();
            if (!p) continue;

            if (p->kind == PrimKind::Brush) {
                // setMaterial(to, i) on a copy changes only face i; faces with other materials are not touched.
                Primitive copy = *p;
                size_t n = 0;
                for (size_t i = 0; i < p->faces.size(); ++i) {
                    if (lowerAscii(p->faces[i].material()) != want) continue;
                    if (copy.setMaterial(to, int(i))) ++n;
                }
                if (n > 0) {
                    r.faces += n;
                    r.edits.push_back({e.id, item.id, std::move(copy)});
                }
            } else if (p->kind == PrimKind::Patch && lowerAscii(p->patchMaterial) == want) {
                Primitive copy = *p;
                if (copy.setMaterial(to)) {
                    ++r.patches;
                    r.edits.push_back({e.id, item.id, std::move(copy)});
                }
            }
        }
    }
    return r;
}

std::vector<std::string> materialsInMap(const MapFile& map) {
    std::set<std::string> found;
    auto add = [&](const std::string& m) {
        if (!m.empty()) found.insert(lowerAscii(m));
    };
    for (const Entity& e : map.entities) {
        for (const EntityItem& item : e.items) {
            if (const KeyValueLine* kv = item.kv()) {
                if (isMaterialKey(kv->name())) add(kv->value);
                continue;
            }
            const Primitive* p = item.prim();
            if (!p) continue;
            if (p->kind == PrimKind::Brush) {
                for (const BrushFace& f : p->faces) add(f.material());
            } else if (p->kind == PrimKind::Patch) {
                add(p->patchMaterial);
            }
        }
    }
    return std::vector<std::string>(found.begin(), found.end());
}

}  // namespace iw8r
