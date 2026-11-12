#pragma once
// Time-of-day presets (roadmap 5.19): sun, sky, fog, film grade and exposure set together on worldspawn in one undo
// step (Compile > Time of Day). Values are starting points for the mapper to tune; the in-game look is NOT VERIFIED.
// The sky of each preset is the stock sky of a Replay map with that kind of light.
#include <string>
#include <utility>
#include <vector>

namespace iw8r {

class Document;

struct TodPreset {
    const char* id;
    const char* title;
    // Worldspawn keys the preset sets; an empty value removes the key (e.g. no fog indoors).
    std::vector<std::pair<std::string, std::string>> keys;
};

const std::vector<TodPreset>& todPresets();
const TodPreset* findTodPreset(const std::string& id);

// Applies the preset to worldspawn as ONE undo step. `keepSky`: leave skyname as it is. Returns false (and changes
// nothing) when the id is unknown, the map has no worldspawn, or every key already has the preset's value.
bool applyTodPreset(Document& doc, const std::string& id, bool keepSky = false);

}  // namespace iw8r
