// iw8r - command line front end for the IW8 Radiant core.
//
//   iw8r iw8-ents <ACTS map_ents asset.json> [--strings <script_strings.json>] [-o out.ents]
//   iw8r info <map>
//   iw8r roundtrip <map> [-o out.map]
//   iw8r diff <a.map> <b.map>
//   iw8r compile <in.map> -o <extraction dir> --map <mp_id>
//   iw8r generate-spawns <in.map> -o <out.map> [--ffa N] [--starts N] [--spacing U] [--replace]
//   iw8r edit <in.map> -o <out.map> [steps...]
//       --grid <n>                 set grid size
//       --select-brush <n>         select the n-th worldspawn brush (0-based)
//       --select-prim <e> <n>      select the n-th primitive of entity index e
//       --select-entity <class> [k]  select the k-th entity with that classname (default 0)
//       --add                      make the next --select* additive
//       --deselect                 clear the selection
//       --move <dx> <dy> <dz>      move the selection (values are snapped to the grid)
//       --move-raw <dx> <dy> <dz>  move without snapping
//       --set <key> <value>        set a key on the inspected entity
//       --remove-key <key>         remove a key from the inspected entity
//       --delete                   delete the selection
//       --undo / --redo
//       --save-copy <path>         write the current state without changing the document path
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "document.h"
#include "iw8_dump.h"
#include "iw8_mapents.h"
#include "shape_models.h"

#ifdef _WIN32
#include <windows.h>
#endif

using namespace iw8r;

