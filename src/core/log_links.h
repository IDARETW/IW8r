#pragma once
// Log links (roadmap 7.13): finds the entity, asset or script position a Replay or zonetool log line names, so the UI can
// underline it and select the entity or asset it refers to. Parsing only; the UI owns the widgets.
//
// Entity numbers: the editor's warnings ("LEAK from info_player_start (entity 1)", "entity 12: brush skipped") count
// map.entities by index (compile_iw8.cpp, gameplay_checks.cpp, leak_check.cpp), not Entity::id, which is a parser
// counter shared with items. So a plain number resolves by index; Entity::id is used only when LogRef::entityIsId is set.
#include <cstddef>
#include <string>
#include <vector>

#include "map_text.h"

namespace iw8r {

struct LogRef {
    enum Kind { Entity, Targetname, Model, Material, Image, Effect, Sound, ScriptLine, File };
    Kind kind = Entity;
    std::string value;          // entity number, name or path, as written in the log (quotes and trailing '.' removed)
    int line = 0;               // ScriptLine only: 1-based line of the .gsc file
    size_t begin = 0, end = 0;  // span [begin, end) in the log line, for underlining
    bool entityIsId = false;    // Entity only: the number is an Entity::id rather than an index into map.entities
};

// Finds the references of one log line, sorted by span. Overlapping matches keep the first one. Conservative: a bare name
// after a keyword is taken only when it looks like an asset name (it contains '_', '/' or '.'), so ordinary words
// ("model cache", "material is") never link.
std::vector<LogRef> findLogRefs(const std::string& logLine);

// The ids the reference names in `map`. Empty when nothing matches, and always empty for ScriptLine, File and Image
// (the UI opens the file instead). Names compare case-insensitively for materials, effects and sound aliases (the engine
// lower-cases them) and exactly for targetnames and models.
std::vector<Id> resolveLogRef(const MapFile& map, const LogRef& ref);

}  // namespace iw8r
