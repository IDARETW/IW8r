// IW8 Radiant - stock scripted object catalog (roadmap 6.13): which entity classnames and key/value pairs the
// stock IW8 MP maps use for script-driven objects, and which stock script handles them. Core only: no UI. The data
// file is data/asset_catalog/iw8_scripted_objects.tsv, written by tools/build_scripted_objects.py. Research:
// docs/research/53_stock_scripted_objects.md.
#pragma once

#include <cstddef>
#include <map>
#include <string>
#include <vector>

namespace iw8r {

// One row of the catalog. Patterns use '*' as the only wildcard (any run of characters, including none).
struct ScriptedObjectKind {
    std::string kind;          // group name, e.g. "sd_site"; several rows can share one kind
    std::string classname;     // classname pattern, e.g. "trigger_use_touch" or "trigger_multiple_bcs_*"
    std::string key;           // entity key that must be present, or "-" when the classname alone decides
    std::string valuePattern;  // pattern the key's value must match ("*" matches any value)
    std::string handledBy;     // stock script evidence, e.g. "scripts/mp/gametypes/sd.gsc:470"
    std::string worksInCustom; // "yes", "no" or "unknown" (a reading of the stock scripts, not a game test)
    std::vector<std::string> maps;  // stock maps where the row was found
    int count = 0;             // stock entities the row matched when the catalog was built
};

class ScriptedObjectCatalog {
public:
    // Loads the TSV at path (replaces any previous content). Lines starting with '#' and blank lines are skipped.
    // Each data row has 8 tab-separated columns: kind, classname, key, value_pattern, handled_by,
    // works_in_custom_map, maps (comma list, may be empty), count (non-negative integer).
    // On a bad row, or an unreadable file, returns false, leaves the catalog empty and sets *err (when err is
    // non-null) to a message naming the line number.
    bool load(const std::string& path, std::string* err);

    // Returns every row whose classname pattern matches entityKeys["classname"] and whose key/value pattern matches
    // (key "-" means no key test). entityKeys maps entity key names to values, including "classname". Rows keep the
    // order of the file. The pointers stay valid until the next load() or until the catalog is destroyed.
    std::vector<const ScriptedObjectKind*> match(const std::map<std::string, std::string>& entityKeys) const;

    size_t size() const { return kinds_.size(); }

private:
    std::vector<ScriptedObjectKind> kinds_;
};

// '*' wildcard match of the whole text (no other metacharacters).
bool scriptedObjectWildcardMatch(const std::string& pattern, const std::string& text);

}  // namespace iw8r
