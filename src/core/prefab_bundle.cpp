// Prefab bundles. See prefab_bundle.h and docs/PREFAB_BUNDLES.md.
#include "prefab_bundle.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <system_error>

#include "asset_usage.h"
#include "compile_iw8.h"
#include "model_import.h"
#include "prefab.h"
#include "script_check.h"

namespace iw8r {

namespace fs = std::filesystem;

namespace {

using json::Value;
using Type = json::Value::Type;

std::string lowerText(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

bool endsWithCi(const std::string& s, const std::string& suffix) {
    return s.size() >= suffix.size() && lowerText(s.substr(s.size() - suffix.size())) == lowerText(suffix);
}

bool isFile(const fs::path& p) {
    std::error_code ec;
    return fs::is_regular_file(p, ec) && !ec;
}

bool isDir(const fs::path& p) {
    std::error_code ec;
    return fs::is_directory(p, ec) && !ec;
}

std::string joinList(const std::vector<std::string>& items, const char* sep = ", ") {
    std::string out;
    for (const std::string& s : items) out += (out.empty() ? "" : sep) + s;
    return out;
}

void addUnique(std::vector<std::string>& list, const std::string& s) {
    if (!s.empty() && std::find(list.begin(), list.end(), s) == list.end()) list.push_back(s);
}

// ---------------------------------------------------------------- JSON values and text

Value textValue(const std::string& s) {
    Value v;
    v.type = Type::String;
    v.str = s;
    return v;
}
Value numberValue(double d) {
    Value v;
    v.type = Type::Number;
    v.num = d;
    return v;
}
Value boolValue(bool b) {
    Value v;
    v.type = Type::Bool;
    v.b = b;
    return v;
}
Value objectValue() {
    Value v;
    v.type = Type::Object;
    return v;
}
Value listValue(const std::vector<std::string>& items) {
    Value v;
    v.type = Type::Array;
    for (const std::string& s : items) v.arr.push_back(textValue(s));
    return v;
}

Value* member(Value& obj, const std::string& key) {
    for (auto& [k, v] : obj.obj)
        if (k == key) return &v;
    return nullptr;
}
void setMember(Value& obj, const std::string& key, Value v) {
    if (Value* have = member(obj, key)) *have = std::move(v);
    else obj.obj.emplace_back(key, std::move(v));
}
void eraseMember(Value& obj, const std::string& key) {
    obj.obj.erase(std::remove_if(obj.obj.begin(), obj.obj.end(), [&](const auto& kv) { return kv.first == key; }), obj.obj.end());
}
// A value that equals its default is written only when the file already had the field.
void putText(Value& obj, const std::string& key, const std::string& v, const std::string& def = {}) {
    if (v != def) setMember(obj, key, textValue(v));
    else if (member(obj, key)) {
        if (v.empty()) eraseMember(obj, key);
        else setMember(obj, key, textValue(v));
    }
}
void putNumber(Value& obj, const std::string& key, double v, double def) {
    if (v != def || member(obj, key)) setMember(obj, key, numberValue(v));
}
void putList(Value& obj, const std::string& key, const std::vector<std::string>& items, bool always = false) {
    if (!items.empty() || always || member(obj, key)) setMember(obj, key, listValue(items));
}
// The object member `key` of the document as it was read (or an empty object), to be filled and put back.
Value subObject(const Value& doc, const std::string& key) {
    const Value* have = doc.get(key);
    return have && have->type == Type::Object ? *have : objectValue();
}
void putObject(Value& doc, const std::string& key, Value sub) {
    if (!sub.obj.empty() || member(doc, key)) setMember(doc, key, std::move(sub));
}
// The element of the list the file had under `key` whose `idField` is `id` (so its unknown fields survive), else {}.
Value itemFrom(const Value& parent, const std::string& key, const char* idField, const std::string& id) {
    if (const Value* list = parent.get(key); list && list->type == Type::Array)
        for (const Value& it : list->arr)
            if (const Value* n = it.get(idField); n && n->string() && *n->string() == id && it.type == Type::Object) return it;
    return objectValue();
}

void dumpString(const std::string& s, std::string& out) {
    out += '"';
    for (unsigned char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    static const char* hex = "0123456789abcdef";
                    out += "\\u00";
                    out += hex[c >> 4];
                    out += hex[c & 15];
                } else {
                    out += static_cast<char>(c);
                }
        }
    }
    out += '"';
}

bool isScalar(const Value& v) { return v.type != Type::Array && v.type != Type::Object; }
// Small values stay on one line: scalars, lists of scalars, and objects that hold only those.
bool isInline(const Value& v, int depth = 0) {
    if (isScalar(v)) return true;
    if (depth >= 2) return false;
    if (v.type == Type::Array) {
        for (const Value& e : v.arr)
            if (!isScalar(e)) return false;
        return true;
    }
    return false;
}

void dumpValue(const Value& v, int indent, std::string& out, bool forceInline = false) {
    const std::string pad(static_cast<size_t>(indent) * 2, ' ');
    switch (v.type) {
        case Type::Null: out += "null"; return;
        case Type::Bool: out += v.b ? "true" : "false"; return;
        case Type::Number: out += std::isfinite(v.num) ? formatNumber(v.num) : std::string("0"); return;
        case Type::String: dumpString(v.str, out); return;
        case Type::Array: {
            if (v.arr.empty()) {
                out += "[]";
                return;
            }
            if (forceInline || isInline(v)) {
                out += '[';
                for (size_t i = 0; i < v.arr.size(); ++i) {
                    if (i) out += ", ";
                    dumpValue(v.arr[i], indent, out, true);
                }
                out += ']';
                return;
            }
            out += "[\n";
            for (size_t i = 0; i < v.arr.size(); ++i) {
                out += pad + "  ";
                // An object whose members are all small goes on one line (a list of {name, zones}).
                bool flat = v.arr[i].type == Type::Object;
                if (flat)
                    for (const auto& kv : v.arr[i].obj) flat &= isInline(kv.second);
                dumpValue(v.arr[i], indent + 1, out, flat);
                out += i + 1 < v.arr.size() ? ",\n" : "\n";
            }
            out += pad + "]";
            return;
        }
        case Type::Object: {
            if (v.obj.empty()) {
                out += "{}";
                return;
            }
            if (forceInline) {
                out += '{';
                for (size_t i = 0; i < v.obj.size(); ++i) {
                    if (i) out += ", ";
                    dumpString(v.obj[i].first, out);
                    out += ": ";
                    dumpValue(v.obj[i].second, indent, out, true);
                }
                out += '}';
                return;
            }
            out += "{\n";
            for (size_t i = 0; i < v.obj.size(); ++i) {
                out += pad + "  ";
                dumpString(v.obj[i].first, out);
                out += ": ";
                dumpValue(v.obj[i].second, indent + 1, out);
                out += i + 1 < v.obj.size() ? ",\n" : "\n";
            }
            out += pad + "}";
            return;
        }
    }
}

std::string dumpJson(const Value& v) {
    std::string out;
    dumpValue(v, 0, out);
    out += "\n";
    return out;
}

// ---------------------------------------------------------------- manifest reading

const char* const kSurfaces[] = {"floor", "wall", "ceiling", "free"};
const char* const kParameterTypes[] = {"number", "choice", "material", "text"};

// A file named by the manifest must lie inside the bundle folder.
bool isBundleFileName(const std::string& name) {
    if (name.empty() || name[0] == '/' || name[0] == '\\' || name.find(':') != std::string::npos) return false;
    std::string part;
    for (size_t i = 0; i <= name.size(); ++i) {
        if (i == name.size() || name[i] == '/' || name[i] == '\\') {
            if (part == "..") return false;
            part.clear();
        } else {
            part += name[i];
        }
    }
    return true;
}

bool isIdentifier(const std::string& s) {
    if (s.empty() || std::isdigit(static_cast<unsigned char>(s[0]))) return false;
    for (char c : s)
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_')) return false;
    return true;
}

struct Reader {
    std::string err;
    bool fail(const std::string& field, const std::string& what) {
        if (err.empty()) err = std::string(kPrefabManifestFile) + ": '" + field + "' " + what;
        return false;
    }
    // Each reader returns true when the field is absent or well formed.
    bool text(const Value& obj, const char* key, const std::string& path, std::string& out) {
        const Value* v = obj.get(key);
        if (!v) return true;
        if (!v->string()) return fail(path, "must be text");
        out = *v->string();
        return true;
    }
    bool file(const Value& obj, const char* key, const std::string& path, std::string& out) {
        const Value* v = obj.get(key);
        if (!v) return true;
        if (!v->string()) return fail(path, "must be a file name (text)");
        if (!isBundleFileName(*v->string())) return fail(path, "\"" + *v->string() + "\" must be a file inside the bundle folder");
        out = *v->string();
        std::replace(out.begin(), out.end(), '\\', '/');
        return true;
    }
    bool number(const Value& obj, const char* key, const std::string& path, double& out, bool* present = nullptr) {
        const Value* v = obj.get(key);
        if (present) *present = v != nullptr;
        if (!v) return true;
        if (!v->number() || !std::isfinite(v->num)) return fail(path, "must be a number");
        out = v->num;
        return true;
    }
    bool textList(const Value& obj, const char* key, const std::string& path, std::vector<std::string>& out, bool* present = nullptr) {
        const Value* v = obj.get(key);
        if (present) *present = v != nullptr;
        if (!v) return true;
        if (v->type != Type::Array) return fail(path, "must be a list of text");
        out.clear();
        for (const Value& e : v->arr) {
            if (!e.string() || e.string()->empty()) return fail(path, "must be a list of text (every item a non-empty text)");
            out.push_back(*e.string());
        }
        return true;
    }
    // A sub-object: null when absent, else it must be an object.
    bool object(const Value& obj, const char* key, const Value*& out) {
        out = obj.get(key);
        if (out && out->type != Type::Object) {
            out = nullptr;
            return fail(key, "must be an object");
        }
        return true;
    }
    bool list(const Value& obj, const char* key, const std::string& path, const Value*& out) {
        out = obj.get(key);
        if (out && out->type != Type::Array) {
            out = nullptr;
            return fail(path, "must be a list");
        }
        return true;
    }
    bool stockRefs(const Value& obj, const char* key, const std::string& path, std::vector<PrefabStockRef>& out) {
        const Value* l = nullptr;
        if (!list(obj, key, path, l)) return false;
        if (!l) return true;
        for (size_t i = 0; i < l->arr.size(); ++i) {
            const Value& e = l->arr[i];
            const std::string at = path + "[" + std::to_string(i) + "]";
            if (e.type != Type::Object) return fail(at, "must be an object {name, zones}");
            PrefabStockRef r;
            if (!text(e, "name", at + ".name", r.name)) return false;
            if (r.name.empty()) return fail(at + ".name", "is missing");
            if (!textList(e, "zones", at + ".zones", r.zones)) return false;
            out.push_back(std::move(r));
        }
        return true;
    }
    bool assetFiles(const Value& obj, const char* key, const std::string& path, std::vector<PrefabAssetFile>& out) {
        const Value* l = nullptr;
        if (!list(obj, key, path, l)) return false;
        if (!l) return true;
        for (size_t i = 0; i < l->arr.size(); ++i) {
            const Value& e = l->arr[i];
            const std::string at = path + "[" + std::to_string(i) + "]";
            if (e.type != Type::Object) return fail(at, "must be an object {name, file}");
            PrefabAssetFile a;
            if (!text(e, "name", at + ".name", a.name) || !file(e, "file", at + ".file", a.file)) return false;
            if (a.name.empty()) return fail(at + ".name", "is missing");
            if (a.file.empty()) return fail(at + ".file", "is missing");
            out.push_back(std::move(a));
        }
        return true;
    }
};

bool readParameter(Reader& r, const Value& e, const std::string& at, PrefabParameter& p) {
    if (e.type != Type::Object) return r.fail(at, "must be an object");
    if (!r.text(e, "name", at + ".name", p.name) || !r.text(e, "label", at + ".label", p.label) ||
        !r.text(e, "type", at + ".type", p.type))
        return false;
    if (!isIdentifier(p.name)) return r.fail(at + ".name", "must be a name of letters, digits and _ (it is the {name} of the template)");
    if (std::find(std::begin(kParameterTypes), std::end(kParameterTypes), p.type) == std::end(kParameterTypes))
        return r.fail(at + ".type", "must be one of number, choice, material, text (found \"" + p.type + "\")");
    if (const Value* d = e.get("default")) {
        if (d->string()) p.defaultValue = *d->string();
        else if (d->number()) p.defaultValue = formatNumber(d->num);
        else if (d->type == Type::Bool) p.defaultValue = d->b ? "1" : "0";
        else return r.fail(at + ".default", "must be text or a number");
    }
    double v = 0;
    bool have = false;
    if (!r.number(e, "min", at + ".min", v, &have)) return false;
    if (have) p.min = v;
    if (!r.number(e, "max", at + ".max", v, &have)) return false;
    if (have) p.max = v;
    if (p.min && p.max && *p.min > *p.max) return r.fail(at + ".min", "is larger than max");
    if (!r.textList(e, "choices", at + ".choices", p.choices)) return false;
    if (p.type == "choice" && p.choices.empty()) return r.fail(at + ".choices", "is missing (a choice needs its list)");
    const Value* apply = e.get("apply");
    if (!apply || apply->type != Type::Object)
        return r.fail(at + ".apply", "is missing: it is {selector, key, template} or {material}");
    if (apply->get("material")) {
        if (!r.text(*apply, "material", at + ".apply.material", p.material)) return false;
        if (p.material.empty()) return r.fail(at + ".apply.material", "must name the material to replace");
        return true;
    }
    p.valueTemplate = "{" + p.name + "}";
    if (!r.text(*apply, "selector", at + ".apply.selector", p.selector) || !r.text(*apply, "key", at + ".apply.key", p.key) ||
        !r.text(*apply, "template", at + ".apply.template", p.valueTemplate))
        return false;
    if (p.selector.empty() || p.key.empty())
        return r.fail(at + ".apply", "needs selector and key (or material)");
    if (p.key == "origin" || p.key == "angles" || p.key == "classname")
        return r.fail(at + ".apply.key", "cannot be origin, angles or classname (the placement sets those)");
    return true;
}

bool readManifest(const Value& doc, PrefabManifest& m, Reader& r) {
    // format and id come first: nothing else is trusted without them.
    const Value* format = doc.get("format");
    if (!format) return r.fail("format", std::string("is missing (it must be \"") + kPrefabBundleFormat + "\")");
    if (!format->string()) return r.fail("format", "must be text");
    m.format = *format->string();
    const Value* id = doc.get("id");
    if (!id) return r.fail("id", "is missing (1-40 characters of a-z 0-9 _)");
    if (!id->string()) return r.fail("id", "must be text");
    m.id = *id->string();
    if (!isValidPrefabId(m.id)) return r.fail("id", "\"" + m.id + "\" must be 1-40 characters of a-z 0-9 _");
    if (!r.text(doc, "title", "title", m.title) || !r.text(doc, "description", "description", m.description)) return false;
    if (m.format != kPrefabBundleFormat) return true;  // listed only: the other fields may mean something else

    if (!r.text(doc, "category", "category", m.category) || !r.textList(doc, "tags", "tags", m.tags) ||
        !r.text(doc, "author", "author", m.author) || !r.file(doc, "map", "map", m.map) ||
        !r.file(doc, "thumbnail", "thumbnail", m.thumbnail) || !r.text(doc, "pivot", "pivot", m.pivot) ||
        !r.textList(doc, "game_modes", "game_modes", m.gameModes))
        return false;
    {
        double v = 0;
        bool have = false;
        if (!r.number(doc, "version", "version", v, &have)) return false;
        if (have) m.version = v;
    }
    const Value* sub = nullptr;
    if (!r.object(doc, "bounds", sub)) return false;
    if (sub) {
        AABB b;
        for (const char* key : {"mins", "maxs"}) {
            const Value* v = sub->get(key);
            if (!v || v->type != Type::Array || v->arr.size() != 3 || !v->arr[0].number() || !v->arr[1].number() || !v->arr[2].number())
                return r.fail(std::string("bounds.") + key, "must be a list of three numbers");
            (std::string(key) == "mins" ? b.mins : b.maxs) = Vec3{v->arr[0].num, v->arr[1].num, v->arr[2].num};
        }
        if (!b.valid()) return r.fail("bounds", "has mins larger than maxs");
        m.bounds = b;
    }
    if (!r.object(doc, "placement", sub)) return false;
    if (sub) {
        if (!r.text(*sub, "surface", "placement.surface", m.surface) || !r.number(*sub, "snap", "placement.snap", m.snap) ||
            !r.number(*sub, "yaw_step", "placement.yaw_step", m.yawStep))
            return false;
        if (std::find_if(std::begin(kSurfaces), std::end(kSurfaces), [&](const char* s) { return m.surface == s; }) == std::end(kSurfaces))
            return r.fail("placement.surface", "must be one of floor, wall, ceiling, free (found \"" + m.surface + "\")");
        if (m.snap < 0) return r.fail("placement.snap", "must not be negative");
        if (m.yawStep <= 0 || m.yawStep > 360) return r.fail("placement.yaw_step", "must be more than 0 and at most 360 degrees");
    }
    if (!r.object(doc, "names", sub)) return false;
    if (sub) {
        if (!r.textList(*sub, "local", "names.local", m.namesLocal, &m.hasLocalNames) ||
            !r.textList(*sub, "global", "names.global", m.namesGlobal) || !r.textList(*sub, "keys", "names.keys", m.nameKeys))
            return false;
        for (const std::string& n : m.namesLocal)
            if (std::find(m.namesGlobal.begin(), m.namesGlobal.end(), n) != m.namesGlobal.end())
                return r.fail("names.local", "and names.global both list \"" + n + "\"");
    }
    const Value* params = nullptr;
    if (!r.list(doc, "parameters", "parameters", params)) return false;
    if (params)
        for (size_t i = 0; i < params->arr.size(); ++i) {
            PrefabParameter p;
            if (!readParameter(r, params->arr[i], "parameters[" + std::to_string(i) + "]", p)) return false;
            for (const PrefabParameter& have : m.parameters)
                if (have.name == p.name) return r.fail("parameters[" + std::to_string(i) + "].name", "\"" + p.name + "\" is used twice");
            m.parameters.push_back(std::move(p));
        }
    if (!r.object(doc, "script", sub)) return false;
    if (sub) {
        if (!r.file(*sub, "file", "script.file", m.scriptFile) || !r.text(*sub, "prefix", "script.prefix", m.scriptPrefix) ||
            !r.text(*sub, "entry", "script.entry", m.scriptEntry) || !r.text(*sub, "once", "script.once", m.scriptOnce))
            return false;
        if (!isIdentifier(m.scriptPrefix)) return r.fail("script.prefix", "must be letters, digits and _ (for example prefab_)");
        for (const auto& [field, name] : {std::pair<const char*, const std::string*>{"script.entry", &m.scriptEntry},
                                          std::pair<const char*, const std::string*>{"script.once", &m.scriptOnce}}) {
            if (name->empty() && std::string(field) == "script.once") continue;
            if (!isIdentifier(*name) || name->size() <= m.scriptPrefix.size() || name->compare(0, m.scriptPrefix.size(), m.scriptPrefix) != 0)
                return r.fail(field, "\"" + *name + "\" must be a function name that starts with the prefix \"" + m.scriptPrefix + "\"");
        }
    }
    if (!r.object(doc, "assets", sub)) return false;
    if (sub) {
        if (!r.assetFiles(*sub, "materials", "assets.materials", m.materials) || !r.assetFiles(*sub, "models", "assets.models", m.models) ||
            !r.assetFiles(*sub, "effects", "assets.effects", m.effects))
            return false;
        const Value* sounds = nullptr;
        if (!r.list(*sub, "sounds", "assets.sounds", sounds)) return false;
        if (sounds)
            for (size_t i = 0; i < sounds->arr.size(); ++i) {
                const Value& e = sounds->arr[i];
                const std::string at = "assets.sounds[" + std::to_string(i) + "]";
                if (e.type != Type::Object) return r.fail(at, "must be an object {alias, file, loop}");
                PrefabSoundFile s;
                if (!r.text(e, "alias", at + ".alias", s.alias) || !r.file(e, "file", at + ".file", s.file)) return false;
                if (s.alias.empty()) return r.fail(at + ".alias", "is missing");
                if (s.file.empty()) return r.fail(at + ".file", "is missing");
                if (const Value* l = e.get("loop")) {
                    if (l->type != Type::Bool) return r.fail(at + ".loop", "must be true or false");
                    s.loop = l->b;
                }
                m.sounds.push_back(std::move(s));
            }
    }
    if (!r.object(doc, "requires", sub)) return false;
    if (sub) {
        if (!r.stockRefs(*sub, "stock_models", "requires.stock_models", m.stockModels) ||
            !r.stockRefs(*sub, "stock_materials", "requires.stock_materials", m.stockMaterials) ||
            !r.stockRefs(*sub, "scriptable_defs", "requires.scriptable_defs", m.scriptableDefs) ||
            !r.textList(*sub, "stock_sounds", "requires.stock_sounds", m.stockSounds) ||
            !r.textList(*sub, "stock_effects", "requires.stock_effects", m.stockEffects) ||
            !r.textList(*sub, "stock_strings", "requires.stock_strings", m.stockStrings) ||
            !r.textList(*sub, "stock_scripts", "requires.stock_scripts", m.stockScripts) ||
            !r.textList(*sub, "build", "requires.build", m.buildFeatures))
            return false;
    }
    if (!r.object(doc, "fallback", sub)) return false;
    if (sub) {
        if (!r.textList(*sub, "when_missing", "fallback.when_missing", m.fallbackWhenMissing) ||
            !r.file(*sub, "map", "fallback.map", m.fallbackMap) || !r.file(*sub, "script", "fallback.script", m.fallbackScript) ||
            !r.text(*sub, "note", "fallback.note", m.fallbackNote))
            return false;
        if (m.fallbackMap.empty()) return r.fail("fallback.map", "is missing (a fallback needs its own contents file)");
        if (m.fallbackWhenMissing.empty())
            return r.fail("fallback.when_missing", "is missing (the build features whose absence selects the fallback)");
    }
    return true;
}

std::string bundleLabel(const fs::path& folder) { return folder.filename().string(); }

// The model's folder and file candidates, in the order resolvePrefabPath has always used.
std::vector<fs::path> prefabCandidates(const std::string& mapPath, const fs::path& m, const std::vector<std::string>& searchRoots) {
    std::vector<fs::path> cands;
    if (!mapPath.empty()) {
        const fs::path dir = fs::path(mapPath).parent_path();
        cands.push_back(dir / m);
        for (fs::path d = dir; !d.empty();) {
            if (d.filename() == "map_source") cands.push_back(d / m);  // the map lives under map_source
            cands.push_back(d / "map_source" / m);
            const fs::path up = d.parent_path();
            if (up == d) break;
            d = up;
        }
    }
    for (const std::string& root : searchRoots) {
        if (root.empty()) continue;
        cands.push_back(fs::path(root) / m);
        cands.push_back(fs::path(root) / "map_source" / m);
    }
    return cands;
}

bool hasBundleSuffix(const fs::path& p) { return endsWithCi(p.filename().string(), kPrefabBundleSuffix); }

// One spelling per bundle folder, however it was reached ("room.iw8prefab/../door.iw8prefab", another letter case),
// so the build knows two placements name the same prefab kind.
std::string oneSpelling(const fs::path& folder) {
    std::error_code ec;
    const fs::path real = fs::weakly_canonical(folder, ec);
    return (ec || real.empty() ? folder.lexically_normal() : real).string();
}

// What one existing path is: a bundle folder, the contents file of one, or a plain .map.
PrefabRef refOf(const fs::path& c) {
    PrefabRef r;
    if (isFile(c)) {
        const fs::path parent = c.parent_path();
        if (hasBundleSuffix(parent) && isFile(parent / kPrefabManifestFile)) {
            const std::string contents = prefabBundleContentsFile(parent.string());
            std::error_code ec;
            if (!contents.empty() && fs::equivalent(fs::path(contents), c, ec) && !ec) {
                r.kind = PrefabRef::Kind::Bundle;
                r.path = oneSpelling(parent);
                r.mapFile = contents;
                return r;
            }
        }
        r.kind = PrefabRef::Kind::BareMap;
        r.path = r.mapFile = c.string();
    } else if (hasBundleSuffix(c) && isDir(c)) {
        r.kind = PrefabRef::Kind::Bundle;
        r.path = oneSpelling(c);
        r.mapFile = prefabBundleContentsFile(r.path);
    }
    return r;
}

std::vector<std::string> entityNames(const Entity& e, const std::string& key) {
    const std::string* v = e.value(key);
    return v ? splitTokens(*v) : std::vector<std::string>{};
}

bool readText(const fs::path& p, std::string& out) { return readFile(p.string(), out); }

}  // namespace

