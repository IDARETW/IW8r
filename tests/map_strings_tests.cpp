// A map's own text for its script (core map_strings, ROADMAP 4.27).
#include "map_strings.h"

#include <string>
#include <vector>

#include "test_harness.h"

using namespace iw8r;

TEST(map_strings_parse_pairs) {
    const std::string text =
        "// a comment\n"
        "\n"
        "REFERENCE       HINT_OPEN_GATE\r\n"
        "LANG_ENGLISH    \"Press ^3[{+activate}]^7 to open the gate\"\r\n"
        "LANG_GERMAN     \"wird nicht gebaut\"\n"
        "reference title\n"
        "lang_english \"Line one\\nLine \\\"two\\\" \\\\ end\"\n"
        "ENDMARKER\n";
    std::vector<std::string> warnings;
    const auto s = parseMapStrings(text, "mp_arena", &warnings);
    CHECK(warnings.empty());
    CHECK(s.size() == 2);
    if (s.size() != 2) return;
    CHECK(s[0].key == "MP_ARENA/HINT_OPEN_GATE" && s[0].text == "Press ^3[{+activate}]^7 to open the gate");
    CHECK(s[1].key == "MP_ARENA/TITLE" && s[1].text == "Line one\nLine \"two\" \\ end");
    // The extraction file: a tab between key and text, the line break and the backslash escaped.
    CHECK(mapStringsTsv(s) == "MP_ARENA/HINT_OPEN_GATE\tPress ^3[{+activate}]^7 to open the gate\n"
                              "MP_ARENA/TITLE\tLine one\\nLine \"two\" \\\\ end\n");
}

TEST(map_strings_parse_problems_are_named) {
    const std::string text =
        "REFERENCE NO_TEXT\n"
        "REFERENCE bad name!\n"
        "LANG_ENGLISH \"belongs to nothing\"\n"
        "REFERENCE DUP\n"
        "LANG_ENGLISH \"first\"\n"
        "REFERENCE DUP\n"
        "LANG_ENGLISH \"second\"\n"
        "REFERENCE NOQUOTES\n"
        "LANG_ENGLISH no quotes here\n"
        "REFERENCE EMPTY\n"
        "LANG_ENGLISH \"\"\n"
        "REFERENCE LAST_WITHOUT_TEXT\n";
    std::vector<std::string> warnings;
    const auto s = parseMapStrings(text, "mp_x", &warnings);
    CHECK(s.size() == 1);
    if (s.size() == 1) CHECK(s[0].key == "MP_X/DUP" && s[0].text == "first");
    CHECK(warnings.size() == 6);  // no text, bad name, twice, no quotes, empty, last without text
    // Nothing at all.
    CHECK(parseMapStrings("", "mp_x").empty());
    CHECK(parseMapStrings("// only a comment\n", "mp_x").empty());
}

TEST(map_strings_limit) {
    std::string text;
    for (size_t i = 0; i < kMaxMapStrings + 2; ++i) text += "REFERENCE N" + std::to_string(i) + "\nLANG_ENGLISH \"t\"\n";
    std::vector<std::string> warnings;
    CHECK(parseMapStrings(text, "mp_x", &warnings).size() == kMaxMapStrings);
    CHECK(warnings.size() == 2);
}

TEST(map_strings_script_names_and_template) {
    const std::string script = "user_main() {\n  t sethintstring( &\"mp_arena/Hint_Open_Gate\" );\n  h settext(&\"MP_ARENA/TITLE\");\n"
                               "  h settext(&\"MP_ARENA/TITLE\");\n  x = \"plain text\";\n  y = &\"MP/STOCK_ONE\";\n}\n";
    const auto names = stringRefsInScript(script);
    CHECK(names.size() == 3);
    if (names.size() == 3) CHECK(names[0] == "MP/STOCK_ONE" && names[1] == "MP_ARENA/HINT_OPEN_GATE" && names[2] == "MP_ARENA/TITLE");
    // The template of a new file parses to its one example, under the map's id.
    const auto t = parseMapStrings(mapStringsTemplate("mp_arena"), "mp_arena");
    CHECK(t.size() == 1);
    if (t.size() == 1) CHECK(t[0].key == "MP_ARENA/HINT_EXAMPLE" && !t[0].text.empty());
    // The file of a map.
    CHECK(mapStringsFileFor("").empty());
    const std::string f = mapStringsFileFor("C:/maps/arena.map");
    CHECK(f.size() > 9 && f.substr(f.size() - 9) == "arena.str");
}
