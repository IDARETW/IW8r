#include "map_strings.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <map>
#include <set>

namespace iw8r {

namespace {

std::string upper(std::string s) {
    for (char& c : s) c = char(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

std::string trim(const std::string& s) {
    const size_t a = s.find_first_not_of(" \t\r");
    if (a == std::string::npos) return {};
    return s.substr(a, s.find_last_not_of(" \t\r") - a + 1);
}

bool nameOk(const std::string& s) {
    if (s.empty() || s.size() > 64) return false;
    for (const char ch : s) {
        const unsigned char c = static_cast<unsigned char>(ch);
        if (!(std::isalnum(c) && c < 128) && ch != '_') return false;
    }
    return true;
}

}  // namespace

std::vector<MapString> parseMapStrings(const std::string& strText, const std::string& prefix, std::vector<std::string>* warnings) {
    const auto warn = [&](const std::string& line) {
        if (warnings) warnings->push_back(line);
    };
    const std::string pre = upper(prefix);
    std::map<std::string, std::string> found;  // key -> text (sorted)
    std::string ref;                           // the REFERENCE waiting for its LANG_ENGLISH
    bool refHasText = false;
    size_t lineNo = 0, pos = 0;
    const auto closeRef = [&] {
        if (!ref.empty() && !refHasText) warn("text file: " + ref + " has no LANG_ENGLISH line; left out");
        ref.clear();
        refHasText = false;
    };
    while (pos <= strText.size()) {
        size_t end = strText.find('\n', pos);
        if (end == std::string::npos) end = strText.size();
        const std::string line = trim(strText.substr(pos, end - pos));
        pos = end + 1;
        ++lineNo;
        if (line.empty() || line.rfind("//", 0) == 0) continue;
        const size_t sp = line.find_first_of(" \t");
        const std::string word = upper(sp == std::string::npos ? line : line.substr(0, sp));
        const std::string rest = sp == std::string::npos ? std::string() : trim(line.substr(sp));
        if (word == "REFERENCE") {
            closeRef();
            const std::string name = upper(rest);
            if (!nameOk(name)) {
                warn("text file line " + std::to_string(lineNo) + ": the name '" + rest + "' must be letters, digits and _ (64 at most); left out");
                continue;
            }
            if (found.count(pre + "/" + name)) {
                warn("text file line " + std::to_string(lineNo) + ": " + name + " is there twice; the first one is kept");
                continue;
            }
            ref = name;
        } else if (word == "LANG_ENGLISH") {
            if (ref.empty() || refHasText) continue;  // belongs to a reference that was left out
            if (rest.size() < 2 || rest.front() != '"' || rest.back() != '"') {
                warn("text file line " + std::to_string(lineNo) + ": the text of " + ref + " must be in double quotes; left out");
                ref.clear();
                continue;
            }
            // \n is a line break, \" a quote, \\ a backslash; everything else as written (colour codes, key names).
            std::string text;
            for (size_t i = 1; i + 1 < rest.size(); ++i) {
                if (rest[i] == '\\' && i + 2 < rest.size() && (rest[i + 1] == 'n' || rest[i + 1] == '"' || rest[i + 1] == '\\')) {
                    text.push_back(rest[i + 1] == 'n' ? '\n' : rest[i + 1]);
                    ++i;
                } else {
                    text.push_back(rest[i]);
                }
            }
            if (text.empty()) {
                warn("text file line " + std::to_string(lineNo) + ": " + ref + " has no text; left out");
                ref.clear();
                continue;
            }
            if (found.size() >= kMaxMapStrings) {
                warn("text file: more than " + std::to_string(kMaxMapStrings) + " entries; " + ref + " left out");
                ref.clear();
                continue;
            }
            found[pre + "/" + ref] = text;
            refHasText = true;
        }
        // Other LANG_ lines, ENDMARKER and the header words of the old tools are ignored.
    }
    closeRef();
    std::vector<MapString> out;
    out.reserve(found.size());
    for (const auto& [key, text] : found) out.push_back({key, text});
    return out;
}

std::string mapStringsTsv(const std::vector<MapString>& strings) {
    std::string out;
    for (const MapString& s : strings) {
        out += s.key;
        out += '\t';
        for (const char c : s.text) {
            if (c == '\n') out += "\\n";
            else if (c == '\\') out += "\\\\";
            else if (c == '\t' || c == '\r') out += ' ';
            else out += c;
        }
        out += '\n';
    }
    return out;
}

std::string mapStringsFileFor(const std::string& mapPath) {
    if (mapPath.empty()) return {};
    const std::filesystem::path p(mapPath);
    return (p.parent_path() / (p.stem().string() + ".str")).string();
}

std::vector<std::string> stringRefsInScript(const std::string& scriptText) {
    std::set<std::string> names;
    size_t pos = 0;
    while ((pos = scriptText.find("&\"", pos)) != std::string::npos) {
        const size_t close = scriptText.find('"', pos + 2);
        if (close == std::string::npos) break;
        const std::string name = scriptText.substr(pos + 2, close - pos - 2);
        pos = close + 1;
        if (!name.empty() && name.find_first_of(" \t\r\n") == std::string::npos) names.insert(upper(name));
    }
    return std::vector<std::string>(names.begin(), names.end());
}

std::string mapStringsTemplate(const std::string& prefix) {
    const std::string pre = upper(prefix);
    return "// Text for this map's script. Each entry is a REFERENCE line and a LANG_ENGLISH line.\n"
           "// In the script the entry below is  &\"" + pre + "/HINT_EXAMPLE\"\n"
           "// for example:  trigger sethintstring( &\"" + pre + "/HINT_EXAMPLE\" );\n"
           "// \\n in the text is a line break. ^1 .. ^7 are the game's colour codes, [{+activate}] shows the key.\n"
           "\n"
           "REFERENCE       HINT_EXAMPLE\n"
           "LANG_ENGLISH    \"Press ^3[{+activate}]^7 to use\"\n";
}

}  // namespace iw8r
