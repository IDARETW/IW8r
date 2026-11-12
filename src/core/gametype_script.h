// IW8 Radiant Suite - a new game type script carried by a map (roadmap 13.6; docs/research/91 and 92).
//
// Read from the game exe: a map zone cannot replace a stock game type script (the stock copy wins), but a game type
// with a new name can be started and its script, scripts/mp/gametypes/<name>.gsc, is then loaded by name from any
// loaded zone, the map zone included (research 91). Far calls in that script link at load by the exact asset name: a
// stock file the game stores by number links (gsc-tool writes the same number); a stock file stored under its path
// string does not link reliably (research 92: the function name forms differ), and a call that fails to link drops the
// match or exits the game. So a game type script may call only its own functions, builtins and stock files stored by
// number (data/generated/iw8_stock_scripts.tsv).
//
// The map names the script with worldspawn iw8r_gametype_script "<file relative to the map folder>"; the file name
// (without .gsc) is the game type name. The build checks it, copies it into the extraction
// (extracted/scripts/mp/gametypes/<name>.gsc, compiled by the converter as it is) and lists the name in map.json.
// Pure core code.
#pragma once

#include <map>
#include <string>
#include <vector>

namespace iw8r {

inline constexpr const char* kGameTypeScriptKey = "iw8r_gametype_script";

// The stock GSC files of the always-loaded zones and how each is stored (tools/build_stock_script_index.py).
class StockScriptIndex {
public:
    enum class Form { Unknown, Numbered, Named };
    bool parse(const std::string& tsv);  // path<TAB>numbered|named<TAB>number<TAB>zone; '#' lines are comments
    bool load(const std::string& path, std::string* err = nullptr);
    // `path` as written in a far call: backslashes or slashes, any case, with or without ".gsc".
    Form form(const std::string& path) const;
    size_t size() const { return files_.size(); }

private:
    std::map<std::string, Form> files_;
};

// "scripts/mp/gametypes/foo" from "scripts\\MP\\gametypes\\foo.gsc".
std::string normalizedScriptPath(const std::string& path);

// The game type name of a script file ("gametypes/Team_Kills.gsc" -> "team_kills"); "" when the file name is not 1 to
// 11 characters of a-z, 0-9 and _ (the name is a ui_gametype value).
std::string gameTypeNameOfScript(const std::string& file);

// One far call of a script: line (1-based), the path as written and the function.
struct ScriptFarCall {
    int line = 0;
    std::string path, function;
};
// The far calls (path::function) of GSC text, comments and strings left out.
std::vector<ScriptFarCall> scriptFarCalls(const std::string& gsc);

// What stops the script from being built (empty when it can be): a bad or stock name, no main(), a far call into a
// stock file stored by name or into no stock file at all. Each line names the line number.
std::vector<std::string> checkGameTypeScript(const std::string& gsc, const std::string& name, const StockScriptIndex& index);

// A complete team game type to start from: kills score for the team up to the score limit, the stock Team Deathmatch
// spawns. It calls only numbered stock files (globallogic, gameobjects, utility\game, spawnlogic, gamescore).
std::string gameTypeScriptTemplate(const std::string& name);

}  // namespace iw8r