namespace {

int usage() {
    std::fprintf(stderr,
                 "usage:\n"
                 "  iw8r [--keys <iw8_canonical_keys.tsv>] <command> ...\n"
                 "  iw8r iw8-ents <map_ents .asset.json> [--strings script_strings.json] [-o out.ents] [--spawns n]\n"
                 "  iw8r iw8d-ents <map_ents .iw8> [-o out.ents]          (x64-zt dump)\n"
                 "  iw8r iw8d-set-ents <map_ents .iw8> <ents file> -o <out .iw8>\n"
                 "  iw8r preflight <extracted dir> <map id> [--data <dataDir>]   (build checks before shipping)\n"
                 "  iw8r report <extracted dir> <map id> [--save file.json] [--diff old.json]\n"
                 "  iw8r sky-panorama <panorama> <out.dds> [--yaw deg] [--ev stops] [--face N]   (custom sky cube)\n"
                 "  iw8r info <map>\n"
                 "  iw8r roundtrip <map> [-o out.map]\n"
                 "  iw8r diff <a.map> <b.map>\n"
                 "  iw8r generate-spawns <in.map> -o <out.map> [--ffa N] [--starts N] [--spacing U] [--replace]\n"
                 "  iw8r edit <in.map> -o <out.map> [--grid n] [--select-brush n] [--select-prim e n]\n"
                 "            [--select-entity class [k]] [--add] [--deselect] [--move dx dy dz]\n"
                 "            [--move-raw dx dy dz] [--set key value] [--remove-key key] [--delete]\n"
                 "            [--undo] [--redo] [--save-copy path]\n");
    return 2;
}

bool loadDoc(Document& doc, const std::string& path) {
    std::string err;
    if (!doc.load(path, &err)) {
        std::fprintf(stderr, "error: %s\n", err.c_str());
        return false;
    }
    return true;
}

int cmdInfo(const std::string& path) {
    Document doc;
    if (!loadDoc(doc, path)) return 1;
    size_t brushes = 0, patches = 0, unknown = 0, faces = 0, kvs = 0, opaque = 0;
    std::map<std::string, size_t> materials, classes;
    AABB bounds;
    for (const auto& e : doc.map().entities) {
        ++classes[e.classname()];
        for (const auto& it : e.items) {
            if (it.isKeyValue()) { ++kvs; continue; }
            const Primitive* p = it.prim();
            if (!p) { ++opaque; continue; }
            if (p->kind == PrimKind::Brush) {
                ++brushes;
                faces += p->faces.size();
                for (const auto& f : p->faces) ++materials[f.material()];
            } else if (p->kind == PrimKind::Patch) {
                ++patches;
                ++materials[p->patchMaterial];
            } else {
                ++unknown;
            }
            bounds.add(p->bounds());
        }
    }
    std::printf("file        %s\n", path.c_str());
    std::printf("bytes       %zu\n", doc.text().size());
    std::printf("line ending %s\n", doc.map().eol == "\r\n" ? "CRLF" : "LF");
    std::printf("entities    %zu\n", doc.map().entities.size());
    std::printf("brushes     %zu (%zu faces)\n", brushes, faces);
    std::printf("patches     %zu\n", patches);
    std::printf("unknown     %zu primitives, %zu opaque lines\n", unknown, opaque);
    std::printf("key/values  %zu\n", kvs);
    if (bounds.valid())
        std::printf("bounds      (%s %s %s) - (%s %s %s)\n", formatNumber(bounds.mins.x).c_str(), formatNumber(bounds.mins.y).c_str(),
                    formatNumber(bounds.mins.z).c_str(), formatNumber(bounds.maxs.x).c_str(), formatNumber(bounds.maxs.y).c_str(),
                    formatNumber(bounds.maxs.z).c_str());
    std::printf("classes     %zu distinct\n", classes.size());
    for (const auto& [c, n] : classes) std::printf("  %5zu  %s\n", n, c.empty() ? "(none)" : c.c_str());
    std::printf("materials   %zu distinct\n", materials.size());
    for (const auto& [m, n] : materials) std::printf("  %5zu  %s\n", n, m.c_str());
    return 0;
}

int cmdRoundtrip(const std::string& path, const std::string& out) {
    std::string original;
    if (!readFile(path, original)) {
        std::fprintf(stderr, "error: cannot read %s\n", path.c_str());
        return 1;
    }
    Document doc;
    std::string err;
    if (!doc.loadFromText(original, path, &err)) {
        std::fprintf(stderr, "error: %s\n", err.c_str());
        return 1;
    }
    const std::string written = doc.text();
    if (!out.empty() && !doc.saveCopy(out, &err)) {
        std::fprintf(stderr, "error: %s\n", err.c_str());
        return 1;
    }
    if (written != original) {
        std::printf("FAIL %s: serialized text differs from input\n", path.c_str());
        return 1;
    }
    std::printf("OK   %s: %zu bytes, %zu entities, byte-identical\n", path.c_str(), original.size(), doc.map().entities.size());
    return 0;
}

int cmdDiff(const std::string& a, const std::string& b) {
    Document da, db;
    if (!loadDoc(da, a) || !loadDoc(db, b)) return 1;
    const MapDiff d = diffMaps(da.map(), db.map());
    std::printf("identical items: %zu\n", d.identicalItems);
    std::printf("changes: %zu%s\n", d.changes.size(), d.structureEqual ? "" : " (item counts differ)");
    for (const auto& c : d.changes) {
        const Entity& e = d.structureEqual || c.entity < da.map().entities.size() ? da.map().entities[c.entity] : db.map().entities[c.entity];
        std::printf("  entity %zu (%s) item %zu: %s %s\n", c.entity, e.classname().c_str(), c.item, c.kind.c_str(), c.detail.c_str());
    }
    return d.changes.empty() ? 0 : 3;
}

double num(const char* s) { return std::strtod(s, nullptr); }

int cmdEdit(int argc, char** argv) {
    if (argc < 3) return usage();
    const std::string in = argv[2];
    std::string out;
    Document doc;
    if (!loadDoc(doc, in)) return 1;
    bool additive = false;
    auto need = [&](int i, int n) {
        if (i + n >= argc) {
            std::fprintf(stderr, "error: %s needs %d argument(s)\n", argv[i], n);
            std::exit(2);
        }
    };
    auto selectRef = [&](const SelRef& r, const char* what) {
        doc.select(r, additive);
        additive = false;
        std::printf("select    %s -> %zu selected\n", what, doc.selection().size());
    };
    for (int i = 3; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "-o") { need(i, 1); out = argv[++i]; }
        else if (a == "--grid") { need(i, 1); doc.setGrid(num(argv[++i])); std::printf("grid      %s\n", formatNumber(doc.grid()).c_str()); }
        else if (a == "--add") additive = true;
        else if (a == "--deselect") { doc.deselectAll(); std::printf("deselect\n"); }
        else if (a == "--select-brush") {
            need(i, 1);
            const long n = std::strtol(argv[++i], nullptr, 10);
            Entity* w = doc.worldspawn();
            long k = 0;
            bool found = false;
            if (w)
                for (const auto& it : w->items)
                    if (const Primitive* p = it.prim(); p && p->kind == PrimKind::Brush && k++ == n) {
                        selectRef({w->id, it.id}, ("worldspawn brush " + std::to_string(n)).c_str());
                        found = true;
                        break;
                    }
            if (!found) { std::fprintf(stderr, "error: no worldspawn brush %ld\n", n); return 1; }
        } else if (a == "--select-prim") {
            need(i, 2);
            const size_t e = std::strtoul(argv[++i], nullptr, 10);
            const long n = std::strtol(argv[++i], nullptr, 10);
            if (e >= doc.map().entities.size()) { std::fprintf(stderr, "error: no entity %zu\n", e); return 1; }
            const Entity& ent = doc.map().entities[e];
            long k = 0;
            bool found = false;
            for (const auto& it : ent.items)
                if (it.isPrimitive() && k++ == n) {
                    selectRef({ent.id, it.id}, ("entity " + std::to_string(e) + " primitive " + std::to_string(n)).c_str());
                    found = true;
                    break;
                }
            if (!found) { std::fprintf(stderr, "error: entity %zu has no primitive %ld\n", e, n); return 1; }
        } else if (a == "--select-entity") {
            need(i, 1);
            const std::string cls = argv[++i];
            long want = 0;
            if (i + 1 < argc && argv[i + 1][0] != '-') want = std::strtol(argv[++i], nullptr, 10);
            long k = 0;
            bool found = false;
            for (const auto& e : doc.map().entities)
                if (e.classname() == cls && k++ == want) {
                    bool hasPrim = false;
                    for (const auto& it : e.items) hasPrim |= it.isPrimitive();
                    Id first = kNoId;
                    if (hasPrim)
                        for (const auto& it : e.items)
                            if (it.isPrimitive()) { first = it.id; break; }
                    selectRef({e.id, first}, (cls + " #" + std::to_string(want)).c_str());
                    found = true;
                    break;
                }
            if (!found) { std::fprintf(stderr, "error: no entity '%s' #%ld\n", cls.c_str(), want); return 1; }
        } else if (a == "--move" || a == "--move-raw") {
            need(i, 3);
            Vec3 d{num(argv[i + 1]), num(argv[i + 2]), num(argv[i + 3])};
            i += 3;
            if (a == "--move") d = snapToGrid(d, doc.grid());
            const bool ok = doc.translateSelection(d);
            std::printf("move      %s %s %s -> %s\n", formatNumber(d.x).c_str(), formatNumber(d.y).c_str(), formatNumber(d.z).c_str(),
                        ok ? "ok" : "nothing to move");
        } else if (a == "--set") {
            need(i, 2);
            const std::string k = argv[++i], v = argv[++i];
            const Id e = doc.inspectedEntity();
            const bool ok = doc.setKeyValue(e, k, v);
            const Entity* ent = doc.findEntity(e);
            std::printf("set       [%s] \"%s\" \"%s\" -> %s\n", ent ? ent->classname().c_str() : "?", k.c_str(), v.c_str(), ok ? "ok" : "unchanged");
        } else if (a == "--remove-key") {
            need(i, 1);
            const std::string k = argv[++i];
            std::printf("remove    \"%s\" -> %s\n", k.c_str(), doc.removeKey(doc.inspectedEntity(), k) ? "ok" : "not found");
        } else if (a == "--delete") {
            std::printf("delete    -> %s\n", doc.deleteSelection() ? "ok" : "nothing selected");
        } else if (a == "--undo") {
            const std::string name = doc.canUndo() ? doc.history()[doc.undoPosition() - 1].name : "";
            std::printf("undo      %s\n", doc.undo() ? name.c_str() : "(nothing to undo)");
        } else if (a == "--redo") {
            const std::string name = doc.canRedo() ? doc.history()[doc.undoPosition()].name : "";
            std::printf("redo      %s\n", doc.redo() ? name.c_str() : "(nothing to redo)");
        } else if (a == "--save-copy") {
            need(i, 1);
            std::string err;
            const std::string p = argv[++i];
            if (!doc.saveCopy(p, &err)) { std::fprintf(stderr, "error: %s\n", err.c_str()); return 1; }
            std::printf("save-copy %s\n", p.c_str());
        } else {
            std::fprintf(stderr, "error: unknown step '%s'\n", a.c_str());
            return usage();
        }
    }
    if (out.empty()) {
        std::fprintf(stderr, "error: edit requires -o <out.map>\n");
        return 2;
    }
    std::string err;
    if (!doc.saveAs(out, &err)) {
        std::fprintf(stderr, "error: %s\n", err.c_str());
        return 1;
    }
    std::printf("saved     %s (%zu undo steps, position %zu)\n", out.c_str(), doc.history().size(), doc.undoPosition());
    return 0;
}