// ------------------------------------------------------------------ manifest

bool isValidPrefabId(const std::string& id) {
    if (id.empty() || id.size() > 40) return false;
    for (char c : id)
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) return false;
    return true;
}

std::string prefabIdFromName(const std::string& name) {
    std::string id;
    for (unsigned char c : name) {
        if (std::isalnum(c) && c < 0x80) id += static_cast<char>(std::tolower(c));
        else if (!id.empty() && id.back() != '_') id += '_';
    }
    while (!id.empty() && id.back() == '_') id.pop_back();
    if (id.size() > 40) id.resize(40);
    while (!id.empty() && id.back() == '_') id.pop_back();
    return id.empty() ? "prefab" : id;
}

bool parsePrefabManifest(const std::string& jsonText, PrefabManifest& out, std::string* err) {
    out = PrefabManifest{};
    std::string perr;
    auto doc = json::parse(jsonText, &perr);
    if (!doc || doc->type != Type::Object) {
        if (err) *err = std::string(kPrefabManifestFile) + ": not a JSON object" + (perr.empty() ? "" : " (" + perr + ")");
        return false;
    }
    Reader r;
    const bool ok = readManifest(*doc, out, r);
    out.raw = std::move(*doc);
    if (!ok && err) *err = r.err;
    return ok;
}

