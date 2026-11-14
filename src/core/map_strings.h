// A map's own text for its script (ROADMAP 4.27, research 36 section 8.2.2): hint strings and HUD labels.
//
// The file "<map name>.str" beside the map, in the form mappers know from the older games:
//
//     REFERENCE       HINT_OPEN_GATE
//     LANG_ENGLISH    "Press ^3[{+activate}]^7 to open the gate"
//
// Each pair becomes a localized string named "<MAP ID IN CAPITALS>/<REFERENCE>" in the map's eng_ zone (the build
// writes extracted/localize/strings.tsv, the converter registers the entries). The map script uses it as
//     trigger sethintstring( &"MP_ARENA/HINT_OPEN_GATE" );
#pragma once

#include <string>
#include <utility>
#include <vector>

namespace iw8r {

struct MapString {
    std::string key;   // "<PREFIX>/<REFERENCE>"
    std::string text;  // as shown ("\n" in the file is a line break here)
};

inline constexpr size_t kMaxMapStrings = 512;

// Parses the text of a .str file. `prefix` is the map id; it is written in capitals. A REFERENCE is letters, digits
// and '_' (written in capitals). Lines starting with "//" and blank lines are skipped; other LANG_ lines than
// LANG_ENGLISH are ignored (the build has one language). Left out, each with a line in *warnings: a reference
// without a LANG_ENGLISH line, a reference twice, a name with other characters, text without its quotes, an empty
// text, and entries past kMaxMapStrings. Sorted by key.
std::vector<MapString> parseMapStrings(const std::string& strText, const std::string& prefix, std::vector<std::string>* warnings = nullptr);

// The extraction file: "KEY<TAB>text" per line; in the text a line break is written "\n" and a backslash "\\".
std::string mapStringsTsv(const std::vector<MapString>& strings);

// The file for a map: "<dir>/<map base name>.str". Empty for an empty path.
std::string mapStringsFileFor(const std::string& mapPath);

// The localized names a script uses (&"KEY"), in capitals, sorted, each once.
std::vector<std::string> stringRefsInScript(const std::string& scriptText);

// A new file's text: a short explanation as comments and one example entry.
std::string mapStringsTemplate(const std::string& prefix);

}  // namespace iw8r
