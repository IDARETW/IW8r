#include "clipboard_ops.h"

#include <set>

namespace iw8r {

std::string selectionToClipboardText(const Document& doc, size_t* items) {
    if (items) *items = 0;
    const auto& sel = doc.selection();
    if (sel.empty()) return {};
    const std::string eol = doc.eol();
    std::string world;     // primitives that go into worldspawn
    std::string entities;  // whole entities
    size_t n = 0, entityNo = 1;
    for (const Entity& e : doc.map().entities) {
        const bool entitySelected = sel.count(SelRef{e.id, kNoId}) > 0;
        size_t prims = 0, selectedPrims = 0;
        for (const auto& it : e.items)
            if (it.isPrimitive()) {
                ++prims;
                selectedPrims += sel.count(SelRef{e.id, it.id});
            }
        if (e.isWorldspawn()) {
            for (const auto& it : e.items)
                if (it.isPrimitive() && sel.count(SelRef{e.id, it.id})) {
                    world += it.prim()->text();
                    ++n;
                }
            continue;
        }
        const bool whole = entitySelected || (prims > 0 && selectedPrims == prims);
        if (whole) {
            entities += "// entity " + std::to_string(entityNo++) + eol + "{" + eol;
            for (const auto& it : e.items) {
                if (const KeyValueLine* kv = it.kv()) entities += "\"" + kv->name() + "\" \"" + kv->value + "\"" + eol;
                else if (it.isPrimitive()) entities += it.prim()->text();
            }
            entities += "}" + eol;
            ++n;
        } else if (selectedPrims > 0) {
            for (const auto& it : e.items)
                if (it.isPrimitive() && sel.count(SelRef{e.id, it.id})) {
                    world += it.prim()->text();
                    ++n;
                }
        }
    }
    if (n == 0) return {};
    if (items) *items = n;
    return "iwmap 4" + eol + "// entity 0" + eol + "{" + eol + "\"classname\" \"worldspawn\"" + eol + world + "}" + eol + entities;
}

size_t pasteClipboardText(Document& doc, const std::string& text, std::string* err) {
    if (text.find("iwmap") == std::string::npos && text.find("\"classname\"") == std::string::npos) {
        if (err) *err = "the clipboard does not hold map text";
        return 0;
    }
    Id next = 1;
    ParseResult pr = parseMap(text, next);
    if (!pr.map) {
        if (err) *err = pr.error ? pr.error->message : std::string("the clipboard text does not parse as a map");
        return 0;
    }
    Entity* world = doc.worldspawn();
    if (!world) {
        if (err) *err = "the map has no worldspawn";
        return 0;
    }
    std::vector<Document::PrimEdit> worldPrims;
    std::vector<std::pair<std::vector<std::pair<std::string, std::string>>, std::vector<Primitive>>> ents;
    for (const Entity& e : pr.map->entities) {
        if (e.isWorldspawn()) {
            for (const auto& it : e.items)
                if (const Primitive* p = it.prim()) worldPrims.push_back({world->id, kNoId, *p});
            continue;
        }
        std::vector<std::pair<std::string, std::string>> kvs;
        std::vector<Primitive> prims;
        for (const auto& it : e.items) {
            if (const KeyValueLine* kv = it.kv()) kvs.push_back({kv->name(), kv->value});
            else if (const Primitive* p = it.prim()) prims.push_back(*p);
        }
        if (!kvs.empty()) ents.push_back({std::move(kvs), std::move(prims)});
    }
    if (worldPrims.empty() && ents.empty()) {
        if (err) *err = "nothing to paste";
        return 0;
    }
    doc.beginGroup("Paste");
    std::vector<SelRef> pasted;
    if (!worldPrims.empty() && doc.editPrimitives(worldPrims, "Paste", true))
        for (const auto& r : doc.selection()) pasted.push_back(r);
    for (auto& [kvs, prims] : ents) {
        const Id id = doc.addEntity(kvs, prims, "Paste");
        if (id == kNoId) continue;
        bool anyPrim = false;
        if (const Entity* e = doc.findEntity(id))
            for (const auto& it : e->items)
                if (it.isPrimitive()) pasted.push_back({id, it.id}), anyPrim = true;
        if (!anyPrim) pasted.push_back({id, kNoId});
    }
    doc.endGroup();
    doc.deselectAll();
    for (const SelRef& r : pasted) doc.select(r, true);
    return worldPrims.size() + ents.size();
}

}  // namespace iw8r