std::string prefabManifestText(const PrefabManifest& m) {
    Value doc = m.raw.type == Type::Object ? m.raw : objectValue();
    // format and id lead a new file.
    setMember(doc, "format", textValue(m.format));
    setMember(doc, "id", textValue(m.id));
    putText(doc, "title", m.title);
    putText(doc, "description", m.description);
    putText(doc, "category", m.category);
    putList(doc, "tags", m.tags);
    putText(doc, "author", m.author);
    if (m.version) setMember(doc, "version", numberValue(*m.version));
    else eraseMember(doc, "version");
    putText(doc, "map", m.map);
    putText(doc, "thumbnail", m.thumbnail);
    putText(doc, "pivot", m.pivot);
    if (m.bounds) {
        Value b = subObject(doc, "bounds");
        const auto vec = [](const Vec3& v) {
            Value a;
            a.type = Type::Array;
            for (double d : {v.x, v.y, v.z}) a.arr.push_back(numberValue(d));
            return a;
        };
        setMember(b, "mins", vec(m.bounds->mins));
        setMember(b, "maxs", vec(m.bounds->maxs));
        setMember(doc, "bounds", std::move(b));
    } else {
        eraseMember(doc, "bounds");
    }
    {
        Value p = subObject(doc, "placement");
        putText(p, "surface", m.surface, "floor");
        putNumber(p, "snap", m.snap, 0);
        putNumber(p, "yaw_step", m.yawStep, 90);
        putObject(doc, "placement", std::move(p));
    }
    {
        Value n = subObject(doc, "names");
        if (m.hasLocalNames) putList(n, "local", m.namesLocal, true);
        else eraseMember(n, "local");
        putList(n, "global", m.namesGlobal);
        putList(n, "keys", m.nameKeys);
        putObject(doc, "names", std::move(n));
    }
    if (!m.parameters.empty() || member(doc, "parameters")) {
        Value list;
        list.type = Type::Array;
        for (const PrefabParameter& p : m.parameters) {
            Value e = itemFrom(doc, "parameters", "name", p.name);
            setMember(e, "name", textValue(p.name));
            putText(e, "label", p.label);
            setMember(e, "type", textValue(p.type));
            if (!p.defaultValue.empty()) {
                char* end = nullptr;
                const double d = std::strtod(p.defaultValue.c_str(), &end);
                const bool numeric = p.type == "number" && end == p.defaultValue.c_str() + p.defaultValue.size();
                setMember(e, "default", numeric ? numberValue(d) : textValue(p.defaultValue));
            } else {
                eraseMember(e, "default");
            }
            if (p.min) setMember(e, "min", numberValue(*p.min));
            else eraseMember(e, "min");
            if (p.max) setMember(e, "max", numberValue(*p.max));
            else eraseMember(e, "max");
            putList(e, "choices", p.choices);
            Value apply = subObject(e, "apply");
            if (!p.material.empty()) {
                apply = objectValue();
                setMember(apply, "material", textValue(p.material));
            } else {
                eraseMember(apply, "material");
                setMember(apply, "selector", textValue(p.selector));
                setMember(apply, "key", textValue(p.key));
                setMember(apply, "template", textValue(p.valueTemplate));
            }
            setMember(e, "apply", std::move(apply));
            list.arr.push_back(std::move(e));
        }
        setMember(doc, "parameters", std::move(list));
    }
    {
        Value s = subObject(doc, "script");
        putText(s, "file", m.scriptFile);
        putText(s, "entry", m.scriptEntry, "prefab_main");
        putText(s, "prefix", m.scriptPrefix, "prefab_");
        putText(s, "once", m.scriptOnce);
        putObject(doc, "script", std::move(s));
    }
    const auto assetList = [](Value& parent, const char* key, const std::vector<PrefabAssetFile>& items) {
        if (items.empty() && !member(parent, key)) return;
        Value list;
        list.type = Type::Array;
        for (const PrefabAssetFile& a : items) {
            Value e = itemFrom(parent, key, "name", a.name);
            setMember(e, "name", textValue(a.name));
            setMember(e, "file", textValue(a.file));
            list.arr.push_back(std::move(e));
        }
        setMember(parent, key, std::move(list));
    };
    const auto refList = [](Value& parent, const char* key, const std::vector<PrefabStockRef>& items) {
        if (items.empty() && !member(parent, key)) return;
        Value list;
        list.type = Type::Array;
        for (const PrefabStockRef& a : items) {
            Value e = itemFrom(parent, key, "name", a.name);
            setMember(e, "name", textValue(a.name));
            putList(e, "zones", a.zones);
            list.arr.push_back(std::move(e));
        }
        setMember(parent, key, std::move(list));
    };
    {
        Value a = subObject(doc, "assets");
        assetList(a, "materials", m.materials);
        assetList(a, "models", m.models);
        if (!m.sounds.empty() || member(a, "sounds")) {
            Value list;
            list.type = Type::Array;
            for (const PrefabSoundFile& s : m.sounds) {
                Value e = itemFrom(a, "sounds", "alias", s.alias);
                setMember(e, "alias", textValue(s.alias));
                setMember(e, "file", textValue(s.file));
                if (s.loop || member(e, "loop")) setMember(e, "loop", boolValue(s.loop));
                list.arr.push_back(std::move(e));
            }
            setMember(a, "sounds", std::move(list));
        }
        assetList(a, "effects", m.effects);
        putObject(doc, "assets", std::move(a));
    }
    {
        Value q = subObject(doc, "requires");
        refList(q, "scriptable_defs", m.scriptableDefs);
        refList(q, "stock_models", m.stockModels);
        refList(q, "stock_materials", m.stockMaterials);
        putList(q, "stock_sounds", m.stockSounds);
        putList(q, "stock_effects", m.stockEffects);
        putList(q, "stock_strings", m.stockStrings);
        putList(q, "stock_scripts", m.stockScripts);
        putList(q, "build", m.buildFeatures);
        putObject(doc, "requires", std::move(q));
    }
    {
        Value f = subObject(doc, "fallback");
        putList(f, "when_missing", m.fallbackWhenMissing);
        putText(f, "map", m.fallbackMap);
        putText(f, "script", m.fallbackScript);
        putText(f, "note", m.fallbackNote);
        putObject(doc, "fallback", std::move(f));
    }
    putList(doc, "game_modes", m.gameModes);
    return dumpJson(doc);
}

