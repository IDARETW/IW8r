// Hotkey presets (roadmap 2.30): see hotkey_presets.h.
#include "hotkey_presets.h"

#include <cctype>
#include <set>

#include "json_lite.h"

namespace iw8r {
namespace {

using json::Value;

// Canonical modifier order: the index also selects the flag in parseKey().
const char* const kModOrder[4] = {"Ctrl", "Alt", "Shift", "Meta"};

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// Parses "Ctrl+Shift+P" style text into `canonical`. False when a modifier is unknown or repeated, or the key is missing.
bool parseKey(const std::string& text, std::string& canonical) {
    canonical.clear();
    if (text.empty()) return true;
    if (text == "+") {
        canonical = "+";
        return true;
    }
    std::string base, key;
    if (text.size() >= 2 && text.compare(text.size() - 2, 2, "++") == 0) {
        base = text.substr(0, text.size() - 2);  // "Ctrl++": the key is the plus sign
        key = "+";
    } else {
        const size_t p = text.rfind('+');
        if (p == std::string::npos) {
            key = text;
        } else {
            base = text.substr(0, p);
            key = text.substr(p + 1);
        }
    }
    if (key.empty()) return false;

    bool on[4] = {false, false, false, false};
    size_t start = 0;
    while (start < base.size()) {
        size_t end = base.find('+', start);
        if (end == std::string::npos) end = base.size();
        const std::string m = lower(base.substr(start, end - start));
        start = end + 1;
        if (m.empty()) continue;
        int idx = -1;
        if (m == "ctrl" || m == "control") idx = 0;
        else if (m == "alt") idx = 1;
        else if (m == "shift") idx = 2;
        else if (m == "meta") idx = 3;
        if (idx < 0 || on[idx]) return false;
        on[idx] = true;
    }
    for (int i = 0; i < 4; ++i) {
        if (!on[i]) continue;
        canonical += kModOrder[i];
        canonical += '+';
    }
    if (key.size() == 1 && std::isalpha(static_cast<unsigned char>(key[0])))
        key[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(key[0])));
    canonical += key;
    return true;
}

}  // namespace

bool loadHotkeyPresets(const std::string& jsonText, std::vector<HotkeyPreset>& out, std::string* err) {
    auto fail = [&](const std::string& msg) {
        if (err) *err = "hotkey presets: " + msg;
        return false;
    };
    std::string parseErr;
    const auto root = json::parse(jsonText, &parseErr);
    if (!root) return fail(parseErr);
    if (root->type != Value::Type::Object) return fail("root is not an object");
    const Value* list = root->get("presets");
    if (!list || list->type != Value::Type::Array) return fail("\"presets\" array missing");

    std::vector<HotkeyPreset> parsed;
    std::set<std::string> ids;
    for (size_t i = 0; i < list->arr.size(); ++i) {
        const Value& p = list->arr[i];
        const std::string where = "presets[" + std::to_string(i) + "]";
        if (p.type != Value::Type::Object) return fail(where + " is not an object");
        HotkeyPreset preset;
        const Value* id = p.get("id");
        if (!id || !id->string() || id->str.empty()) return fail(where + ": \"id\" missing");
        preset.id = id->str;
        const std::string tag = where + " (" + preset.id + ")";
        const Value* name = p.get("name");
        if (!name || !name->string()) return fail(tag + ": \"name\" missing");
        preset.name = name->str;
        if (const Value* src = p.get("source")) {
            if (!src->string()) return fail(tag + ": \"source\" is not a string");
            preset.source = src->str;
        }
        if (!ids.insert(preset.id).second) return fail("duplicate id " + preset.id);
        const Value* keys = p.get("keys");
        if (!keys || keys->type != Value::Type::Object) return fail(tag + ": \"keys\" object missing");
        for (const auto& [cmd, v] : keys->obj) {
            const std::string* text = v.string();
            if (!text) return fail(tag + ": keys." + cmd + " is not a string");
            std::string canon;
            if (!parseKey(*text, canon)) return fail(tag + ": keys." + cmd + " has a bad key \"" + *text + "\"");
            preset.keys[cmd] = *text;
        }
        parsed.push_back(std::move(preset));
    }
    out = std::move(parsed);
    return true;
}

std::string canonicalKey(const std::string& key) {
    std::string canon;
    return parseKey(key, canon) ? canon : key;
}

std::vector<std::pair<std::string, std::string>> presetConflicts(const HotkeyPreset& preset) {
    std::map<std::string, std::vector<std::string>> byKey;  // canonical key -> command ids, sorted by id
    for (const auto& [cmd, key] : preset.keys) {
        const std::string k = canonicalKey(key);
        if (!k.empty()) byKey[k].push_back(cmd);
    }
    std::vector<std::pair<std::string, std::string>> out;
    for (const auto& [key, cmds] : byKey)
        for (size_t i = 0; i < cmds.size(); ++i)
            for (size_t j = i + 1; j < cmds.size(); ++j) out.emplace_back(cmds[i], cmds[j]);
    return out;
}

std::map<std::string, std::string> applyPreset(const std::map<std::string, std::string>& current, const HotkeyPreset& preset) {
    std::map<std::string, std::string> out = current;
    for (const auto& [cmd, key] : preset.keys) out[cmd] = key;
    return out;
}

}  // namespace iw8r
