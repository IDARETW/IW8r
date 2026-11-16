// IW8 Radiant - incremental build support (see build_cache.h and docs/research/44_incremental_builds.md).
#include "build_cache.h"
#include <algorithm>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <sstream>
#include <variant>
#include <vector>

namespace iw8r {

namespace fs = std::filesystem;

namespace {

constexpr const char* kScriptKey = "iw8r_script";
constexpr const char* kModelKey = "model";
constexpr const char* kFingerprintHeader = "iw8r-fingerprint 1";

bool readAll(const fs::path& p, std::string& out) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return !f.bad();
}

// Temporary names next to the target: a rename within one folder is atomic, a rename across volumes is not.
std::string tempSuffix() {
    static std::atomic<uint64_t> serial{0};
    const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    return ".iw8r-tmp-" + std::to_string(now) + "-" + std::to_string(++serial);
}

// FNV-1a, 64 bit. Strings are length-prefixed so that ("ab", "c") and ("a", "bc") hash differently.
struct Fnv {
    uint64_t h = 14695981039346656037ull;
    void byte(uint8_t b) {
        h ^= b;
        h *= 1099511628211ull;
    }
    void u64(uint64_t v) {
        for (int i = 0; i < 8; ++i) byte(uint8_t(v >> (8 * i)));
    }
    void str(const std::string& s) {
        u64(s.size());
        for (unsigned char c : s) byte(c);
    }
};

bool hasBrushes(const Entity& e) {
    for (const EntityItem& item : e.items)
        if (item.isPrimitive()) return true;
    return false;
}

// Feeds one entity item into a hash. Key/values hash name and value; brushes and patches hash their full text; any
// other line hashes its text.
void feedItem(Fnv& h, const EntityItem& item) {
    if (const KeyValueLine* kv = item.kv()) {
        h.byte('k');
        h.str(kv->name());
        h.str(kv->value);
    } else if (const Primitive* p = item.prim()) {
        h.byte('p');
        h.str(p->text());
    } else if (const auto* o = std::get_if<OpaqueLine>(&item.v)) {
        h.byte('o');
        h.str(o->line.text);
    }
}

}  // namespace

bool writeIfChanged(const std::string& path, const std::string& bytes, bool* changed) {
    if (changed) *changed = false;
    const fs::path target(path);
    std::error_code ec;

    const auto size = fs::file_size(target, ec);
    if (!ec && size == bytes.size()) {
        std::string current;
        if (readAll(target, current) && current == bytes) return true;  // identical: the file is not touched
    }

    if (target.has_parent_path()) {
        fs::create_directories(target.parent_path(), ec);
        if (ec) return false;
    }

    const fs::path temp = fs::path(path + tempSuffix());
    {
        std::ofstream f(temp, std::ios::binary | std::ios::trunc);
        if (!f) return false;
        f.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        f.close();
        if (!f) {
            fs::remove(temp, ec);
            return false;
        }
    }

    fs::rename(temp, target, ec);
    if (ec) {
        // Some platforms do not replace an existing target in rename: remove it and try once more.
        std::error_code ignored;
        fs::remove(target, ignored);
        ec.clear();
        fs::rename(temp, target, ec);
        if (ec) {
            fs::remove(temp, ignored);
            return false;
        }
    }
    if (changed) *changed = true;
    return true;
}

InputFingerprint fingerprintMap(const MapFile& map) {
    Fnv world, entities, script, materials, models;
    std::set<std::string> materialNames, modelNames;
    bool scriptPresent = false;
    std::string scriptValue;

    for (const Entity& e : map.entities) {
        const bool worldBucket = e.isWorldspawn() || hasBrushes(e);

        if (worldBucket) {
            world.byte('{');
            for (const EntityItem& item : e.items) {
                if (e.isWorldspawn()) {
                    // The script key belongs to the script fingerprint, not to the world.
                    if (const KeyValueLine* kv = item.kv(); kv && kv->name() == kScriptKey) continue;
                }
                feedItem(world, item);
            }
            world.byte('}');
        } else {
            entities.byte('{');
            for (const EntityItem& item : e.items) feedItem(entities, item);
            entities.byte('}');
        }

        for (const EntityItem& item : e.items) {
            if (const Primitive* p = item.prim()) {
                for (const BrushFace& f : p->faces)
                    if (std::string m = f.material(); !m.empty()) materialNames.insert(m);
                if (p->kind == PrimKind::Patch && !p->patchMaterial.empty()) materialNames.insert(p->patchMaterial);
            }
        }
        if (const std::string* m = e.value(kModelKey); m && !m->empty()) modelNames.insert(*m);

        if (e.isWorldspawn() && !scriptPresent) {
            if (const std::string* s = e.value(kScriptKey)) {
                scriptPresent = true;
                scriptValue = *s;
            }
        }
    }

    script.byte(scriptPresent ? 1 : 0);
    script.str(scriptValue);
    for (const std::string& m : materialNames) materials.str(m);
    for (const std::string& m : modelNames) models.str(m);

    InputFingerprint fp;
    fp.world = world.h;
    fp.entities = entities.h;
    fp.script = script.h;
    fp.materials = materials.h;
    fp.models = models.h;
    return fp;
}