std::pair<std::string, std::string> prefabParameterKey(const PrefabParameter& p, const std::string& value) {
    if (!p.material.empty()) return {std::string(kPrefabMaterialSwapPrefix) + p.material, value};
    std::string v = p.valueTemplate;
    const std::string hole = "{" + p.name + "}";
    for (size_t at = v.find(hole); at != std::string::npos; at = v.find(hole, at + value.size())) v.replace(at, hole.size(), value);
    return {std::string(kPrefabKeyOverridePrefix) + p.selector + ":" + p.key, v};
}

std::optional<std::string> prefabParameterValue(const PrefabParameter& p, const Entity& placement) {
    const std::string* stored = placement.value(prefabParameterKey(p, "").first);
    if (!stored) return std::nullopt;
    if (!p.material.empty()) return *stored;
    const std::string hole = "{" + p.name + "}";
    const size_t at = p.valueTemplate.find(hole);
    if (at == std::string::npos) return std::nullopt;  // the template does not use the value
    const std::string before = p.valueTemplate.substr(0, at);
    std::string after = p.valueTemplate.substr(at + hole.size());
    const size_t again = after.find(hole);
    if (stored->compare(0, before.size(), before) != 0) return std::nullopt;
    std::string value;
    if (again == std::string::npos) {
        if (stored->size() < before.size() + after.size() || stored->compare(stored->size() - after.size(), after.size(), after) != 0)
            return std::nullopt;
        value = stored->substr(before.size(), stored->size() - before.size() - after.size());
    } else {
        // The value stands in the template more than once: it ends where the text between the first two holes starts.
        const std::string between = after.substr(0, again);
        if (between.empty()) return std::nullopt;
        const size_t end = stored->find(between, before.size());
        if (end == std::string::npos) return std::nullopt;
        value = stored->substr(before.size(), end - before.size());
    }
    if (prefabParameterKey(p, value).second != *stored) return std::nullopt;
    return value;
}

