#include "scripted_objects.h"

#include <cstdio>

namespace iw8r {

namespace {

bool isModelEntity(const Entity& e) {
    const std::string c = e.classname();
    const std::string* m = e.value("model");
    return (c == "misc_model" || c == "script_model") && m && !m->empty() && m->front() != '*';
}

std::string fmt(double v) {
    char b[48];
    std::snprintf(b, sizeof b, "%.6g", v);
    return b;
}

}  // namespace

int makeDestructible(Document& doc, const std::set<Id>& entities) {
    int n = 0;
    doc.beginGroup("Make Destructible");
    for (Id id : entities) {
        const Entity* e = doc.findEntity(id);
        if (!e || !isModelEntity(*e)) continue;
        doc.setKeyValue(id, "classname", "script_model");
        doc.setKeyValue(id, "targetname", "destructable");
        if (!doc.findEntity(id)->value("footprint_mask_group")) doc.setKeyValue(id, "footprint_mask_group", "default");
        ++n;
    }
    doc.endGroup();
    return n;
}

int makeScripted(Document& doc, const std::set<Id>& entities, std::string* names) {
    std::set<std::string> used;
    for (const auto& e : doc.map().entities)
        if (const std::string* t = e.value("targetname")) used.insert(*t);
    int n = 0, next = 1;
    doc.beginGroup("Make Scripted");
    for (Id id : entities) {
        const Entity* e = doc.findEntity(id);
        if (!e || !isModelEntity(*e)) continue;
        std::string name;
        if (const std::string* t = e->value("targetname"); t && !t->empty() && *t != "destructable") {
            name = *t;  // keep the mapper's name
        } else {
            do name = "scripted_prop_" + std::to_string(next++);
            while (used.count(name));
            used.insert(name);
        }
        const Vec3 o = e->origin().value_or(Vec3{});
        doc.setKeyValue(id, "classname", "script_model");
        doc.setKeyValue(id, "targetname", name);
        if (!doc.findEntity(id)->value("footprint_mask_group")) doc.setKeyValue(id, "footprint_mask_group", "default");
        // The targeting script_origin keeps it a live entity (x64-zt promotes untargeted script_models to static props).
        bool targetedAlready = false;
        for (const auto& other : doc.map().entities)
            if (const std::string* t = other.value("target"); t && *t == name) targetedAlready = true;
        if (!targetedAlready)
            doc.addEntity({{"classname", "script_origin"}, {"target", name}, {"origin", fmt(o.x) + " " + fmt(o.y) + " " + fmt(o.z + 8)}}, {},
                          "script_origin for " + name);
        if (names) *names += (n ? ", " : "") + name;
        ++n;
    }
    doc.endGroup();
    return n;
}

}  // namespace iw8r
