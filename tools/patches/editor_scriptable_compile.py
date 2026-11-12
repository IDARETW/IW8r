import io, json

def load(p):
    return io.open(p, encoding='utf-8', newline='').read()
def save(p, s):
    io.open(p, 'w', encoding='utf-8', newline='').write(s)
def rep(s, a, b, count=1):
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == count, (s.count(a), a[:70])
    return s.replace(a, b)

# ---- catalogue: verified column
p = 'src/core/scriptable_catalog.h'
s = load(p)
s = rep(s, '''    bool observed = false;     // numbers read from stock map instances (else derived by rule)
};
''', '''    bool observed = false;     // numbers read from stock map instances (else a guess)
    bool verified = false;     // observed, and its share of the map's physics limits is known
    // What a map build accepts today: the definition is always loaded and every number is known.
    bool placeable() const { return alwaysLoaded && verified; }
};
''')
save(p, s)
p = 'src/core/scriptable_catalog.cpp'
s = load(p)
s = rep(s, '''        d.observed = f[14] == "observed";
''', '''        d.observed = f[14] == "observed";
        d.verified = f.size() > 15 && intOf(f[15]) != 0;
''')
save(p, s)

# ---- compile options
p = 'src/core/compile_iw8.h'
s = load(p)
s = rep(s, '''    const MaterialLibrary* library = nullptr;
''', '''    const MaterialLibrary* library = nullptr;
    // Stock scriptable definitions (scriptable_catalog.h). A `scriptable` point entity whose definition is placeable
    // is written to the collision JSON's scriptable_instances; without a catalogue every such entity is left out.
    const ScriptableCatalog* scriptables = nullptr;
''')
if '#include "scriptable_catalog.h"' not in s:
    first = s.index('#include "')
    s = s[:first] + '#include "scriptable_catalog.h"' + ('\r\n' if '\r\n' in s else '\n') + s[first:]
save(p, s)

# ---- compile
p = 'src/core/compile_iw8.cpp'
s = load(p)
s = rep(s, '''        if (lcls == "iw8r_decal") continue;     // written as a world.json decal volume (5.8)
''', '''        if (lcls == "iw8r_decal") continue;     // written as a world.json decal volume (5.8)
        if (lcls == "scriptable") continue;     // written as a collision json scriptable instance (6.14)
''')
s = rep(s, '''    // Dynamic props (6.15, research 41): `dynamic_prop` point entities (model, origin, angles, mass in kg, material
''', '''    // Scriptables (6.14, research 86): `scriptable` point entities (scriptable = the stock definition's name, origin,
    // angles, optional targetname / target / script_noteworthy / script_linkname / script_linkto) become the collision
    // json's scriptable_instances with the numbers of the definition from the catalogue. Only a definition that is
    // always loaded and fully known is written; any other is left out with a warning (a wrong count is a crash at load).
    std::string scriptablesJson;
    size_t scriptableCount = 0;
    {
        std::map<std::string, size_t> leftOut;  // reason -> count
        for (const Entity* pe : points) {
            if (lower(pe->classname()) != "scriptable") continue;
            const std::string* dv = pe->value("scriptable");
            if (!dv || dv->empty()) dv = pe->value("definition");
            if (!dv || dv->empty()) {
                ++leftOut["no scriptable key (the definition's name)"];
                continue;
            }
            const ScriptableDefinition* def = opt.scriptables ? opt.scriptables->find(*dv) : nullptr;
            if (!opt.scriptables || opt.scriptables->size() == 0) {
                ++leftOut["the scriptable catalogue (data/asset_catalog/iw8_scriptables.tsv) was not found"];
                continue;
            }
            if (!def) {
                ++leftOut["'" + *dv + "' is not a stock scriptable definition"];
                continue;
            }
            if (!def->alwaysLoaded) {
                ++leftOut["'" + def->name + "' lives only in stock map zones (" + def->zones + "); a map cannot carry a definition yet"];
                continue;
            }
            if (!def->verified) {
                ++leftOut["'" + def->name + "' is always loaded, but no stock map shows all the numbers a build must write for it"];
                continue;
            }
            if (scriptableCount >= 2000) {
                ++leftOut["more than 2000 scriptables"];
                continue;
            }
            ScriptablePlacement place;
            place.def = def;
            const Vec3 o = pe->origin().value_or(Vec3{});
            place.origin[0] = o.x, place.origin[1] = o.y, place.origin[2] = o.z;
            if (const std::string* av = pe->value("angles"); av && !parseVec3Text(*av, place.angles))
                place.angles[0] = place.angles[1] = place.angles[2] = 0;
            const auto text = [&](const char* key) {
                const std::string* v = pe->value(key);
                std::string out = v ? *v : std::string();
                for (char& c : out)
                    if (static_cast<unsigned char>(c) < 0x20 || c == '"' || c == '\\\\') c = '_';
                return out;
            };
            place.targetname = text("targetname");
            place.target = text("target");
            place.noteworthy = text("script_noteworthy");
            place.linkname = text("script_linkname");
            place.linkto = text("script_linkto");
            scriptablesJson += std::string(scriptableCount ? "," : "") + scriptableInstanceJson(place);
            ++scriptableCount;
        }
        for (const auto& [reason, n] : leftOut)
            warn.push_back(std::to_string(n) + " scriptable entit" + (n == 1 ? "y" : "ies") + " left out: " + reason);
        if (scriptableCount)
            warn.push_back(std::to_string(scriptableCount) + " scriptable instance(s) written (needs a converter with scriptable instances, " +
                           "research 86; not seen in game)");
    }
    // Dynamic props (6.15, research 41): `dynamic_prop` point entities (model, origin, angles, mass in kg, material
''')
s = rep(s, '''           dynModelsJson + "],\\"dynamic_brushes\\":[]}";
''', '''           dynModelsJson + "],\\"dynamic_brushes\\":[]" +
           (scriptableCount ? ",\\"scriptable_instances\\":[" + scriptablesJson + "]" : std::string()) + "}";
''')
save(p, s)