int cmdIw8Ents(int argc, char** argv) {
    if (argc < 3) return usage();
    std::string strings, out;
    size_t listSpawns = 10;
    for (int i = 3; i + 1 < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--strings") strings = argv[++i];
        else if (a == "-o") out = argv[++i];
        else if (a == "--spawns") listSpawns = std::strtoul(argv[++i], nullptr, 10);
    }
    if (strings.empty()) {
        // ACTS layout: <zone>/assets/map_ents/maps/mp/<map>.d3dbsp.0.asset.json and <zone>/script_strings.json
        const std::string p = argv[2];
        const size_t cut = p.find("assets");
        if (cut != std::string::npos) {
            const std::string guess = p.substr(0, cut) + "script_strings.json";
            std::string probe;
            if (readFile(guess, probe)) strings = guess;
        }
    }
    Iw8MapEntsDump d;
    std::string err;
    if (!loadActsMapEnts(argv[2], strings, d, &err)) {
        std::fprintf(stderr, "error: %s\n", err.c_str());
        return 1;
    }
    std::printf("asset         %s (profile %s)\n", d.assetName.c_str(), d.profile.c_str());
    std::printf("entityString  %llu bytes%s\n", (unsigned long long)d.numEntityChars,
                !d.entityString.empty() && d.entityString.back() == '\0' ? " (NUL terminated)" : "");
    Document doc;
    if (!doc.loadFromText(d.entityString, d.assetName, &err)) {
        std::fprintf(stderr, "error: entity string does not parse: %s\n", err.c_str());
        return 1;
    }
    size_t canonical = 0, quoted = 0, script = 0, unresolved = 0;
    std::map<std::string, size_t> classes;
    for (const auto& e : doc.map().entities) {
        ++classes[e.classname()];
        for (const auto& it : e.items)
            if (const KeyValueLine* kv = it.kv()) {
                if (kv->form == KeyForm::Canonical) {
                    ++canonical;
                    unresolved += KeyTable::global().name(kv->keyId) == nullptr;
                } else if (kv->form == KeyForm::Quoted) ++quoted;
                else ++script;
            }
    }
    std::printf("entities      %zu, round trip %s\n", doc.map().entities.size(), doc.text() == d.entityString ? "byte-identical" : "DIFFERS");
    std::printf("keys          %zu canonical-id (%zu without a name in the key table of %zu), %zu quoted, %zu script-string\n",
                canonical, unresolved, KeyTable::global().size(), quoted, script);
    std::printf("classes       %zu distinct\n", classes.size());
    for (const auto& [c, n] : classes) std::printf("  %5zu  %s\n", n, c.c_str());
    for (const auto& [s, n] : d.sections) std::printf("section       %-22s %llu\n", s.c_str(), (unsigned long long)n);
    std::printf("spawns        %zu compiled spawnList records (not in the entity string)\n", d.spawns.size());
    for (size_t i = 0; i < d.spawns.size() && i < listSpawns; ++i) {
        const auto& s = d.spawns[i];
        std::printf("  %4u %-32s origin %s %s %s  yaw %s\n", s.index, s.name.c_str(), formatFloat32(float(s.origin.x)).c_str(),
                    formatFloat32(float(s.origin.y)).c_str(), formatFloat32(float(s.origin.z)).c_str(), formatFloat32(float(s.angles.y)).c_str());
    }
    if (!out.empty()) {
        if (!writeFileAtomic(out, d.entityString, &err)) {
            std::fprintf(stderr, "error: %s\n", err.c_str());
            return 1;
        }
        std::printf("wrote         %s\n", out.c_str());
    }
    return 0;
}

