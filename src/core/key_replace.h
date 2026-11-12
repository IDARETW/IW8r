// IW8 Radiant Suite - batch key/value tools (roadmap 8.7): model replace and find/replace of any entity key.
#pragma once

#include <set>
#include <string>
#include <utility>
#include <vector>

#include "document.h"

namespace iw8r {

// Every value of `key` in the map with how many entities use it, sorted by count (descending), then value.
std::vector<std::pair<std::string, size_t>> keyValueUsage(const MapFile& map, const std::string& key);

// Entities whose `key` matches `find` (whole value, or part of it with `substring`).
std::set<Id> entitiesWithValue(const MapFile& map, const std::string& key, const std::string& find, bool substring);

// Replaces matching values (the whole value, or every occurrence of `find` inside it with `substring`) on every entity,
// or only the selected ones. One undo step; returns the number of entities changed. An empty key or find changes nothing.
size_t replaceKeyValues(Document& doc, const std::string& key, const std::string& find, const std::string& replace, bool substring,
                        bool selectionOnly);

}  // namespace iw8r
