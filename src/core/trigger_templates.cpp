// IW8 Radiant - trigger templates (roadmap 6.17, 6.20). See trigger_templates.h.
#include "trigger_templates.h"

#include <algorithm>
#include <set>

#include "brush_ops.h"
#include "map_text.h"

namespace iw8r {

namespace {

using KV = std::pair<std::string, std::string>;

// Trigger face suffix: the "trigger" tool material (same as gamemode_templates.cpp; isIw8ToolMaterial matches "trigger").
const char* kTriggerSuffix = " trigger 128 128 0 0 0 0";
const std::string kCalloutPrefix = "trigger_multiple_bcs_";
constexpr size_t kCalloutNameMax = 32;

// Quotes and line breaks would break the iwmap text of a key value.
bool cleanText(const std::string& s) { return s.find_first_of("\"\r\n") == std::string::npos; }

std::string vec3Text(double x, double y, double z) {
    return formatNumber(x) + " " + formatNumber(y) + " " + formatNumber(z);
}

// False (with a reason) for an empty or flat box: a flat brush has no trigger volume.
bool boxHasVolume(const AABB& b, std::string* why) {
    if (!b.valid()) {
        if (why) *why = "the box is empty";
        return false;
    }
    if (b.maxs.x <= b.mins.x || b.maxs.y <= b.mins.y || b.maxs.z <= b.mins.z) {
        if (why) *why = "the box has no volume (flat on an axis)";
        return false;
    }
    return true;
}

// One undo step: the entity is added inside a group, then selected (as gamemode_templates does).
Id addOneEntity(Document& doc, const std::string& undoName, const std::vector<KV>& kvs, std::vector<Primitive> prims,
                std::string* err) {
    auto fail = [&](const std::string& msg) {
        if (err) *err = msg;
        return kNoId;
    };
    if (doc.worldspawn() == nullptr) return fail("map has no worldspawn");
    const size_t before = doc.undoPosition();
    doc.beginGroup(undoName);
    const Id id = doc.addEntity(kvs, std::move(prims), undoName);
    doc.endGroup();
    if (doc.undoPosition() != before + 1) return fail(undoName + " did not form one undo step");
    doc.deselectAll();
    for (const SelRef& r : doc.selectionUnit({id, kNoId})) doc.select(r, true);
    return id;
}

bool calloutPrefixed(const std::string& cls) { return cls.rfind(kCalloutPrefix, 0) == 0; }

}  // namespace

const std::vector<TriggerKind>& triggerKinds() {
    // Build support, from compile_iw8.cpp:
    //   493-494  acceptedPointClass() known list: trigger_hurt, trigger_multiple, trigger_radius, trigger_use,
    //            trigger_use_touch and trigger_damage are kept (other classes are left out with a warning).
    //   509      any "trigger_multiple_bcs_*" name is accepted (callout zones).
    //   944      a "trigger_*" brush that is accepted becomes a trigger hull (trigger contents, never drawn).
    //   1371     trigger_radius is handled as a point entity with its keys.
    // "once" is not in data/iw8_entity_defs.json or the stock inventory, so it is not offered.
    static const std::vector<TriggerKind> kinds = {
        {"multiple", "trigger_multiple", "Multiple", false, {{"footprint_mask_group", "default"}}, true,
         "compile_iw8.cpp:493-494 (accepted), :944 (trigger hull)",
         "Brush volume; scripts check its classname (defs: trigger_multiple)."},
        {"hurt", "trigger_hurt", "Hurt", false, {{"dmg", "25"}, {"footprint_mask_group", "default"}}, true,
         "compile_iw8.cpp:493 (accepted), :944 (trigger hull)",
         "Brush volume that damages what is inside (dmg 25 by default)."},
        {"use", "trigger_use", "Use", false, {{"footprint_mask_group", "default"}}, true,
         "compile_iw8.cpp:494 (accepted), :944 (trigger hull)",
         "Brush volume for player use (defs: trigger_use)."},
        {"use_touch", "trigger_use_touch", "Use on Touch", false, {{"footprint_mask_group", "default"}}, true,
         "compile_iw8.cpp:494 (accepted), :944 (trigger hull)",
         "Brush volume for use on touch (defs: trigger_use_touch)."},
        {"radius", "trigger_radius", "Radius", true, {{"footprint_mask_group", "default"}}, true,
         "compile_iw8.cpp:494 (accepted), :1371 (point entity)",
         "Point trigger with radius and height taken from the box; origin at the bottom centre."},
        {"damage", "trigger_damage", "Damage", false, {{"footprint_mask_group", "default"}}, false,
         "compile_iw8.cpp:493 accepts the class, but defs say the converter drops it without script_exploder, "
         "and no script_exploder value is known",
         "Brush trigger with damage. Not offered: the build drops it without script_exploder."},
    };
    return kinds;
}

const TriggerKind* findTriggerKind(const std::string& id) {
    for (const TriggerKind& k : triggerKinds())
        if (k.id == id) return &k;
    return nullptr;
}

Id insertTrigger(Document& doc, const std::string& kindId, const AABB& box, const std::string& targetname, std::string* err) {
    auto fail = [&](const std::string& msg) {
        if (err) *err = msg;
        return kNoId;
    };
    const TriggerKind* k = findTriggerKind(kindId);
    if (!k) return fail("unknown trigger kind '" + kindId + "'");
    if (!k->buildSupported) return fail(k->classname + " is not kept by the build: " + k->buildNote);
    if (!cleanText(targetname)) return fail("targetname has a quote or line break");
    std::string why;
    if (!boxHasVolume(box, &why)) return fail(why);

    std::vector<KV> kvs{{"classname", k->classname}};
    std::vector<Primitive> prims;
    if (k->point) {
        // trigger_radius: stock values are radius 64 / height 128 for a 128-wide zone (gamemode_templates CTF).
        const double dx = box.maxs.x - box.mins.x, dy = box.maxs.y - box.mins.y, dz = box.maxs.z - box.mins.z;
        const Vec3 c = box.center();
        kvs.push_back({"origin", vec3Text(c.x, c.y, box.mins.z)});
        kvs.push_back({"radius", formatNumber(std::max(dx, dy) / 2)});
        kvs.push_back({"height", formatNumber(dz)});
    } else {
        const Vec3 c = box.center();
        kvs.push_back({"origin", vec3Text(c.x, c.y, c.z)});
        auto prim = makeBrushPrimitive(makeBox(box, kTriggerSuffix), doc.eol());
        if (!prim) return fail("the box did not make a brush");
        prims.push_back(*prim);
    }
    for (const auto& kv : k->defaultKeys) kvs.push_back(kv);
    if (!targetname.empty()) kvs.push_back({"targetname", targetname});
    return addOneEntity(doc, "Insert Trigger: " + k->title, kvs, std::move(prims), err);
}

bool isValidCalloutArea(const std::string& name) {
    if (name.empty() || name.size() > kCalloutNameMax) return false;
    for (char c : name) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
        if (!ok) return false;
    }
    return true;
}

