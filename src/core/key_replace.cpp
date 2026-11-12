#include "key_replace.h"

#include <algorithm>
#include <map>

namespace iw8r {

namespace {

bool matches(const std::string& value, const std::string& find, bool substring) {
    return substring ? value.find(find) != std::string::npos : value == find;
}

std::string replaced(std::string value, const std::string& find, const std::string& replace, bool substring) {
    if (!substring) return replace;
    for (size_t at = value.find(find); at != std::string::npos; at = value.find(find, at + replace.size()))
        value.replace(at, find.size(), replace);
    return value;
}

}  // namespace

std::vector<std::pair<std::string, size_t>> keyValueUsage(const MapFile& map, const std::string& key) {
    std::map<std::string, size_t> counts;
    for (const auto& e : map.entities)
        if (const std::string* v = e.value(key)) ++counts[*v];
    std::vector<std::pair<std::string, size_t>> out(counts.begin(), counts.end());
    std::stable_sort(out.begin(), out.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
    return out;
}

std::set<Id> entitiesWithValue(const MapFile& map, const std::string& key, const std::string& find, bool substring) {
    std::set<Id> out;
    if (key.empty() || find.empty()) return out;
    for (const auto& e : map.entities)
        if (const std::string* v = e.value(key); v && matches(*v, find, substring)) out.insert(e.id);
    return out;
}

size_t replaceKeyValues(Document& doc, const std::string& key, const std::string& find, const std::string& replace, bool substring,
                        bool selectionOnly) {
    if (key.empty() || find.empty() || (key == "classname" && replace.empty())) return 0;
    std::set<Id> selected;
    for (const SelRef& r : doc.selection()) selected.insert(r.entity);
    std::vector<std::pair<Id, std::string>> edits;
    for (const auto& e : doc.map().entities) {
        if (selectionOnly && !selected.count(e.id)) continue;
        const std::string* v = e.value(key);
        if (!v || !matches(*v, find, substring)) continue;
        const std::string nv = replaced(*v, find, replace, substring);
        if (nv != *v) edits.push_back({e.id, nv});
    }
    if (edits.empty()) return 0;
    doc.beginGroup("Replace " + key);
    for (const auto& [id, value] : edits) doc.setKeyValue(id, key, value);
    doc.endGroup();
    return edits.size();
}

}  // namespace iw8r