// Builds the IW8 entity-class inventory from ACTS map_ents dumps with the editor's own parser.
//   iw8r iw8-classes -o data/iw8_entity_classes.json <asset.json>...
int cmdIw8Classes(int argc, char** argv) {
    std::string out;
    std::vector<std::string> inputs;
    for (int i = 2; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "-o" && i + 1 < argc) out = argv[++i];
        else inputs.push_back(a);
    }
    if (out.empty() || inputs.empty()) return usage();
    struct KeyStat { size_t count = 0; std::vector<std::string> examples; };
    struct ClassStat { size_t count = 0; std::set<std::string> maps; std::map<std::string, KeyStat> keys; };
    std::map<std::string, ClassStat> classes;
    std::map<std::string, size_t> spawnNames;
    std::vector<std::string> sources;
    auto jsonStr = [](const std::string& s) {
        std::string r = "\"";
        for (unsigned char c : s) {
            if (c == '"' || c == '\\') { r += '\\'; r += char(c); }
            else if (c < 0x20) { char b[8]; std::snprintf(b, sizeof(b), "\\u%04x", c); r += b; }
            else r += char(c);
        }
        return r + "\"";
    };
    for (const auto& in : inputs) {
        std::string strings;
        const size_t cut = in.find("assets");
        if (cut != std::string::npos) {
            std::string probe;
            if (readFile(in.substr(0, cut) + "script_strings.json", probe)) strings = in.substr(0, cut) + "script_strings.json";
        }
        Iw8MapEntsDump d;
        std::string err;
        if (!loadActsMapEnts(in, strings, d, &err)) { std::fprintf(stderr, "error: %s\n", err.c_str()); return 1; }
        Document doc;
        if (!doc.loadFromText(d.entityString, d.assetName, &err)) { std::fprintf(stderr, "error: %s\n", err.c_str()); return 1; }
        std::string map = d.assetName;
        if (auto s = map.find_last_of('/'); s != std::string::npos) map = map.substr(s + 1);
        if (auto s = map.find('.'); s != std::string::npos) map = map.substr(0, s);
        sources.push_back(map);
        for (const auto& e : doc.map().entities) {
            auto& c = classes[e.classname()];
            ++c.count;
            c.maps.insert(map);
            for (const auto& it : e.items)
                if (const KeyValueLine* kv = it.kv()) {
                    auto& k = c.keys[kv->name()];
                    ++k.count;
                    if (k.examples.size() < 3 && std::find(k.examples.begin(), k.examples.end(), kv->value) == k.examples.end())
                        k.examples.push_back(kv->value.substr(0, 80));
                }
        }
        for (const auto& s : d.spawns) ++spawnNames[s.name];
        std::printf("%-16s %5zu entities, %4zu spawns\n", map.c_str(), doc.map().entities.size(), d.spawns.size());
    }
    std::string j = "{\n  \"build\": \"replay-1.20.4.7623265\",\n  \"generator\": \"iw8r iw8-classes (IW8 Radiant core parser)\",\n  \"sources\": [";
    for (size_t i = 0; i < sources.size(); ++i) j += (i ? ", " : "") + jsonStr(sources[i]);
    j += "],\n  \"classes\": {";
    bool firstC = true;
    for (const auto& [name, c] : classes) {
        j += std::string(firstC ? "\n" : ",\n") + "    " + jsonStr(name) + ": {\"count\": " + std::to_string(c.count) + ", \"maps\": [";
        firstC = false;
        bool fm = true;
        for (const auto& m : c.maps) { j += (fm ? "" : ", ") + jsonStr(m); fm = false; }
        j += "], \"keys\": {";
        bool fk = true;
        for (const auto& [k, ks] : c.keys) {
            j += std::string(fk ? "" : ", ") + jsonStr(k) + ": {\"count\": " + std::to_string(ks.count) + ", \"examples\": [";
            fk = false;
            for (size_t i = 0; i < ks.examples.size(); ++i) j += (i ? ", " : "") + jsonStr(ks.examples[i]);
            j += "]}";
        }
        j += "}}";
    }
    j += "\n  },\n  \"spawn_names\": {";
    bool fs = true;
    for (const auto& [n, c] : spawnNames) { j += std::string(fs ? "\n" : ",\n") + "    " + jsonStr(n) + ": " + std::to_string(c); fs = false; }
    j += "\n  }\n}\n";
    std::string err;
    if (!writeFileAtomic(out, j, &err)) { std::fprintf(stderr, "error: %s\n", err.c_str()); return 1; }
    std::printf("wrote %s: %zu classes, %zu spawn names\n", out.c_str(), classes.size(), spawnNames.size());
    return 0;
}

