import io


def patch(p, pairs):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    for a, b in pairs:
        a = a.replace('\n', nl); b = b.replace('\n', nl)
        assert s.count(a) == 1, (p, s.count(a), a[:70])
        s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


patch('src/core/stock_loot.h', [
 ('''inline constexpr const char* kLootPointClass = "iw8r_loot_point";
''', '''inline constexpr const char* kLootPointClass = "iw8r_loot_point";
// New loot boxes (research 89 section 9): brush entities whose brushes give the box. A cluster groups the points
// inside it; a zone (key "name") is what the loot tables pick the loot by. They are added after the map's own boxes.
inline constexpr const char* kLootClusterClass = "iw8r_loot_cluster";
inline constexpr const char* kLootZoneClass = "iw8r_loot_zone";
'''),
 ('''bool isLootPointClass(const std::string& classname);
''', '''bool isLootPointClass(const std::string& classname);
bool isLootBoxClass(const std::string& classname);  // iw8r_loot_cluster or iw8r_loot_zone

// A new loot box of the document: the bounds of its entity's brushes (and patches); `name` for a zone.
struct LootBoxEdit {
    Vec3 mid, half;
    std::string name;
    bool operator==(const LootBoxEdit& o) const { return mid == o.mid && half == o.half && name == o.name; }
};
struct LootBoxEdits {
    std::vector<LootBoxEdit> clusters, zones;
    bool empty() const { return clusters.empty() && zones.empty(); }
};

// The new loot boxes of the document, in document order. A box entity without brushes is left out (noted); a zone
// without a name gets `defaultZoneName` (noted); a name that is not lower-case letters, digits and '_' is lowered and
// cleaned (noted).
LootBoxEdits documentLootBoxes(const MapFile& map, const std::string& defaultZoneName = "map_quarry",
                               std::vector<std::string>* notes = nullptr);

// {"clusters":[{"mid":[..],"half":[..]}],"zones":[{"name":"..","mid":[..],"half":[..]}]} for the edits file.
std::string lootBoxesJson(const LootBoxEdits& boxes);
'''),
])

patch('src/core/stock_loot.cpp', [
 ('''bool hasLootPoints(const MapFile& map) {''', '''bool isLootBoxClass(const std::string& classname) {
    std::string c;
    for (const char ch : classname) c += char(std::tolower(static_cast<unsigned char>(ch)));
    return c == kLootClusterClass || c == kLootZoneClass;
}

LootBoxEdits documentLootBoxes(const MapFile& map, const std::string& defaultZoneName, std::vector<std::string>* notes) {
    LootBoxEdits out;
    size_t empty = 0, unnamed = 0, renamed = 0;
    for (const Entity& e : map.entities) {
        if (!isLootBoxClass(e.classname())) continue;
        AABB box;
        for (const EntityItem& it : e.items) {
            const Primitive* p = it.prim();
            if (!p) continue;
            if (p->kind == PrimKind::Brush) {
                const AABB b = brushBounds(brushFromPrimitive(*p));
                if (b.valid()) box.add(b);
            } else {
                for (const PatchVertex& v : p->verts) box.add(v.pos);
            }
        }
        if (!box.valid() || box.maxs.x <= box.mins.x || box.maxs.y <= box.mins.y || box.maxs.z <= box.mins.z) {
            ++empty;
            continue;
        }
        LootBoxEdit edit;
        edit.mid = (box.mins + box.maxs) * 0.5;
        edit.half = (box.maxs - box.mins) * 0.5;
        std::string c;
        for (const char ch : e.classname()) c += char(std::tolower(static_cast<unsigned char>(ch)));
        if (c == kLootClusterClass) {
            out.clusters.push_back(edit);
            continue;
        }
        const std::string* name = e.value("name");
        std::string clean;
        for (const char ch : name ? *name : std::string()) {
            const char l = char(std::tolower(static_cast<unsigned char>(ch)));
            if (std::islower(static_cast<unsigned char>(l)) || std::isdigit(static_cast<unsigned char>(l)) || l == '_') clean += l;
        }
        if (clean.empty()) {
            ++unnamed;
            clean = defaultZoneName;
        } else if (clean != *name) {
            ++renamed;
        }
        edit.name = clean;
        out.zones.push_back(edit);
    }
    if (notes) {
        if (empty) notes->push_back(std::to_string(empty) + " loot box entit" + (empty == 1 ? "y has" : "ies have") + " no brush and are left out");
        if (unnamed) notes->push_back(std::to_string(unnamed) + " loot zone(s) without a name are named " + defaultZoneName);
        if (renamed) notes->push_back(std::to_string(renamed) + " loot zone name(s) were lowered to letters, digits and '_'");
    }
    return out;
}

std::string lootBoxesJson(const LootBoxEdits& boxes) {
    const auto vec = [](const Vec3& v) { return "[" + num(v.x) + "," + num(v.y) + "," + num(v.z) + "]"; };
    std::string json = "{\\"clusters\\":[";
    for (size_t i = 0; i < boxes.clusters.size(); ++i)
        json += std::string(i ? "," : "") + "{\\"mid\\":" + vec(boxes.clusters[i].mid) + ",\\"half\\":" + vec(boxes.clusters[i].half) + "}";
    json += "],\\"zones\\":[";
    for (size_t i = 0; i < boxes.zones.size(); ++i)
        json += std::string(i ? "," : "") + "{\\"name\\":\\"" + boxes.zones[i].name + "\\",\\"mid\\":" + vec(boxes.zones[i].mid) +
                ",\\"half\\":" + vec(boxes.zones[i].half) + "}";
    return json + "]}";
}

bool hasLootPoints(const MapFile& map) {'''),
 ('''        if (!isLootPointClass(e.classname())) rest.entities.push_back(e);
''', '''        if (!isLootPointClass(e.classname()) && !isLootBoxClass(e.classname())) rest.entities.push_back(e);
'''),
 ('''#include "json_lite.h"
''', '''#include "brush_ops.h"
#include "json_lite.h"
'''),
])

