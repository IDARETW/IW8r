// IW8 Radiant - in-app help (roadmap 9.11): a searchable index over the Markdown guides, and the guided first-map
// checklist (each step ticks itself from the map's own state).
#pragma once

#include <string>
#include <vector>

#include "map_text.h"

namespace iw8r {

struct HelpSection {
    std::string document;  // e.g. "USER_GUIDE.md"
    std::string title;     // heading text ("" for the text before the first heading)
    std::string path;      // "Guide > Building > Compile" (headings above it, joined with " > ")
    std::string body;      // Markdown of the section (heading line excluded)
};

// Splits Markdown into one section per heading (#..######). Fenced code blocks are kept in the body and their '#'
// lines are not headings.
std::vector<HelpSection> splitMarkdownSections(const std::string& document, const std::string& markdown);

struct HelpHit {
    size_t section = 0;   // index into the sections searched
    double score = 0;
    std::string snippet;  // ~160 characters around the first match, single line
};

// Every term (whitespace separated, case-insensitive) must appear in the title or the body. Title matches weigh more,
// whole-word matches more than partial ones. Best first; at most `limit` hits.
std::vector<HelpHit> searchHelp(const std::vector<HelpSection>& sections, const std::string& query, size_t limit = 50);

// Guided first map: the steps of docs/TUTORIAL.md as checks on the open map. `compiled` / `deployed` come from the
// editor (last build ok, package installed).
struct FirstMapStep {
    std::string title;  // "Floor"
    std::string hint;   // how to do it in the editor
    std::string command;  // editor command name that helps ("" = none), e.g. "GenerateSpawns"
    bool done = false;
};
std::vector<FirstMapStep> firstMapSteps(const MapFile& map, bool compiled, bool deployed);

}  // namespace iw8r
