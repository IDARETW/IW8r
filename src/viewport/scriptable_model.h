// IW8 Radiant - the model a `scriptable` entity is drawn with in the camera (header only: core tests use it).
//
// A scriptable point entity names a stock definition (key "scriptable"). Without a model key of its own it is drawn
// with the definition's first model (core ScriptableCatalog, field `model`), exactly as a misc_model with that model
// key; with no catalogue model it stays a box.
#pragma once

#include <functional>
#include <string>

#include "map_text.h"
#include "scriptable_catalog.h"

namespace iw8r::app {

// The definition name of a `scriptable` entity: accepts both "scriptable" and "definition" keys (compile_iw8.cpp:2403).
inline const std::string* scriptableDefinitionKey(const Entity& e) {
    if (const std::string* def = e.value("scriptable"); def && !def->empty()) return def;
    if (const std::string* def = e.value("definition"); def && !def->empty()) return def;
    return nullptr;
}

// The model name to draw `e` with, or "" (not a scriptable, it has a model key, or the definition has no model).
// modelOf: definition name -> its first model ("" when unknown).
inline std::string scriptableModelName(const Entity& e, const std::function<std::string(const std::string&)>& modelOf) {
    if (!modelOf || e.classname() != "scriptable") return {};
    if (const std::string* m = e.value("model"); m && !m->empty()) return {};
    const std::string* def = scriptableDefinitionKey(e);
    if (!def || def->empty()) return {};
    return modelOf(*def);
}

inline std::string scriptableModelName(const Entity& e, const ScriptableCatalog& catalog) {
    return scriptableModelName(e, [&catalog](const std::string& d) {
        const ScriptableDefinition* def = catalog.find(d);
        return def ? def->model : std::string();
    });
}

// A copy of `e` with its model key set to `model` (what a misc_model placed with that model has).
inline Entity withModelKey(const Entity& e, const std::string& model) {
    Entity out = e;
    for (auto& it : out.items)
        if (KeyValueLine* kv = it.kv(); kv && kv->name() == "model") {
            kv->set(model);
            return out;
        }
    EntityItem item;
    item.v = makeKeyValue("model", model, std::string(1, char(10)), out.usesCanonicalKeys());
    out.items.push_back(std::move(item));
    return out;
}

}  // namespace iw8r::app