patch('src/core/stock_spawns.cpp', [
 ('''        if (isLootPointClass(e.classname())) continue;  // loot points go to the edits file, never into the text (stock_loot.h)
''', '''        if (isLootPointClass(e.classname())) continue;  // loot points go to the edits file, never into the text (stock_loot.h)
        if (isLootBoxClass(e.classname())) continue;    // so do new loot boxes
'''),
])

patch('src/core/compile_iw8.cpp', [
 ('''        const std::string cls = lower(e.classname());
        if (cls != "worldspawn" && cls != "func_group") {
            ++otherEntities;
''', '''        const std::string cls = lower(e.classname());
        if (isLootBoxClass(cls)) continue;  // loot boxes go to the edits file (stock_loot.h)
        if (cls != "worldspawn" && cls != "func_group") {
            ++otherEntities;
'''),
])

patch('src/core/stock_zone_edit.h', [
 ('''                          const std::vector<StockPropCollision>* removeCollision = nullptr);
''', '''                          const std::vector<StockPropCollision>* removeCollision = nullptr, const std::string* lootBoxes = nullptr);
'''),
])
patch('src/core/stock_zone_edit.cpp', [
 ('''                          const std::vector<StockPatchOverride>* stockPatches, const std::vector<StockPropCollision>* removeCollision) {''',
  '''                          const std::vector<StockPatchOverride>* stockPatches, const std::vector<StockPropCollision>* removeCollision,
                          const std::string* lootBoxes) {'''),
 ('''    if (lootPoints) {
        sep();
        json += "\\"loot_points\\":" + *lootPoints;
    }
''', '''    if (lootBoxes && !lootBoxes->empty()) {
        sep();
        json += "\\"loot_boxes\\":" + *lootBoxes;
    }
    if (lootPoints) {
        sep();
        json += "\\"loot_points\\":" + *lootPoints;
    }
'''),
])

patch('src/qtapp/main_window_stock_edit.cpp', [
 ('''    // 13.5: placed objects (buy stations) that were moved or added as `scriptable` entities.
''', '''    // New loot clusters and zones (iw8r_loot_cluster / iw8r_loot_zone brush entities, research 89 section 9).
    std::string lootBoxes;
    {
        std::vector<std::string> boxNotes;
        const LootBoxEdits boxes = documentLootBoxes(ctx_.doc.map(), stockLoot_.zoneNames.empty() || stockLoot_.zoneNames[0].empty()
                                                                         ? std::string("map_quarry") : stockLoot_.zoneNames[0], &boxNotes);
        if (!boxes.empty()) {
            lootBoxes = lootBoxesJson(boxes);
            if (notes) *notes << QString("%1 new loot cluster(s) and %2 new loot zone(s) are added").arg(boxes.clusters.size()).arg(boxes.zones.size());
        }
        if (notes)
            for (const std::string& n : boxNotes) *notes << qs(n);
    }

    // 13.5: placed objects (buy stations) that were moved or added as `scriptable` entities.
'''),
 ('''                                           &placed.json, &stockPatches, &removeCollision);''',
  '''                                           &placed.json, &stockPatches, &removeCollision, &lootBoxes);'''),
])
print('ok')