std::string classifyChange(const InputFingerprint& before, const InputFingerprint& after) {
    const bool geometry = before.world != after.world;
    const bool materials = before.materials != after.materials;
    const bool models = before.models != after.models;
    const bool entities = before.entities != after.entities;
    const bool script = before.script != after.script;

    if (geometry || materials || models) return "full";
    if (entities && script) return "full";
    if (entities) return "entities-only";
    if (script) return "script-only";
    return "none";
}

bool saveFingerprint(const std::string& path, const InputFingerprint& fp, std::string* err) {
    char buf[64];
    std::string text = std::string(kFingerprintHeader) + "\n";
    const auto line = [&](const char* name, uint64_t v) {
        std::snprintf(buf, sizeof buf, "%016llx", static_cast<unsigned long long>(v));
        text += std::string(name) + " " + buf + "\n";
    };
    line("world", fp.world);
    line("entities", fp.entities);
    line("script", fp.script);
    line("materials", fp.materials);
    line("models", fp.models);
    if (!writeIfChanged(path, text, nullptr)) {
        if (err) *err = "cannot write fingerprint " + path;
        return false;
    }
    return true;
}

bool loadFingerprint(const std::string& path, InputFingerprint& out, std::string* err) {
    std::string text;
    if (!readAll(fs::path(path), text)) {
        if (err) *err = "cannot read fingerprint " + path;
        return false;
    }
    std::istringstream in(text);
    std::string header;
    if (!std::getline(in, header) || header != kFingerprintHeader) {
        if (err) *err = "not a fingerprint file: " + path;
        return false;
    }
    InputFingerprint fp;
    std::set<std::string> seen;
    std::string name, hex;
    while (in >> name >> hex) {
        char* end = nullptr;
        const unsigned long long v = std::strtoull(hex.c_str(), &end, 16);
        if (hex.size() != 16 || !end || *end != '\0') {
            if (err) *err = "bad value for " + name + " in " + path;
            return false;
        }
        if (name == "world") fp.world = v;
        else if (name == "entities") fp.entities = v;
        else if (name == "script") fp.script = v;
        else if (name == "materials") fp.materials = v;
        else if (name == "models") fp.models = v;
        else {
            if (err) *err = "unknown field " + name + " in " + path;
            return false;
        }
        seen.insert(name);
    }
    if (seen.size() != 5) {
        if (err) *err = "incomplete fingerprint " + path;
        return false;
    }
    out = fp;
    return true;
}

uint64_t hashDirectory(const std::string& dir) {
    namespace fs = std::filesystem;
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) return 0;
    std::vector<std::pair<std::string, fs::path>> files;
    for (fs::recursive_directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec))
        if (it->is_regular_file(ec)) {
            std::string rel = fs::relative(it->path(), dir, ec).generic_string();
            files.push_back({rel, it->path()});
        }
    std::sort(files.begin(), files.end());
    uint64_t h = 1469598103934665603ull;  // FNV-1a 64
    const auto mix = [&](const char* p, size_t n) {
        for (size_t i = 0; i < n; ++i) h = (h ^ static_cast<unsigned char>(p[i])) * 1099511628211ull;
    };
    std::vector<char> buf(1 << 16);
    for (const auto& [rel, path] : files) {
        mix(rel.data(), rel.size() + 1);  // with the terminating NUL as a separator
        std::FILE* f = std::fopen(path.string().c_str(), "rb");
        if (!f) continue;
        size_t got;
        while ((got = std::fread(buf.data(), 1, buf.size(), f)) > 0) mix(buf.data(), got);
        std::fclose(f);
        mix("ÿ", 1);
    }
    return h ? h : 1;
}

}  // namespace iw8r
