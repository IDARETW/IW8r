import io

def load(p):
    return io.open(p, encoding='utf-8', newline='').read()
def save(p, s):
    io.open(p, 'w', encoding='utf-8', newline='').write(s)
def rep(s, a, b, count=1):
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == count, (s.count(a), a[:70])
    return s.replace(a, b)

p = 'src/qtapp/compile_panel.cpp'
s = load(p)
s = rep(s, '''        if (opt.library) append(QString("custom materials: %1 in the library").arg(library.names().size()));
''', '''        if (opt.library) append(QString("custom materials: %1 in the library").arg(library.names().size()));
        // Stock scriptable definitions (6.14): the catalogue beside the editor.
        opt.scriptables = &ScriptableCatalog::shared(QCoreApplication::applicationDirPath().toStdString());
''')
if '#include "scriptable_catalog.h"' not in s and '#include "../core/scriptable_catalog.h"' not in s:
    # follow the include style of material_tints
    import re
    m = re.search(r'#include "([^"]*)material_tints\.h"', s)
    assert m, 'material_tints include not found'
    s = s.replace(m.group(0), m.group(0) + ('\r\n' if '\r\n' in s else '\n') + '#include "' + m.group(1) + 'scriptable_catalog.h"', 1)
save(p, s)

p = 'src/cli/compile_cmd.cpp'
s = load(p)
s = rep(s, '''    if (!materialRoots.empty()) opt.library = &library;
''', '''    if (!materialRoots.empty()) opt.library = &library;
    // Stock scriptable definitions: the catalogue found from the working folder upward.
    opt.scriptables = &iw8r::ScriptableCatalog::shared(std::filesystem::current_path().string());
''')
save(p, s)

