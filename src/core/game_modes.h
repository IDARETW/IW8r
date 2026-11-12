// IW8 Radiant Suite - game mode presets (owner scope 2026-11-12: "warzone-specific editing and map/mode creation";
// docs/research/87_warzone_survey.md sections 4 to 6).
//
// In this game a mode variant is not a script: it is a game type script (war, dom, br ...) plus dvars. Trios, Quads,
// Solos and Blood Money are all the script "br" with different dvars (research 87, 4.3). A private match does not
// apply playlist dvars, so a preset has to bring them: the editor sends them as console "set" lines when it starts a
// match, and a map can carry them (worldspawn iw8r_mode_dvars: the generated map script sets them at start, as the
// stock Warzone tutorial map does for its own).
//
// A preset can also say what a small map switches off in the br game type (no circle, no gulag, no drop from the
// plane), which the stock tutorial map does in its map script.
// Pure core code.
#pragma once

#include <string>
#include <utility>
#include <vector>

namespace iw8r {

struct GameModePreset {
    std::string id;           // [a-z0-9_], 1 to 24 characters
    std::string title;
    std::string gametype;     // the game type script name (ui_gametype): war, dm, dom, sd, ctf, koth, br ...
    std::vector<std::pair<std::string, std::string>> dvars;  // set before the match starts, in order
    std::string description;
    // For the br game type on a map that has no circle data, no gulag arena or no plane path.
    bool brNoCircle = false, brNoGulag = false, brSkipDrop = false;
    bool builtin = false;
};

// A dvar name is letters, digits and '_' (1 to 64); a value is 1 to 96 characters of letters, digits and "_./:-"
// (no spaces, quotes or semicolons: the value travels in a console line and in a script string).
bool validModeDvarName(const std::string& name);
bool validModeDvarValue(const std::string& value);
bool validGameModeId(const std::string& id);

// The presets that ship with the editor: the core game types without dvars, and the Warzone variants of the private
// server's playlist catalogue (research 87, table in 4.3) plus two for small custom maps.
const std::vector<GameModePreset>& builtinGameModes();

// One preset as JSON text ({"id","title","gametype","dvars":{...},"description","br":{"no_circle",...}}) and back.
// parse fails (false, *err) on a bad id, game type, dvar name or value.
std::string gameModePresetJson(const GameModePreset& preset);
bool parseGameModePreset(const std::string& json, GameModePreset& out, std::string* err = nullptr);

// The built-in presets followed by every "<name>.iw8mode.json" of the folder (a user preset with a built-in id
// replaces it). A file that does not parse is reported in `notes` and left out.
std::vector<GameModePreset> loadGameModes(const std::string& userDir, std::vector<std::string>* notes = nullptr);
const GameModePreset* findGameMode(const std::vector<GameModePreset>& modes, const std::string& id);

// "name=value; name=value" (the worldspawn iw8r_mode_dvars form) and back; bad pairs are dropped and named in `notes`.
std::string modeDvarsText(const std::vector<std::pair<std::string, std::string>>& dvars);
std::vector<std::pair<std::string, std::string>> parseModeDvars(const std::string& text, std::vector<std::string>* notes = nullptr);

// The worldspawn iw8r_br words of a preset ("nocircle nogulag skipdrop"; "" for none) and back.
std::string modeBrText(const GameModePreset& preset);
void parseModeBr(const std::string& text, bool& noCircle, bool& noGulag, bool& skipDrop);

}  // namespace iw8r