Id insertCalloutZone(Document& doc, const std::string& areaName, const AABB& box, std::string* err) {
    auto fail = [&](const std::string& msg) {
        if (err) *err = msg;
        return kNoId;
    };
    if (!isValidCalloutArea(areaName))
        return fail("callout area name must be 1-32 characters of a-z, 0-9 and _: '" + areaName + "'");
    std::string why;
    if (!boxHasVolume(box, &why)) return fail(why);
    auto prim = makeBrushPrimitive(makeBox(box, kTriggerSuffix), doc.eol());
    if (!prim) return fail("the box did not make a brush");
    const Vec3 c = box.center();
    const std::vector<KV> kvs{{"classname", kCalloutPrefix + areaName},
                              {"origin", vec3Text(c.x, c.y, c.z)},
                              {"footprint_mask_group", "default"}};
    std::vector<Primitive> prims{*prim};
    return addOneEntity(doc, "Insert Callout Zone: " + areaName, kvs, std::move(prims), err);
}

std::vector<std::string> calloutNamesInMap(const MapFile& map) {
    std::set<std::string> names;
    for (const Entity& e : map.entities) {
        const std::string cls = e.classname();
        if (calloutPrefixed(cls)) names.insert(cls.substr(kCalloutPrefix.size()));
    }
    return {names.begin(), names.end()};
}

std::vector<std::string> checkCallouts(const MapFile& map) {
    std::vector<std::string> warnings;

    struct Zone {
        size_t entity;
        std::string name;
        AABB box;
    };
    std::vector<Zone> zones;

    for (size_t i = 0; i < map.entities.size(); ++i) {
        const Entity& e = map.entities[i];
        const std::string cls = e.classname();
        if (!calloutPrefixed(cls)) continue;
        const std::string name = cls.substr(kCalloutPrefix.size());
        const std::string where = "entity " + std::to_string(i) + " (" + cls + ")";

        if (!isValidCalloutArea(name))
            warnings.push_back(where + ": area name '" + name + "' is invalid (1-32 characters of a-z, 0-9, _)");

        AABB box;
        for (const auto& it : e.items)
            if (const Primitive* p = it.prim())
                if (p->kind == PrimKind::Brush) box.add(p->bounds());
        if (!box.valid()) {
            warnings.push_back(where + ": has no brush, so no volume");
            continue;
        }
        if (box.maxs.x <= box.mins.x || box.maxs.y <= box.mins.y || box.maxs.z <= box.mins.z) {
            warnings.push_back(where + ": zero-volume box");
            continue;
        }
        zones.push_back({i, name, box});
    }

    // Overlap of volumes with different names: strict overlap on all three axes (touching faces do not count).
    for (size_t a = 0; a < zones.size(); ++a) {
        for (size_t b = a + 1; b < zones.size(); ++b) {
            const Zone& za = zones[a];
            const Zone& zb = zones[b];
            if (za.name == zb.name) continue;
            const bool overlap = std::min(za.box.maxs.x, zb.box.maxs.x) > std::max(za.box.mins.x, zb.box.mins.x) &&
                                 std::min(za.box.maxs.y, zb.box.maxs.y) > std::max(za.box.mins.y, zb.box.mins.y) &&
                                 std::min(za.box.maxs.z, zb.box.maxs.z) > std::max(za.box.mins.z, zb.box.mins.z);
            if (overlap)
                warnings.push_back("entity " + std::to_string(za.entity) + " ('" + za.name + "') overlaps entity " +
                                   std::to_string(zb.entity) + " ('" + zb.name + "'): a point in both is ambiguous");
        }
    }
    return warnings;
}

}  // namespace iw8r