// x64-zt IW8D map_ents dumps (milestone 2 write-back path).
//   iw8r iw8d-ents <map_ents .iw8> [-o out.ents]
//   iw8r iw8d-set-ents <map_ents .iw8> <entity string file> -o <out .iw8>
int cmdIw8dEnts(int argc, char** argv, bool set) {
    if (argc < 3) return usage();
    std::string in = argv[2], ents, out;
    int i = 3;
    if (set) {
        if (argc < 4) return usage();
        ents = argv[3];
        i = 4;
    }
    for (; i + 1 < argc; ++i)
        if (std::string(argv[i]) == "-o") out = argv[++i];
    std::string raw, err;
    if (!readFile(in, raw)) { std::fprintf(stderr, "error: cannot read %s\n", in.c_str()); return 1; }
    Iw8Dump d;
    if (!parseIw8Dump(raw, d, &err)) { std::fprintf(stderr, "error: %s: %s\n", in.c_str(), err.c_str()); return 1; }
    const bool exact = serializeIw8Dump(d) == raw;
    std::string current;
    if (!mapEntsEntityString(d, current, &err)) { std::fprintf(stderr, "error: %s\n", err.c_str()); return 1; }
    std::printf("dump          type %d, %zu records, re-serialized %s\n", d.type, d.records.size(), exact ? "byte-identical" : "DIFFERS");
    std::printf("entityString  %zu bytes\n", current.size());
    if (!set) {
        if (!out.empty() && !writeFileAtomic(out, current, &err)) { std::fprintf(stderr, "error: %s\n", err.c_str()); return 1; }
        if (!out.empty()) std::printf("wrote         %s\n", out.c_str());
        return exact ? 0 : 1;
    }
    if (out.empty()) { std::fprintf(stderr, "error: iw8d-set-ents needs -o <out .iw8>\n"); return 2; }
    std::string text;
    if (!readFile(ents, text)) { std::fprintf(stderr, "error: cannot read %s\n", ents.c_str()); return 1; }
    Document check;
    if (!check.loadFromText(text, ents, &err)) { std::fprintf(stderr, "error: entity string does not parse: %s\n", err.c_str()); return 1; }
    if (!setMapEntsEntityString(d, text, &err)) { std::fprintf(stderr, "error: %s\n", err.c_str()); return 1; }
    if (!writeFileAtomic(out, serializeIw8Dump(d), &err)) { std::fprintf(stderr, "error: %s\n", err.c_str()); return 1; }
    std::printf("wrote         %s (%zu entities, entityString %zu bytes; every other record unchanged)\n", out.c_str(),
                check.map().entities.size(), d.records[2].bytes.size());
    return 0;
}