// ------------------------------------------------------------------ bundle folders

bool isPrefabBundleFolder(const std::string& path) {
    const fs::path p(path);
    return hasBundleSuffix(p) && isDir(p);
}

bool loadPrefabBundle(const std::string& folder, PrefabBundle& out, std::string* err) {
    out = PrefabBundle{};
    const fs::path dir = fs::path(folder).lexically_normal();
    out.folder = dir.string();
    const std::string label = bundleLabel(dir);
    out.title = endsWithCi(label, kPrefabBundleSuffix) ? label.substr(0, label.size() - std::string(kPrefabBundleSuffix).size()) : label;
    out.category = dir.parent_path().filename().string();
    const auto fail = [&](const std::string& why) {
        out.placeable = false;
        out.problem = "prefab '" + label + "': " + why;
        if (err) *err = out.problem;
        return false;
    };
    std::string text;
    if (!readText(dir / kPrefabManifestFile, text)) return fail(std::string("no ") + kPrefabManifestFile + " in the folder");
    std::string perr;
    if (!parsePrefabManifest(text, out.manifest, &perr)) return fail(perr);
    const PrefabManifest& m = out.manifest;
    if (!m.title.empty()) out.title = m.title;
    if (!m.category.empty()) out.category = m.category;
    if (m.format != kPrefabBundleFormat)
        return fail("format \"" + m.format + "\" is not one this editor reads (it reads " + kPrefabBundleFormat +
                    "); the prefab is listed but cannot be placed");
    const auto dirIf = [&](const char* name) { return isDir(dir / name) ? (dir / name).string() : std::string(); };
    out.materialsDir = dirIf("materials");
    out.modelsDir = dirIf("models");
    out.soundsDir = dirIf("sounds");
    out.effectsDir = dirIf("effects");

    const std::string mapName = m.map.empty() ? "prefab.map" : m.map;
    if (!isFile(dir / mapName)) return fail("contents file '" + mapName + "' not found in the bundle");
    out.mapFile = (dir / mapName).string();
    if (!m.scriptFile.empty()) {
        if (!isFile(dir / m.scriptFile)) return fail("script.file '" + m.scriptFile + "' not found in the bundle");
        out.scriptFile = (dir / m.scriptFile).string();
    } else if (isFile(dir / "script.gsc")) {
        out.scriptFile = (dir / "script.gsc").string();
    }
    if (!m.fallbackMap.empty()) {
        if (!isFile(dir / m.fallbackMap)) return fail("fallback.map '" + m.fallbackMap + "' not found in the bundle");
        out.fallbackMapFile = (dir / m.fallbackMap).string();
    }
    if (!m.fallbackScript.empty()) {
        if (!isFile(dir / m.fallbackScript)) return fail("fallback.script '" + m.fallbackScript + "' not found in the bundle");
        out.fallbackScriptFile = (dir / m.fallbackScript).string();
    }
    if (!m.thumbnail.empty()) {
        if (isFile(dir / m.thumbnail)) out.thumbFile = (dir / m.thumbnail).string();
        else out.warnings.push_back("thumbnail '" + m.thumbnail + "' not found; the picture is drawn from the contents");
    } else if (isFile(dir / "thumb.png")) {
        out.thumbFile = (dir / "thumb.png").string();
    }
    const auto listed = [&](const char* kind, const std::string& name, const std::string& file) {
        if (!isFile(dir / file)) out.warnings.push_back(std::string(kind) + " '" + name + "': file '" + file + "' not found in the bundle");
    };
    for (const auto& a : m.materials) listed("material", a.name, a.file);
    for (const auto& a : m.models) listed("model", a.name, a.file);
    for (const auto& a : m.effects) listed("effect", a.name, a.file);
    for (const auto& a : m.sounds) listed("sound", a.alias, a.file);
    return true;
}

std::string prefabBundleContentsFile(const std::string& folder) {
    const fs::path dir(folder);
    std::string text, name = "prefab.map";
    PrefabManifest m;
    if (readText(dir / kPrefabManifestFile, text) && parsePrefabManifest(text, m, nullptr) && !m.map.empty()) name = m.map;
    return isFile(dir / name) ? (dir / name).string() : std::string();
}

PrefabRef resolvePrefabReference(const std::string& mapPath, const std::string& model, const std::vector<std::string>& searchRoots) {
    if (model.empty()) return {};
    std::string rel = model;
    std::replace(rel.begin(), rel.end(), '\\', '/');
    // "doors/door.iw8prefab/" names the folder too.
    if (std::string cut = rel.substr(0, rel.find_last_not_of('/') + 1); cut != rel && endsWithCi(cut, kPrefabBundleSuffix)) rel = cut;
    const fs::path m = fs::path(rel).make_preferred();
    if (m.is_absolute()) return refOf(m);
    for (const fs::path& c : prefabCandidates(mapPath, m, searchRoots)) {
        PrefabRef r = refOf(c);
        if (r.kind != PrefabRef::Kind::Missing) return r;
    }
    return {};
}

