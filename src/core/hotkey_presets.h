#pragma once
// Hotkey presets (roadmap 2.30): editor command ids mapped to portable Qt key strings ("Ctrl+Shift+P"). The data lives in
// data/hotkey_presets.json; its "iw8radiant" preset is generated from src/qtapp/main_window.cpp by
// tools/extract_default_hotkeys.py. Pure data and logic: no Qt, no UI.

#include <map>
#include <string>
#include <utility>
#include <vector>

namespace iw8r {

struct HotkeyPreset {
    std::string id, name, source;
    std::map<std::string, std::string> keys;  // command id -> key text as written in the file; "" = no key
};

// Parses the presets file text. Returns false and sets *err (when given) on bad JSON, a missing or duplicate id,
// a missing name or keys object, a non-string key value, or a key that does not parse. `source` may be absent.
bool loadHotkeyPresets(const std::string& jsonText, std::vector<HotkeyPreset>& out, std::string* err);

// Canonical form of a portable key: modifiers in Ctrl, Alt, Shift, Meta order, then the key ("Shift+Ctrl+f" -> "Ctrl+Shift+F").
// "" stays "". Text that does not parse is returned unchanged.
std::string canonicalKey(const std::string& key);

// Pairs of command ids that share one key inside this preset (all pairs, each pair sorted, in key order). Unbound
// commands ("") never conflict. Only the preset's own keys are compared; use applyPreset() onto the defaults to check
// what a user would actually get.
std::vector<std::pair<std::string, std::string>> presetConflicts(const HotkeyPreset& preset);

// `current` with every key the preset lists written over it. Commands the preset does not list keep their current key;
// a "" in the preset unbinds the command. Returns a new map; `current` is not changed.
std::map<std::string, std::string> applyPreset(const std::map<std::string, std::string>& current, const HotkeyPreset& preset);

}  // namespace iw8r