void loadKeyTable(std::vector<char*>& args) {
    std::string explicitPath;
    for (size_t i = 1; i + 1 < args.size(); ++i)
        if (std::string(args[i]) == "--keys") {
            explicitPath = args[i + 1];
            args.erase(args.begin() + (std::ptrdiff_t)i, args.begin() + (std::ptrdiff_t)i + 2);
            break;
        }
    std::string err;
    if (!explicitPath.empty()) {
        if (!KeyTable::global().loadTsv(explicitPath, &err)) std::fprintf(stderr, "warning: %s\n", err.c_str());
        return;
    }
    if (const char* env = std::getenv("IW8R_KEYS")) {
        KeyTable::global().loadTsv(env, &err);
        return;
    }
#ifdef _WIN32
    char exe[1024] = {};
    GetModuleFileNameA(nullptr, exe, sizeof(exe));
    std::string dir = exe;
    for (int up = 0; up < 3; ++up) {
        const size_t slash = dir.find_last_of("\\/");
        if (slash == std::string::npos) break;
        dir = dir.substr(0, slash);
        const std::string candidate = dir + "\\data\\generated\\iw8_canonical_keys.tsv";
        std::string probe;
        if (readFile(candidate, probe)) {
            KeyTable::global().loadTsv(candidate, &err);
            return;
        }
    }
#endif
}

}  // namespace