# ---- entity class
p = 'data/iw8_entity_defs_overrides.json'
raw = load(p)
nl = '\r\n' if '\r\n' in raw else '\n'
doc = json.loads(raw)
def find_classes(node):
    if isinstance(node, dict):
        if 'iw8r_decal' in node:
            return node
        for v in node.values():
            r = find_classes(v)
            if r is not None:
                return r
    return None
classes = find_classes(doc)
assert classes is not None and 'scriptable' not in classes
def key(t, default, desc, required=False, examples=None):
    return {"type": t, "default": default, "description": desc, "required": required, "examples": examples or []}
classes['scriptable'] = {
    "description": "A stock scripted object (a scriptable): the game's own object with built-in behaviour, placed by the name of its stock definition. Today a map can place the definitions the game always has loaded and whose build numbers are known: hardpoint_chevron (the Hardpoint zone marker). Doors, traffic cones and cars live in stock map zones and cannot be carried into a map yet; they are left out of the build with a warning. Tools > Scriptable Definitions lists every stock definition. Needs a converter with scriptable instances (research 86). Not seen in game.",
    "kind": "point",
    "size": [[-8, -8, -8], [8, 8, 8]],
    "color": [1.0, 0.75, 0.2],
    "buildAccepted": True,
    "keys": {
        "origin": key("vec3", "", "Where the object stands.", True, ["0 0 16"]),
        "angles": key("vec3", "0 0 0", "Pitch, yaw, roll.", False, ["0 90 0"]),
        "scriptable": key("string", "hardpoint_chevron", "The stock definition's name (data/asset_catalog/iw8_scriptables.tsv).", True, ["hardpoint_chevron"]),
        "targetname": key("string", "", "Name that script uses to find it. Stock Hardpoint chevrons carry the name of their zone's marker group.", False, ["hp_zone_1_chevrons"]),
        "target": key("string", "", "Optional target name.", False, []),
        "script_noteworthy": key("string", "", "Optional script label.", False, []),
        "script_linkname": key("string", "", "Optional link name.", False, []),
        "script_linkto": key("string", "", "Optional link target.", False, []),
    },
}
out = json.dumps(doc, indent=2, ensure_ascii=False)
if nl != '\n':
    out = out.replace('\n', nl)
save(p, out + (nl if raw.endswith(nl) else ''))
print('ok')
