// Asset usage (roadmap 12.9). See asset_usage.h.
#include "asset_usage.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <map>
#include <set>
#include <system_error>

#include "json_lite.h"

namespace iw8r {

namespace fs = std::filesystem;

namespace {

constexpr const char* kMatSuffix = ".iw8mat.json";
constexpr const char* kFxSuffix = ".iw8fx.json";

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

bool endsWith(const std::string& s, const std::string& suffix) {
    return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

bool startsWith(const std::string& s, const std::string& prefix) {
    return s.compare(0, prefix.size(), prefix) == 0;
}

fs::path fromUtf8(const std::string& s) {
#if defined(__cpp_char8_t)
    return fs::path(std::u8string(s.begin(), s.end()));
#else
    return fs::u8path(s);
#endif
}

std::string toUtf8(const fs::path& p) {
#if defined(__cpp_char8_t)
    const std::u8string u = p.u8string();
    return std::string(u.begin(), u.end());
#else
    return p.u8string();
#endif
}

// Adds one name to a count map, skipping empty names. Names are stored in lower case.
void add(std::map<std::string, size_t>& counts, const std::string& name, size_t n = 1) {
    if (name.empty()) return;
    counts[lower(name)] += n;
}

std::vector<AssetUse> finish(const std::map<std::string, size_t>& counts) {
    std::vector<AssetUse> out;
    out.reserve(counts.size());
    for (const auto& [name, count] : counts) out.push_back({name, count});
    std::sort(out.begin(), out.end(), [](const AssetUse& a, const AssetUse& b) {
        if (a.count != b.count) return a.count > b.count;
        return a.name < b.name;
    });
    return out;
}

// Name from an iw8_ambient_oneshots value: "<min> <max> name[:weight] name[:weight] ...".
void addOneshots(std::map<std::string, size_t>& sounds, const std::string& value) {
    const std::vector<std::string> tokens = splitTokens(value);
    for (size_t i = 2; i < tokens.size(); ++i) {
        const size_t colon = tokens[i].find(':');
        add(sounds, colon == std::string::npos ? tokens[i] : tokens[i].substr(0, colon));
    }
}

// Lists the regular files under one root (empty when the root is missing).
std::vector<fs::path> listFiles(const std::string& root) {
    std::vector<fs::path> out;
    if (root.empty()) return out;
    std::error_code ec;
    const fs::path r = fromUtf8(root);
    if (!fs::is_directory(r, ec)) return out;
    for (fs::recursive_directory_iterator it(r, fs::directory_options::skip_permission_denied, ec), end;
         !ec && it != end; it.increment(ec)) {
        std::error_code fec;
        if (it->is_regular_file(fec)) out.push_back(it->path());
    }
    return out;
}

// Name from the JSON "name" field, or the file name without the suffix.
std::string materialName(const fs::path& file, const std::string& fileName) {
    std::string text;
    if (readFile(toUtf8(file), text)) {
        const auto doc = json::parse(text, nullptr);
        if (doc && doc->type == json::Value::Type::Object) {
            if (const json::Value* v = doc->get("name")) {
                if (const std::string* s = v->string(); s && !s->empty()) return *s;
            }
        }
    }
    return fileName.substr(0, fileName.size() - std::string(kMatSuffix).size());
}

void sortByPath(std::vector<OwnAssetFile>& files, size_t from) {
    std::sort(files.begin() + static_cast<std::ptrdiff_t>(from), files.end(),
              [](const OwnAssetFile& a, const OwnAssetFile& b) { return a.path < b.path; });
}

}  // namespace

MapAssetUsage collectAssetUsage(const MapFile& map) {
    std::map<std::string, size_t> materials, models, effects, sounds, prefabs;

    for (const Entity& e : map.entities) {
        const std::string cls = e.classname();

        for (const EntityItem& item : e.items) {
            if (const Primitive* prim = item.prim()) {
                if (prim->kind == PrimKind::Brush) {
                    for (const BrushFace& f : prim->faces) add(materials, f.material());
                } else if (prim->kind == PrimKind::Patch) {
                    add(materials, prim->patchMaterial);
                }
            } else if (const KeyValueLine* kv = item.kv()) {
                const std::string key = kv->name();
                if (key == "iw8r_material" || startsWith(key, "iw8r_mat:")) add(materials, kv->value);
            }
        }

        if (cls == "misc_model" || cls == "script_model" || cls == "dynamic_prop") {
            if (const std::string* m = e.value("model"); m && !m->empty() && (*m)[0] != '*') add(models, *m);
        }
        if (cls == "misc_prefab") {
            if (const std::string* m = e.value("model")) add(prefabs, *m);
        }
        if (cls == "fx_origin") {
            if (const std::string* fx = e.value("fx")) add(effects, *fx);
        }
        if (cls == "sound_emitter" || cls == "fx_origin") {
            if (const std::string* s = e.value("soundalias")) add(sounds, *s);
        }
        if (cls == "worldspawn") {
            if (const std::string* s = e.value("iw8r_ambient")) add(sounds, *s);
            if (const std::string* s = e.value("iw8r_ambient_oneshots")) addOneshots(sounds, *s);
        }
    }

    MapAssetUsage out;
    out.materials = finish(materials);
    out.models = finish(models);
    out.effects = finish(effects);
    out.sounds = finish(sounds);
    out.prefabs = finish(prefabs);
    return out;
}

std::vector<OwnAssetFile> findOwnAssetFiles(const MapAssetUsage& usage, const std::vector<std::string>& materialRoots,
                                            const std::vector<std::string>& modelRoots,
                                            const std::vector<std::string>& soundRoots,
                                            const std::vector<std::string>& fxRoots) {
    auto nameSet = [](const std::vector<AssetUse>& list) {
        std::set<std::string> s;
        for (const AssetUse& u : list) s.insert(lower(u.name));
        return s;
    };
    const std::set<std::string> matNames = nameSet(usage.materials);
    const std::set<std::string> modelNames = nameSet(usage.models);
    const std::set<std::string> soundNames = nameSet(usage.sounds);
    const std::set<std::string> fxNames = nameSet(usage.effects);

    std::vector<OwnAssetFile> out;

    // Materials.
    size_t from = out.size();
    for (const std::string& root : materialRoots) {
        for (const fs::path& p : listFiles(root)) {
            const std::string fileName = toUtf8(p.filename());
            if (!endsWith(lower(fileName), kMatSuffix)) continue;
            OwnAssetFile f;
            f.kind = "material";
            f.name = materialName(p, fileName);
            f.path = toUtf8(p);
            f.used = matNames.count(lower(f.name)) > 0;
            out.push_back(std::move(f));
        }
    }
    sortByPath(out, from);

    // Models.
    from = out.size();
    for (const std::string& root : modelRoots) {
        for (const fs::path& p : listFiles(root)) {
            const std::string ext = lower(toUtf8(p.extension()));
            if (ext != ".obj" && ext != ".glb" && ext != ".gltf") continue;
            OwnAssetFile f;
            f.kind = "model";
            f.name = toUtf8(p.stem());
            f.path = toUtf8(p);
            f.used = modelNames.count(lower(f.name)) > 0;
            out.push_back(std::move(f));
        }
    }
    sortByPath(out, from);

    // Sounds.
    from = out.size();
    for (const std::string& root : soundRoots) {
        for (const fs::path& p : listFiles(root)) {
            if (lower(toUtf8(p.extension())) != ".wav") continue;
            OwnAssetFile f;
            f.kind = "sound";
            f.name = toUtf8(p.stem());
            f.path = toUtf8(p);
            f.used = soundNames.count(lower(f.name)) > 0;
            out.push_back(std::move(f));
        }
    }
    sortByPath(out, from);

    // Effects.
    from = out.size();
    for (const std::string& root : fxRoots) {
        std::error_code ec;
        const fs::path base = fromUtf8(root);
        for (const fs::path& p : listFiles(root)) {
            const std::string fileName = toUtf8(p.filename());
            if (!endsWith(lower(fileName), kFxSuffix)) continue;
            std::string rel = toUtf8(fs::relative(p, base, ec));
            if (ec) rel = fileName;
            for (char& c : rel)
                if (c == '\\') c = '/';
            OwnAssetFile f;
            f.kind = "effect";
            f.name = rel.substr(0, rel.size() - std::string(kFxSuffix).size());
            f.path = toUtf8(p);
            f.used = fxNames.count(lower(f.name)) > 0;
            out.push_back(std::move(f));
        }
    }
    sortByPath(out, from);

    return out;
}

}  // namespace iw8r