int cmdCompileIw8(int argc, char** argv);  // compile_cmd.cpp
int cmdIw8dSpawns(int argc, char** argv);  // spawns_cmd.cpp

int cmdLiveRead(int argc, char** argv);
int cmdPreflight(int argc, char** argv);  // preflight_cmd.cpp
int cmdReport(int argc, char** argv);      // report_cmd.cpp
int cmdSkyPanorama(int argc, char** argv); // sky_cmd.cpp
int cmdSkySun(int argc, char** argv);       // sky_cmd.cpp
int cmdVisionSet(int argc, char** argv);    // sky_cmd.cpp
int main(int argc, char** argv) {
    std::vector<char*> args(argv, argv + argc);
    loadKeyTable(args);
    argc = int(args.size());
    argv = args.data();
    if (argc < 2) return usage();
    const std::string cmd = argv[1];
    if (cmd == "iw8-ents") return cmdIw8Ents(argc, argv);
    if (cmd == "iw8-classes") return cmdIw8Classes(argc, argv);
    if (cmd == "iw8d-ents") return cmdIw8dEnts(argc, argv, false);
    if (cmd == "iw8d-set-ents") return cmdIw8dEnts(argc, argv, true);
    if (cmd == "info" && argc == 3) return cmdInfo(argv[2]);
    if (cmd == "roundtrip" && (argc == 3 || (argc == 5 && std::string(argv[3]) == "-o"))) return cmdRoundtrip(argv[2], argc == 5 ? argv[4] : "");
    if (cmd == "diff" && argc == 4) return cmdDiff(argv[2], argv[3]);
    if (cmd == "edit") return cmdEdit(argc, argv);
    if (cmd == "compile") return cmdCompileIw8(argc, argv);
    if (cmd == "shape-obj") {
        // iw8r shape-obj <cube|sphere|...|all> <material> <out dir>: writes iw8r_shape_<id>_<hash>.obj files.
        if (argc < 5) {
            std::fprintf(stderr, "usage: iw8r shape-obj <shape|all> <material> <out dir>\n");
            return 2;
        }
        const std::string which = argv[2], material = argv[3], dir = argv[4];
        int written = 0;
        for (const auto& sh : iw8r::shapeKinds()) {
            if (which != "all" && which != sh.id) continue;
            const std::string path = dir + "/" + iw8r::shapeModelName(sh.kind, material) + ".obj";
            std::string err;
            if (!iw8r::writeFileAtomic(path, iw8r::shapeObj(sh.kind, material), &err)) {
                std::fprintf(stderr, "error: %s\n", err.c_str());
                return 1;
            }
            std::printf("%s\n", path.c_str());
            ++written;
        }
        return written ? 0 : 2;
    }
    if (cmd == "iw8d-spawns") return cmdIw8dSpawns(argc, argv);
    if (cmd == "live-read") return cmdLiveRead(argc, argv);
    if (cmd == "preflight") return cmdPreflight(argc, argv);
    if (cmd == "report") return cmdReport(argc, argv);
    if (cmd == "sky-panorama") return cmdSkyPanorama(argc, argv);
    if (cmd == "sky-sun") return cmdSkySun(argc, argv);
    if (cmd == "vision-set") return cmdVisionSet(argc, argv);
    if (cmd == "generate-spawns") {
        int cmdGenerateSpawns(int argc, char** argv);  // spawngen_cmd.cpp
        return cmdGenerateSpawns(argc, argv);
    }
    if (cmd == "stock-prefabs") {
        int cmdStockPrefabs(int argc, char** argv);  // prefab_cmd.cpp
        return cmdStockPrefabs(argc, argv);
    }
    return usage();
}
