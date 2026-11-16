// IW8 Radiant - game-mode object templates (roadmap 6.2): Domination flags, Hardpoint zones, Search & Destroy
// sites and bomb, Capture the Flag, and spawn sets. Each template is a list of entities with the classnames and keys
// that the stock Replay 1.20 maps use (docs/research/32_gamemode_objects.md; the evidence and the UNKNOWN parts are there).
//
// Positions are relative to a placement point (floor level, inches, Z up). Point entities get "origin" = placement + offset.
// Trigger brushes are placeholder boxes (their stock sizes are UNKNOWN) given as an AABB relative to the placement point;
// the brush entity's "origin" is the centre of its box. Brush triggers use the "trigger" tool material, which the compiler
// turns into trigger hulls (compile_iw8.cpp).
#pragma once

#include <string>
#include <utility>
#include <vector>

#include "document.h"
#include "geometry.h"

namespace iw8r {

struct TemplateEntity {
    std::string classname;
    Vec3 offset;                                                  // point entity: origin = placement + offset
    std::vector<std::pair<std::string, std::string>> keys;        // written after classname (and origin), verbatim
    bool hasBrush = false;                                        // brush entity (trigger volume)
    AABB brush;                                                   // brush box relative to the placement point
};

struct GameModeTemplate {
    std::string id;           // "dom_flags": command GameModeTemplate_<id>
    std::string name;         // menu text
    std::string description;  // one line
    std::vector<TemplateEntity> entities;
};

// The built-in templates, in menu order.
const std::vector<GameModeTemplate>& gameModeTemplates();
// nullptr when no template has this id.
const GameModeTemplate* findGameModeTemplate(const std::string& id);

// iwmap 4 text of the template placed at `origin`: the "iwmap 4" header, then one "// entity N" block per entity
// (quoted keys, one per line, brush entities with their box brush). Every line ends with `eol`.
std::string templateMapText(const GameModeTemplate& t, const Vec3& origin, const std::string& eol);

// Inserts the template as ONE undo step (Document::beginGroup) and selects the new entities. Returns the new entity
// ids (empty and err set on failure, with the document unchanged).
std::vector<Id> insertGameModeTemplate(Document& doc, const GameModeTemplate& t, const Vec3& origin, std::string* err = nullptr);

}  // namespace iw8r