# ---- tests
p = 'tests/compile_iw8_tests.cpp'
s = load(p)
a = '// 12.7: "iw8r_mat:<model material>" replaces one slot of a stock model; the build makes a copy named from the change.'
assert s.count(a) == 1
nl = '\r\n' if '\r\n' in s else '\n'
t = r'''// 6.14 (research 86): `scriptable` point entities become the collision json's scriptable_instances with the numbers of
// the stock definition; a definition that is not always loaded, not fully known, or unknown is left out with a warning.
TEST(compile_iw8_scriptable_instances) {
    ScriptableCatalog catalog;
    CHECK(catalog.parse(
        "# comment\n"
        "name\tzones\talways_loaded\tparts\tserver_instanced_parts\tusable_parts\tmax_dynent_parts\tphysics_dynents\tinstance_dynent\tsparse_occluder\tcollision_flags\tphysics_limit\tflags\tmodel\tsource\tverified\n"
        "hardpoint_chevron\tcommon_core_mp\t1\t4\t4\t0\t5\t0\t1\t0\t0\t0\t0x402246\thp_chevron_rig\tobserved\t1\n"
        "scriptable_door_x\tmp_backlot2\t0\t1\t1\t1\t2\t0\t1\t1\t1\t1\t0x48E2C7\tdoor_x\tobserved\t1\n"
        "scriptable_auto_ascender\tcommon_mp\t1\t1\t1\t1\t3\t0\t1\t0\t0\t0\t0x402046\ttag_origin\tobserved\t0\n"));
    CHECK(catalog.size() == 3);
    const ScriptableDefinition* chevron = catalog.find("HARDPOINT_CHEVRON");
    CHECK(chevron && chevron->placeable() && chevron->serverInstancedParts == 4 && chevron->maxDynentParts == 5 &&
          chevron->instanceDynent && !chevron->sparseOccluder && chevron->model == "hp_chevron_rig");
    const ScriptableDefinition* door = catalog.find("scriptable_door_x");
    CHECK(door && !door->placeable() && door->sparseOccluder && door->physicsLimit && door->collisionFlags == 1 && door->usableParts == 1);
    CHECK(catalog.find("scriptable_auto_ascender") && !catalog.find("scriptable_auto_ascender")->placeable());
    CHECK(!catalog.find("nothing"));

    std::string text;
    CHECK(readFile(samplePath(), text));
    if (text.empty()) return;
    const std::string eol = text.find("\r\n") != std::string::npos ? "\r\n" : "\n";
    const std::string base = text;
    const auto ent = [&](const std::string& keys) { return "{" + eol + "\"classname\" \"scriptable\"" + eol + keys + "}" + eol; };
    const auto kv = [&](const std::string& k, const std::string& v) { return "\"" + k + "\" \"" + v + "\"" + eol; };
    text += ent(kv("scriptable", "hardpoint_chevron") + kv("origin", "64 32 16") + kv("angles", "0 90 0") + kv("targetname", "hp_a") +
                kv("script_noteworthy", "zone_1"));
    text += ent(kv("scriptable", "hardpoint_chevron") + kv("origin", "-64 32 16"));
    text += ent(kv("scriptable", "scriptable_door_x") + kv("origin", "0 0 0"));         // lives in a map zone
    text += ent(kv("scriptable", "scriptable_auto_ascender") + kv("origin", "0 0 0"));  // not fully known
    text += ent(kv("scriptable", "made_up_thing") + kv("origin", "0 0 0"));             // unknown
    text += ent(kv("origin", "0 0 0"));                                                 // no definition
    Id next = 1;
    ParseResult pr = parseMap(text, next);
    CHECK(pr.map.has_value());
    if (!pr.map) return;
    const std::string out = scratch("scriptables");
    CompileOptions opt;
    opt.mapId = kSampleId;
    opt.scriptables = &catalog;
    CompileReport rep;
    std::string err;
    CHECK(compileIw8Extraction(*pr.map, out, opt, rep, &err));
    const fs::path mp = fs::path(out) / "extracted" / "maps" / "mp";
    const std::string col = readAll((mp / (std::string(kSampleId) + ".d3dbsp.replay-collision.json")).string());
    const size_t at = col.find("\"scriptable_instances\":[");
    CHECK(at != std::string::npos);
    if (at == std::string::npos) return;
    const std::string list = col.substr(at);
    size_t entries = 0;
    for (size_t p2 = 0; (p2 = list.find("\"definition\":", p2)) != std::string::npos; ++p2) ++entries;
    CHECK(entries == 2);
    CHECK(list.find("{\"definition\":\"hardpoint_chevron\",\"origin\":[64,32,16],\"angles\":[0,90,0],\"targetname\":\"hp_a\","
                    "\"script_noteworthy\":\"zone_1\",\"server_instanced_parts\":4,\"usable_parts\":0,\"physics_limit\":false,"
                    "\"max_dynent_parts\":5,\"instance_dynent\":true,\"sparse_occluder\":false,\"collision_flags\":0,"
                    "\"physics_dynents\":0}") != std::string::npos);
    CHECK(list.find("{\"definition\":\"hardpoint_chevron\",\"origin\":[-64,32,16],\"angles\":[0,0,0],\"server_instanced_parts\":4") != std::string::npos);
    CHECK(list.find("scriptable_door_x") == std::string::npos && list.find("made_up_thing") == std::string::npos);
    bool mapZone = false, notKnown = false, unknown = false, noKey = false, written = false;
    for (const auto& w : rep.warnings) {
        mapZone |= w.find("'scriptable_door_x' lives only in stock map zones (mp_backlot2)") != std::string::npos;
        notKnown |= w.find("'scriptable_auto_ascender' is always loaded, but") != std::string::npos;
        unknown |= w.find("'made_up_thing' is not a stock scriptable definition") != std::string::npos;
        noKey |= w.find("no scriptable key") != std::string::npos;
        written |= w.find("2 scriptable instance(s) written") != std::string::npos;
    }
    CHECK(mapZone && notKnown && unknown && noKey && written);
    // The class is not an entity of the built map.
    const std::string ents = readAll((mp / (std::string(kSampleId) + ".d3dbsp.ents")).string());
    CHECK(ents.find("scriptable") == std::string::npos);

    // Without a catalogue the entities are left out; a map without scriptables has no scriptable_instances key.
    CompileOptions bare;
    bare.mapId = kSampleId;
    CompileReport rep2;
    const std::string out2 = scratch("scriptables_nocat");
    CHECK(compileIw8Extraction(*pr.map, out2, bare, rep2, &err));
    const std::string col2 = readAll((fs::path(out2) / "extracted" / "maps" / "mp" / (std::string(kSampleId) + ".d3dbsp.replay-collision.json")).string());
    CHECK(!col2.empty() && col2.find("scriptable_instances") == std::string::npos);
    bool noCatalogue = false;
    for (const auto& w : rep2.warnings) noCatalogue |= w.find("scriptable catalogue") != std::string::npos;
    CHECK(noCatalogue);
    Id next3 = 1;
    ParseResult plain = parseMap(base, next3);
    CHECK(plain.map.has_value());
    if (!plain.map) return;
    CompileReport rep3;
    const std::string out3 = scratch("scriptables_none");
    CHECK(compileIw8Extraction(*plain.map, out3, opt, rep3, &err));
    const std::string col3 = readAll((fs::path(out3) / "extracted" / "maps" / "mp" / (std::string(kSampleId) + ".d3dbsp.replay-collision.json")).string());
    CHECK(!col3.empty() && col3.find("scriptable_instances") == std::string::npos);
}

// The shipped catalogue: hardpoint_chevron is placeable; a stock door is listed but lives in map zones.
TEST(scriptable_catalog_shipped_file) {
    ScriptableCatalog catalog;
    std::string err;
    if (!catalog.load("data/asset_catalog/iw8_scriptables.tsv", &err)) {
        test::skipf("data/asset_catalog/iw8_scriptables.tsv not found");
        return;
    }
    CHECK(catalog.size() > 500);
    const ScriptableDefinition* chevron = catalog.find("hardpoint_chevron");
    CHECK(chevron && chevron->placeable() && chevron->serverInstancedParts == 4 && chevron->maxDynentParts == 5 && chevron->physicsDynents == 0);
    const ScriptableDefinition* door = catalog.find("scriptable_door_wooden_panel_03_painted_mp");
    CHECK(door && !door->alwaysLoaded && door->sparseOccluder && door->usableParts == 1 && door->maxDynentParts == 2);
    const ScriptableDefinition* cone = catalog.find("com_trafficcone_01");
    CHECK(cone && cone->physicsDynents == 1 && cone->instanceDynent && !cone->alwaysLoaded);
    size_t placeable = 0;
    for (const auto& d : catalog.all()) placeable += d.placeable() ? 1 : 0;
    CHECK(placeable >= 1);
}

'''.replace('\n', nl)
s = s.replace(a, t + a)
if 'scriptable_catalog.h' not in s:
    import re
    m = re.search(r'#include "([^"]*)compile_iw8\.h"', s)
    assert m
    s = s.replace(m.group(0), m.group(0) + nl + '#include "' + m.group(1) + 'scriptable_catalog.h"', 1)
save(p, s)
print('ok')
