#include "entity_defs.h"

#include <cctype>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <sstream>

#include "json_lite.h"

namespace iw8r {
namespace {

// Area names that the converter accepts by prefix (compile_iw8.cpp acceptedPointClass). They have
// no row of their own in the table, so validate() treats them as built.
constexpr const char* kBcsPrefix = "trigger_multiple_bcs_";

bool startsWith(const std::string& s, const std::string& prefix) {
    return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}

std::vector<std::string> splitWs(const std::string& s) {
    std::vector<std::string> out;
    std::istringstream in(s);
    std::string t;
    while (in >> t) out.push_back(t);
    return out;
}

bool parseNumber(const std::string& s, double& out) {
    const char* begin = s.c_str();
    char* end = nullptr;
    out = std::strtod(begin, &end);
    if (end == begin || !std::isfinite(out)) return false;
    return *end == '\0';
}

bool isInt(const std::string& s) {
    size_t i = (!s.empty() && (s[0] == '-' || s[0] == '+')) ? 1 : 0;
    if (i >= s.size()) return false;
    for (; i < s.size(); ++i)
        if (!std::isdigit(static_cast<unsigned char>(s[i]))) return false;
    return true;
}

// Returns an empty string when `value` parses as `type`, otherwise a short description of what was expected.
std::string checkValue(const std::string& type, const std::string& value) {
    const std::vector<std::string> tok = splitWs(value);
    double d = 0;
    if (type == "int") return (tok.size() == 1 && isInt(tok[0])) ? "" : "an integer";
    if (type == "float") return (tok.size() == 1 && parseNumber(tok[0], d)) ? "" : "a number";
    if (type == "vec3" || type == "angles") {
        if (tok.size() != 3) return "three numbers";
        for (const auto& t : tok)
            if (!parseNumber(t, d)) return "three numbers";
        return "";
    }
    if (type == "bool") {
        if (tok.size() != 1) return "0, 1, true or false";
        std::string v = tok[0];
        for (auto& c : v) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return (v == "0" || v == "1" || v == "true" || v == "false") ? "" : "0, 1, true or false";
    }
    if (type == "model" && !value.empty() && value[0] == '*')  // "*N": brush model N
        return isInt(value.substr(1)) ? "" : "a brush model index (*N)";
    return "";  // string, targetname, material and plain model names accept any text
}

json::Value::Type objectType() { return json::Value::Type::Object; }

std::string strOf(const json::Value* v) {
    const std::string* s = v ? v->string() : nullptr;
    return s ? *s : std::string();
}

bool boolOf(const json::Value* v) { return v && v->type == json::Value::Type::Bool && v->b; }

double numOf(const json::Value* v, double def) {
    if (v)
        if (auto n = v->number()) return *n;
    return def;
}

Vec3 vec3Of(const json::Value* v) {
    Vec3 r;
    if (!v || v->type != json::Value::Type::Array || v->arr.size() < 3) return r;
    r.x = numOf(v->at(0), 0);
    r.y = numOf(v->at(1), 0);
    r.z = numOf(v->at(2), 0);
    return r;
}

}  // namespace

const KeyDef* ClassDef::key(const std::string& keyName) const {
    for (const KeyDef& k : keys)
        if (k.name == keyName) return &k;
    return nullptr;
}

bool EntityDefs::load(const std::string& jsonPath, std::string* err) {
    auto fail = [&](const std::string& msg) {
        if (err) *err = jsonPath + ": " + msg;
        return false;
    };
    std::ifstream f(jsonPath, std::ios::binary);
    if (!f) return fail("cannot open file");
    std::stringstream ss;
    ss << f.rdbuf();

    std::string parseErr;
    const std::optional<json::Value> root = json::parse(ss.str(), &parseErr);
    if (!root) return fail(parseErr);
    const json::Value* schema = root->get("schema");
    const auto schemaNum = schema ? schema->number() : std::nullopt;
    if (!schemaNum || *schemaNum != 1) return fail("unsupported or missing schema (expected 1)");
    const json::Value* classes = root->get("classes");
    if (!classes || classes->type != objectType()) return fail("no classes object");

    std::map<std::string, ClassDef> loaded;
    for (const auto& [className, cv] : classes->obj) {
        ClassDef c;
        c.name = className;
        c.description = strOf(cv.get("description"));
        c.kind = strOf(cv.get("kind"));
        if (const json::Value* sz = cv.get("size"); sz && sz->type == json::Value::Type::Array && sz->arr.size() == 2) {
            c.box.mins = vec3Of(sz->at(0));
            c.box.maxs = vec3Of(sz->at(1));
        }
        if (const json::Value* col = cv.get("color"); col && col->type == json::Value::Type::Array)
            for (size_t i = 0; i < 3; ++i) c.color[i] = numOf(col->at(i), 0.8);
        c.buildAccepted = boolOf(cv.get("buildAccepted"));
        if (const json::Value* keysV = cv.get("keys"); keysV && keysV->type == objectType()) {
            for (const auto& [keyName, kd] : keysV->obj) {
                KeyDef k;
                k.name = keyName;
                k.type = strOf(kd.get("type"));
                k.defaultValue = strOf(kd.get("default"));
                k.description = strOf(kd.get("description"));
                k.required = boolOf(kd.get("required"));
                if (const json::Value* ex = kd.get("examples"); ex && ex->type == json::Value::Type::Array)
                    for (const json::Value& e : ex->arr) k.examples.push_back(strOf(&e));
                c.keys.push_back(std::move(k));
            }
        }
        loaded[className] = std::move(c);
    }
    if (loaded.empty()) return fail("no classes");
    classes_ = std::move(loaded);
    return true;
}

const ClassDef* EntityDefs::find(const std::string& classname) const {
    auto it = classes_.find(classname);
    return it == classes_.end() ? nullptr : &it->second;
}

std::vector<std::string> EntityDefs::classNames() const {
    std::vector<std::string> out;
    out.reserve(classes_.size());
    for (const auto& kv : classes_) out.push_back(kv.first);  // std::map keeps them sorted
    return out;
}

std::vector<std::string> EntityDefs::validate(const Entity& e) const {
    std::vector<std::string> out;
    const std::string cls = e.classname();
    const ClassDef* def = find(cls);
    if (!def) {
        if (!startsWith(cls, kBcsPrefix))
            out.push_back("class '" + cls + "' has no entity definition, so the build does not accept it");
        return out;
    }
    if (!def->buildAccepted) out.push_back("class '" + cls + "' is not built: the converter drops it (buildAccepted is false)");

    for (const KeyDef& k : def->keys) {
        if (!k.required) continue;
        const std::string* v = e.value(k.name);
        if (!v || v->empty()) out.push_back("missing required key '" + k.name + "' on " + cls);
    }
    for (const EntityItem& it : e.items) {
        const KeyValueLine* kv = it.kv();
        if (!kv || kv->value.empty()) continue;
        const std::string name = kv->name();
        const KeyDef* k = def->key(name);
        if (!k) continue;
        const std::string why = checkValue(k->type, kv->value);
        if (!why.empty())
            out.push_back("key '" + name + "' value '" + kv->value + "' is not a valid " + k->type + " on " + cls +
                          " (expected " + why + ")");
    }
    return out;
}

}  // namespace iw8r