std::vector<PrefabBundleEntry> listPrefabBundles(const std::vector<std::string>& roots) {
    std::vector<PrefabBundleEntry> out;
    std::set<std::string> seen;
    for (const std::string& root : roots) {
        if (root.empty() || !isDir(root)) continue;
        const fs::path base(root);
        std::error_code ec;
        std::vector<fs::path> found;
        for (fs::recursive_directory_iterator it(base, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end;
             it.increment(ec)) {
            std::error_code dec;
            if (!it->is_directory(dec) || !hasBundleSuffix(it->path())) continue;
            found.push_back(it->path());
            it.disable_recursion_pending();  // nothing inside a bundle is another bundle
        }
        std::sort(found.begin(), found.end());
        for (const fs::path& dir : found) {
            if (!seen.insert(lowerText(dir.lexically_normal().generic_string())).second) continue;
            PrefabBundleEntry e;
            e.root = root;
            e.model = dir.lexically_relative(base).generic_string();
            loadPrefabBundle(dir.string(), e.bundle, nullptr);
            out.push_back(std::move(e));
        }
    }
    return out;
}

namespace {

void walkBundles(const MapFile& map, const std::string& mapPath, const std::vector<std::string>& roots, int depth,
                 std::set<std::string>& visited, std::vector<std::string>& folders) {
    for (const Entity& e : map.entities) {
        if (e.classname() != "misc_prefab") continue;
        const std::string* model = e.value("model");
        if (!model || model->empty()) continue;
        const PrefabRef ref = resolvePrefabReference(mapPath, *model, roots);
        if (ref.kind == PrefabRef::Kind::Missing) continue;
        std::vector<std::string> contents{ref.mapFile};
        if (ref.kind == PrefabRef::Kind::Bundle) {
            addUnique(folders, ref.path);
            PrefabBundle b;
            if (loadPrefabBundle(ref.path, b, nullptr) && !b.fallbackMapFile.empty()) contents.push_back(b.fallbackMapFile);
        }
        if (depth >= 8) continue;
        for (const std::string& file : contents) {
            if (file.empty() || !visited.insert(lowerText(file)).second) continue;
            std::string text;
            if (!readFile(file, text)) continue;
            Id next = 1;
            ParseResult pr = parseMap(text, next);
            if (pr.map) walkBundles(*pr.map, file, roots, depth + 1, visited, folders);
        }
    }
}

}  // namespace

std::vector<std::string> prefabBundleFoldersOf(const MapFile& map, const std::string& mapPath, const std::vector<std::string>& roots) {
    std::vector<std::string> folders;
    std::set<std::string> visited;
    walkBundles(map, mapPath, roots, 0, visited, folders);
    return folders;
}

PrefabAssetRoots prefabBundleAssetRoots(const MapFile& map, const std::string& mapPath, const std::vector<std::string>& roots) {
    PrefabAssetRoots out;
    for (const std::string& folder : prefabBundleFoldersOf(map, mapPath, roots)) {
        PrefabBundle b;
        if (!loadPrefabBundle(folder, b, nullptr)) continue;
        addUnique(out.materials, b.materialsDir);
        addUnique(out.models, b.modelsDir);
        addUnique(out.sounds, b.soundsDir);
        addUnique(out.effects, b.effectsDir);
    }
    return out;
}

// ------------------------------------------------------------------ requires / fallback

int PrefabStockCatalog::load(const std::string& assetCatalogDir) {
    int files = 0;
    const auto read = [&](const char* name, std::map<std::string, std::vector<std::string>>& into) {
        into.clear();
        std::string text;
        if (!readText(fs::path(assetCatalogDir) / name, text)) return;
        ++files;
        size_t at = 0;
        while (at < text.size()) {
            size_t end = text.find('\n', at);
            if (end == std::string::npos) end = text.size();
            std::string line = text.substr(at, end - at);
            at = end + 1;
            if (!line.empty() && line.back() == '\r') line.pop_back();
            // A comment starts with "# "; some material names start with '#' themselves.
            if (line.empty() || line == "#" || line.rfind("# ", 0) == 0) continue;
            const size_t tab = line.find('\t');
            const std::string key = lowerText(line.substr(0, tab));
            std::vector<std::string> zones;
            if (tab != std::string::npos) {
                const std::string last = line.substr(line.rfind('\t') + 1);
                std::string z;
                for (size_t i = 0; i <= last.size(); ++i) {
                    if (i == last.size() || last[i] == ',') {
                        if (!z.empty()) zones.push_back(z);
                        z.clear();
                    } else {
                        z += last[i];
                    }
                }
            }
            if (!key.empty()) into[key] = std::move(zones);
        }
    };
    read("iw8_models.tsv", models);
    read("iw8_materials.tsv", materials);
    read("iw8_common_sounds.tsv", sounds);
    read("iw8_common_vfx.tsv", effects);
    return files;
}

// scriptable_instances is not listed although the build writes instances since 2026-11-11 (research 86): only
// hardpoint_chevron can be placed, so a bundle that asks for scriptables still builds its fallback contents.
std::set<std::string> defaultPrefabBuildFeatures() { return {kPrefabFeatureCustomSounds}; }

PrefabContentsChoice choosePrefabContents(const PrefabBundle& bundle, const PrefabBuildSupport& support) {
    PrefabContentsChoice c;
    const PrefabManifest& m = bundle.manifest;
    for (const std::string& f : m.buildFeatures)
        if (!support.features.count(f)) addUnique(c.missingFeatures, f);
    std::vector<std::string> triggers;
    for (const std::string& f : m.fallbackWhenMissing)
        if (!support.features.count(f)) addUnique(triggers, f);
    c.fallback = !triggers.empty() && !bundle.fallbackMapFile.empty();
    c.mapFile = c.fallback ? bundle.fallbackMapFile : bundle.mapFile;
    c.scriptFile = c.fallback ? bundle.fallbackScriptFile : bundle.scriptFile;
    if (c.fallback) {
        c.reason = "fallback contents built: " + (m.fallbackNote.empty() ? std::string("the build lacks a feature the prefab needs") : m.fallbackNote) +
                   " (missing build feature(s): " + joinList(triggers) + ")";
    } else if (!c.missingFeatures.empty()) {
        c.reason = "needs the build feature(s) " + joinList(c.missingFeatures) +
                   ", which this build does not have, and has no fallback for them: the main contents are built as they are";
    }
    if (support.stock) {
        const auto check = [&](const char* kind, const std::string& name, const std::map<std::string, std::vector<std::string>>& known) {
            if (!known.empty() && !known.count(lowerText(name))) c.missingAssets.push_back(std::string("stock ") + kind + " '" + name + "'");
        };
        for (const auto& r : m.stockModels) check("model", r.name, support.stock->models);
        for (const auto& r : m.stockMaterials) check("material", r.name, support.stock->materials);
        for (const auto& s : m.stockSounds) check("sound", s, support.stock->sounds);
        for (const auto& s : m.stockEffects) check("effect", s, support.stock->effects);
    }
    return c;
}

// ------------------------------------------------------------------ names per placement

const std::vector<std::string>& prefabNameKeys() {
    static const std::vector<std::string> keys = {"targetname", "target",  "target2", "target3",         "target4",       "target5",
                                                  "target6",    "target7", "target8", "script_linkname", "script_linkto", "iw8r_teleport",
                                                  "iw8r_visual"};
    return keys;
}

bool isValidPrefabPlacementId(const std::string& pid) {
    if (pid.empty() || pid.size() > 24) return false;
    for (char c : pid)
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_')) return false;
    return true;
}

std::string nextPrefabPlacementId(const MapFile& map) {
    std::set<std::string> used;
    for (const Entity& e : map.entities)
        if (e.classname() == "misc_prefab")
            if (const std::string* p = e.value(kPrefabPlacementIdKey)) used.insert(*p);
    for (int n = 1;; ++n) {
        const std::string id = "p" + std::to_string(n);
        if (!used.count(id)) return id;
    }
}

std::string scopedPrefabName(const std::string& pid, const std::string& name) { return pid + "_" + name; }

std::set<std::string> prefabLinkedNames(const MapFile& contents, const std::vector<std::string>& extraKeys) {
    // Two name spaces: targetname with the keys that point at one, script_linkname with script_linkto.
    std::set<std::string> targetnames, linknames, targeted, linked;
    for (const Entity& e : contents.entities) {
        for (const std::string& n : entityNames(e, "targetname")) targetnames.insert(n);
        for (const std::string& n : entityNames(e, "script_linkname")) linknames.insert(n);
        for (const std::string& n : entityNames(e, "script_linkto")) linked.insert(n);
        for (const std::string& key : prefabNameKeys())
            if (key != "targetname" && key != "script_linkname" && key != "script_linkto")
                for (const std::string& n : entityNames(e, key)) targeted.insert(n);
        for (const std::string& key : extraKeys)
            for (const std::string& n : entityNames(e, key)) targeted.insert(n);
    }
    std::set<std::string> out;
    for (const std::string& n : targetnames)
        if (targeted.count(n)) out.insert(n);
    for (const std::string& n : linknames)
        if (linked.count(n)) out.insert(n);
    return out;
}

const std::vector<std::string>& prefabBehaviourKeys() {
    static const std::vector<std::string> keys = {"iw8r_door", "iw8r_teleport", "iw8r_jump", "iw8r_damage", "iw8r_lift",
                                                  "iw8r_spin", "iw8r_bob",      "iw8r_anim", "iw8r_vision", "iw8r_no_collision"};
    return keys;
}

std::set<std::string> prefabInferredLocalNames(const MapFile& contents, const std::vector<std::string>& extraKeys) {
    std::set<std::string> local = prefabLinkedNames(contents, extraKeys);
    // An entity that refers to a local name is part of the same mechanism: its own name is local too. Repeated
    // until nothing is added (a trigger that targets a relay that targets a door).
    for (bool grew = true; grew;) {
        grew = false;
        for (const Entity& e : contents.entities) {
            bool refers = false;
            for (const std::string& key : prefabNameKeys())
                if (key != "targetname" && key != "script_linkname")
                    for (const std::string& n : entityNames(e, key)) refers |= local.count(n) != 0;
            for (const std::string& key : extraKeys)
                for (const std::string& n : entityNames(e, key)) refers |= local.count(n) != 0;
            if (!refers) continue;
            for (const std::string& n : entityNames(e, "targetname")) grew |= local.insert(n).second;
        }
    }
    for (const Entity& e : contents.entities)
        for (const std::string& key : prefabBehaviourKeys())
            if (e.value(key))
                for (const std::string& n : entityNames(e, "targetname")) local.insert(n);
    return local;
}

std::set<std::string> prefabLocalNames(const MapFile& contents, const PrefabManifest& manifest) {
    std::set<std::string> local = manifest.hasLocalNames ? std::set<std::string>(manifest.namesLocal.begin(), manifest.namesLocal.end())
                                                         : prefabInferredLocalNames(contents, manifest.nameKeys);
    for (const std::string& g : manifest.namesGlobal) local.erase(g);
    return local;
}

PrefabNameScope prefabNameScope(const MapFile& contents, const PrefabManifest& manifest, const std::string& pid) {
    PrefabNameScope s;
    s.pid = pid;
    s.local = prefabLocalNames(contents, manifest);
    s.keys = prefabNameKeys();
    for (const std::string& k : manifest.nameKeys) addUnique(s.keys, k);
    return s;
}

size_t scopePrefabNames(std::vector<std::pair<std::string, std::string>>& keys, const PrefabNameScope& scope) {
    if (scope.pid.empty() || scope.local.empty()) return 0;
    size_t renamed = 0;
    for (auto& [key, value] : keys) {
        if (std::find(scope.keys.begin(), scope.keys.end(), key) == scope.keys.end()) continue;
        // Each run of non-space characters is one name; the spaces between them stay as they are.
        std::string out;
        size_t i = 0;
        while (i < value.size()) {
            size_t e = i;
            if (value[i] == ' ' || value[i] == '\t') {
                while (e < value.size() && (value[e] == ' ' || value[e] == '\t')) ++e;
                out.append(value, i, e - i);
            } else {
                while (e < value.size() && value[e] != ' ' && value[e] != '\t') ++e;
                const std::string name = value.substr(i, e - i);
                if (scope.local.count(name)) {
                    out += scopedPrefabName(scope.pid, name);
                    ++renamed;
                } else {
                    out += name;
                }
            }
            i = e;
        }
        value = std::move(out);
    }
    return renamed;
}

// ------------------------------------------------------------------ scripts

std::vector<std::string> checkPrefabScript(const std::string& text, const PrefabManifest& manifest) {
    std::vector<std::string> out;
    const std::vector<std::string> names = scriptFunctionNames(text);
    const auto has = [&](const std::string& n) { return std::find(names.begin(), names.end(), n) != names.end(); };
    if (!has(manifest.scriptEntry)) out.push_back("no " + manifest.scriptEntry + "() function: the script is included, but nothing starts it");
    if (!manifest.scriptOnce.empty() && !has(manifest.scriptOnce)) out.push_back("no " + manifest.scriptOnce + "() function (script.once)");
    std::set<std::string> seen;
    for (const std::string& n : names) {
        if (n.compare(0, manifest.scriptPrefix.size(), manifest.scriptPrefix) != 0 || n.size() <= manifest.scriptPrefix.size())
            out.push_back("function " + n + "() does not start with " + manifest.scriptPrefix +
                          ": it can clash with another prefab or with the map script");
        if (!seen.insert(n).second) out.push_back("function " + n + "() is defined twice");
    }
    int line = 1;
    for (size_t at = 0; at < text.size(); ++line) {
        size_t end = text.find('\n', at);
        if (end == std::string::npos) end = text.size();
        size_t first = at;
        while (first < end && (text[first] == ' ' || text[first] == '\t')) ++first;
        if (text.compare(first, 8, "#include") == 0 || text.compare(first, 6, "#using") == 0)
            out.push_back("line " + std::to_string(line) + ": #include and #using cannot be used (the script is placed inside the map script)");
        at = end + 1;
    }
    return out;
}

// ------------------------------------------------------------------ save selection as a bundle

namespace {

std::string safeFileStem(const std::string& name) {
    std::string out;
    for (unsigned char c : name) out += std::isalnum(c) || c == '_' || c == '-' ? static_cast<char>(std::tolower(c)) : '_';
    return out.empty() ? "asset" : out;
}

struct FileCollector {
    PrefabSaveResult& r;
    std::map<std::string, std::string> destSource;  // lower-case dest -> source

    // Adds a file to copy. A second file that would land on the same name gets the hint in front. Returns the dest.
    std::string add(const std::string& kind, const std::string& name, const fs::path& source, const std::string& folder,
                    const std::string& hint) {
        std::string dest = folder + "/" + source.filename().string();
        const std::string src = source.lexically_normal().string();
        auto it = destSource.find(lowerText(dest));
        if (it != destSource.end()) {
            if (lowerText(it->second) == lowerText(src)) return dest;  // already there
            dest = folder + "/" + safeFileStem(hint) + "_" + source.filename().string();
            if (destSource.count(lowerText(dest))) return dest;
        }
        destSource[lowerText(dest)] = src;
        r.files.push_back({kind, name, src, dest});
        return dest;
    }
};

// Files next to `file` with the same stem and another extension (a .mtl, a glTF .bin, a sound's settings file).
std::vector<fs::path> sidecars(const fs::path& file) {
    std::vector<fs::path> out;
    std::error_code ec;
    const std::string stem = lowerText(file.stem().string());
    for (fs::directory_iterator it(file.parent_path(), ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code fec;
        if (!it->is_regular_file(fec) || it->path().filename() == file.filename()) continue;
        std::string name = lowerText(it->path().filename().string());
        if (name.size() > stem.size() && name.compare(0, stem.size(), stem) == 0 && name[stem.size()] == '.') out.push_back(it->path());
    }
    std::sort(out.begin(), out.end());
    return out;
}

}  // namespace

PrefabSaveResult prefabBundleFromSelection(const Document& doc, const PrefabSaveOptions& opt) {
    PrefabSaveResult r;
    // What is selected: bounds for the pivot, and the entities for the link check.
    AABB box;
    std::set<Id> selectedEntities;
    for (const SelRef& ref : doc.selection()) {
        const Entity* e = doc.findEntity(ref.entity);
        if (!e) continue;
        if (ref.item != kNoId) {
            const EntityItem* it = doc.findItem(ref.entity, ref.item);
            if (!it || !it->prim()) continue;
            box.add(it->prim()->bounds());
            if (!e->isWorldspawn()) selectedEntities.insert(e->id);
            continue;
        }
        if (e->isWorldspawn()) continue;
        selectedEntities.insert(e->id);
        bool prims = false;
        for (const auto& it : e->items)
            if (const Primitive* p = it.prim()) {
                box.add(p->bounds());
                prims = true;
            }
        if (!prims) box.add(e->origin().value_or(Vec3{}));
    }
    if (!box.valid()) {
        r.error = "nothing is selected: select the brushes and entities the prefab should hold";
        return r;
    }
    r.pivot = opt.pivot ? *opt.pivot : Vec3{(box.mins.x + box.maxs.x) * 0.5, (box.mins.y + box.maxs.y) * 0.5, box.mins.z};
    if (!opt.pivot && opt.grid > 0) r.pivot = snapToGrid(r.pivot, opt.grid);
    r.mapText = selectionToPrefabText(doc, r.pivot);
    Id next = 1;
    ParseResult pr = parseMap(r.mapText, next);
    if (!pr.map) {
        r.error = "the selection does not give a readable prefab";
        return r;
    }
    const MapFile& pm = *pr.map;

    PrefabManifest& m = r.manifest;
    m.id = isValidPrefabId(opt.id) ? opt.id : prefabIdFromName(opt.id.empty() ? opt.name : opt.id);
    m.title = opt.name;
    m.description = opt.description;
    m.category = opt.category;
    m.tags = opt.tags;
    m.author = opt.author;
    m.pivot = opt.pivot ? "the point picked when it was saved" : "bottom centre of the saved selection";
    m.bounds = AABB{box.mins - r.pivot, box.maxs - r.pivot};

    // Linked objects outside the selection (the dialog offers to add them).
    {
        std::set<std::string> defined, referred;
        const auto namesOf = [](const Entity& e, bool defs) {
            std::vector<std::string> out;
            for (const std::string& key : prefabNameKeys()) {
                const bool isDef = key == "targetname" || key == "script_linkname";
                if (isDef != defs) continue;
                for (const std::string& n : entityNames(e, key)) out.push_back(n);
            }
            return out;
        };
        for (const Entity& e : doc.map().entities) {
            if (!selectedEntities.count(e.id)) continue;
            for (const std::string& n : namesOf(e, true)) defined.insert(n);
            for (const std::string& n : namesOf(e, false)) referred.insert(n);
        }
        for (const Entity& e : doc.map().entities) {
            if (e.isWorldspawn() || selectedEntities.count(e.id)) continue;
            const std::string* tn = e.value("targetname");
            const std::string who = e.classname() + (tn && !tn->empty() ? " '" + *tn + "'" : std::string());
            std::string why;
            for (const std::string& n : namesOf(e, true))
                if (referred.count(n) && why.empty()) why = who + " is targeted by the selection ('" + n + "') and is not selected";
            for (const std::string& n : namesOf(e, false))
                if (defined.count(n) && why.empty()) why = who + " targets '" + n + "' in the selection and is not selected";
            if (!why.empty()) r.linkedOutside.push_back({e.id, why});
        }
        // Names: linked ones and the ones the generated script looks up are local; names that point outside are global.
        std::set<std::string> local = prefabInferredLocalNames(pm), definedIn;
        for (const Entity& e : pm.entities)
            for (const std::string& n : namesOf(e, true)) definedIn.insert(n);
        m.hasLocalNames = true;
        m.namesLocal.assign(local.begin(), local.end());
        std::set<std::string> global;
        for (const Entity& e : pm.entities)
            for (const std::string& n : namesOf(e, false))
                if (!definedIn.count(n)) global.insert(n);
        m.namesGlobal.assign(global.begin(), global.end());
    }

    // Assets: the mapper's own files are copied, every other name is a stock name the prefab requires.
    const MapAssetUsage usage = collectAssetUsage(pm);
    const std::vector<OwnAssetFile> own = findOwnAssetFiles(usage, opt.materialRoots, opt.modelRoots, opt.soundRoots, opt.fxRoots);
    std::map<std::string, const OwnAssetFile*> ownMaterials;
    for (const OwnAssetFile& f : own)
        if (f.kind == "material") ownMaterials.emplace(lowerText(f.name), &f);
    FileCollector files{r, {}};
    std::set<std::string> savedMaterials;
    const auto saveMaterial = [&](const OwnAssetFile& f) {
        if (!savedMaterials.insert(lowerText(f.name)).second) return;
        std::string text;
        std::optional<Value> def;
        if (readFile(f.path, text)) def = json::parse(text, nullptr);
        if (!def || def->type != Type::Object) {
            r.notes.push_back("material '" + f.name + "': its definition cannot be read; not copied");
            return;
        }
        // The copy names itself and its images by file name, so it works from the bundle's materials folder.
        setMember(*def, "name", textValue(f.name));
        const fs::path base = fs::path(f.path).parent_path();
        for (const char* key : {"color", "normal", "specular"}) {
            Value* v = member(*def, key);
            if (!v || !v->string() || v->string()->empty()) continue;
            fs::path img(*v->string());
            if (!img.is_absolute()) img = base / img;
            img = img.lexically_normal();
            if (!isFile(img)) {
                r.notes.push_back("material '" + f.name + "': image '" + *v->string() + "' not found; not copied");
                continue;
            }
            const std::string dest = files.add("image", f.name, img, "materials", f.name);
            *v = textValue(dest.substr(std::string("materials/").size()));
        }
        const std::string dest = "materials/" + safeFileStem(f.name) + ".iw8mat.json";
        r.generated.emplace_back(dest, dumpJson(*def));
        m.materials.push_back({f.name, dest});
    };
    for (const OwnAssetFile& f : own) {
        if (!f.used) continue;
        if (f.kind == "material") {
            saveMaterial(f);
        } else if (f.kind == "model") {
            const fs::path src(f.path);
            m.models.push_back({f.name, files.add("model", f.name, src, "models", f.name)});
            for (const fs::path& side : sidecars(src)) files.add("model", f.name, side, "models", f.name);
            // The model's own materials follow it.
            ImportedModel im;
            std::string ierr;
            if (importModelFile(f.path, im, &ierr))
                for (const ImportedMesh& mesh : im.meshes)
                    if (const auto it = ownMaterials.find(lowerText(mesh.material)); it != ownMaterials.end()) saveMaterial(*it->second);
        } else if (f.kind == "sound") {
            const fs::path src(f.path);
            m.sounds.push_back({f.name, files.add("sound", f.name, src, "sounds", f.name), false});
            for (const fs::path& side : sidecars(src)) files.add("sound", f.name, side, "sounds", f.name);
        } else if (f.kind == "effect") {
            const std::string dest = "effects/" + f.name + ".iw8fx.json";
            r.files.push_back({"effect", f.name, fs::path(f.path).lexically_normal().string(), dest});
            m.effects.push_back({f.name, dest});
        }
    }
    const auto isOwn = [&](const char* kind, const std::string& name) {
        for (const OwnAssetFile& f : own)
            if (f.used && f.kind == kind && lowerText(f.name) == name) return true;
        return false;
    };
    const auto zonesOf = [&](const std::map<std::string, std::vector<std::string>>* known, const std::string& name, bool& found) {
        found = true;
        if (!known || known->empty()) return std::vector<std::string>{};
        const auto it = known->find(name);
        found = it != known->end();
        return found ? it->second : std::vector<std::string>{};
    };
    const PrefabStockCatalog* cat = opt.stock;
    for (const AssetUse& u : usage.materials) {
        if (isOwn("material", u.name) || isIw8ToolMaterial(u.name)) continue;
        bool found = false;
        std::vector<std::string> zones = zonesOf(cat ? &cat->materials : nullptr, u.name, found);
        if (found) m.stockMaterials.push_back({u.name, std::move(zones)});
        else r.notes.push_back("material '" + u.name + "' is not one of your materials and not a stock material: it builds grey");
    }
    for (const AssetUse& u : usage.models) {
        if (isOwn("model", u.name)) continue;
        bool found = false;
        std::vector<std::string> zones = zonesOf(cat ? &cat->models : nullptr, u.name, found);
        if (found) m.stockModels.push_back({u.name, std::move(zones)});
        else r.notes.push_back("model '" + u.name + "' is not one of your models and not a stock model");
    }
    for (const AssetUse& u : usage.sounds) {
        if (isOwn("sound", u.name)) continue;
        bool found = false;
        zonesOf(cat ? &cat->sounds : nullptr, u.name, found);
        if (found) m.stockSounds.push_back(u.name);
        else r.notes.push_back("sound '" + u.name + "' is not one of your sounds and not in the always-loaded stock sounds");
    }
    for (const AssetUse& u : usage.effects) {
        if (isOwn("effect", u.name)) continue;
        bool found = false;
        zonesOf(cat ? &cat->effects : nullptr, u.name, found);
        if (found) m.stockEffects.push_back(u.name);
        else r.notes.push_back("effect '" + u.name + "' is not one of your effects and not in the always-loaded stock effects");
    }
    const auto byName = [](const auto& a, const auto& b) { return a.name < b.name; };
    std::sort(m.stockMaterials.begin(), m.stockMaterials.end(), byName);
    std::sort(m.stockModels.begin(), m.stockModels.end(), byName);
    std::sort(m.stockSounds.begin(), m.stockSounds.end());
    std::sort(m.stockEffects.begin(), m.stockEffects.end());
    if (!m.sounds.empty()) addUnique(m.buildFeatures, kPrefabFeatureCustomSounds);
    for (const Entity& e : pm.entities) {
        if (e.classname() != "scriptable") continue;
        if (const std::string* def = e.value("scriptable"); def && !def->empty()) {
            bool have = false;
            for (const auto& d : m.scriptableDefs) have |= d.name == *def;
            if (!have) m.scriptableDefs.push_back({*def, {}});
        }
        addUnique(m.buildFeatures, kPrefabFeatureScriptableInstances);
    }
    if (!opt.scriptText.empty()) {
        r.scriptText = opt.scriptText;
        m.scriptFile = "script.gsc";
        for (const std::string& problem : checkPrefabScript(r.scriptText, m)) r.notes.push_back("script: " + problem);
    }
    r.ok = true;
    return r;
}

bool writePrefabBundleFolder(const std::string& folder, const PrefabSaveResult& result, std::string* err, bool overwrite) {
    const auto fail = [&](const std::string& why) {
        if (err) *err = why;
        return false;
    };
    if (!result.ok) return fail(result.error.empty() ? "there is nothing to save" : result.error);
    const fs::path dir(folder);
    if (!hasBundleSuffix(dir)) return fail("a prefab bundle folder must end with " + std::string(kPrefabBundleSuffix));
    if (!overwrite && isFile(dir / kPrefabManifestFile)) return fail("'" + dir.string() + "' already holds a prefab");
    std::error_code ec;
    fs::create_directories(dir, ec);
    if (ec) return fail("cannot create '" + dir.string() + "': " + ec.message());
    const auto put = [&](const std::string& rel, const std::string& text) {
        const fs::path p = dir / fs::path(rel);
        std::error_code dec;
        fs::create_directories(p.parent_path(), dec);
        std::string werr;
        if (writeFileAtomic(p.string(), text, &werr)) return true;
        return fail("cannot write '" + p.string() + "': " + werr);
    };
    for (const PrefabSaveFile& f : result.files) {
        if (!isBundleFileName(f.dest)) return fail("'" + f.dest + "' is not a path inside the bundle");
        const fs::path dest = dir / fs::path(f.dest);
        std::error_code cec;
        fs::create_directories(dest.parent_path(), cec);
        fs::copy_file(fs::path(f.source), dest, fs::copy_options::overwrite_existing, cec);
        if (cec) return fail("cannot copy '" + f.source + "' into the bundle: " + cec.message());
    }
    for (const auto& [dest, text] : result.generated)
        if (!put(dest, text)) return false;
    if (!result.scriptText.empty() && !put(result.manifest.scriptFile.empty() ? "script.gsc" : result.manifest.scriptFile, result.scriptText))
        return false;
    if (!put(result.manifest.map.empty() ? "prefab.map" : result.manifest.map, result.mapText)) return false;
    return put(kPrefabManifestFile, prefabManifestText(result.manifest));  // last: a folder with a manifest is complete
}

}  // namespace iw8r
