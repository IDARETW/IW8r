#include "help_index.h"
#include "test_harness.h"

#include <string>

using namespace iw8r;

TEST(help_sections_split_by_headings) {
    const std::string md =
        "Intro line\n# Guide\nTop text\n## Building\nCompile with F7.\n```\n# not a heading\n```\n### Compile\nDeploy too.\n## Lighting\nSun.\n";
    const auto s = splitMarkdownSections("USER_GUIDE.md", md);
    CHECK(s.size() == 5);
    if (s.size() != 5) return;
    CHECK(s[0].title.empty() && s[0].body.find("Intro") != std::string::npos);
    CHECK(s[2].title == "Building" && s[2].body.find("# not a heading") != std::string::npos);
    CHECK(s[3].path == "USER_GUIDE.md > Guide > Building > Compile");
    CHECK(s[4].path == "USER_GUIDE.md > Guide > Lighting");
}

TEST(help_search_ranks_and_requires_all_terms) {
    const auto s = splitMarkdownSections("G.md", "# Spawns\nPlace spawn points.\n# Lights\nA light near the spawn.\n# Sky\nNothing.\n");
    auto hits = searchHelp(s, "spawn");
    CHECK(hits.size() == 2);
    if (hits.size() == 2) CHECK(s[hits[0].section].title == "Spawns");  // title match weighs more
    CHECK(searchHelp(s, "light spawn").size() == 1);
    CHECK(searchHelp(s, "SKY").size() == 1);
    CHECK(searchHelp(s, "unknownword").empty());
    CHECK(searchHelp(s, "   ").empty());
    hits = searchHelp(s, "near");
    CHECK(hits.size() == 1 && hits[0].snippet.find("near the spawn") != std::string::npos);
}

TEST(help_first_map_steps_follow_the_map) {
    Id next = 1;
    auto pe = parseMap("iwmap 4\n// entity 0\n{\n\"classname\" \"worldspawn\"\n}\n", next);
    CHECK(pe.map.has_value());
    if (!pe.map) return;
    auto steps = firstMapSteps(*pe.map, false, false);
    CHECK(steps.size() == 9);
    for (const auto& st : steps) CHECK(!st.done);
    const std::string text =
        "iwmap 4\n// entity 0\n{\n\"classname\" \"worldspawn\"\n\"skyname\" \"sky_mp_shipment\"\n}\n"
        "// entity 1\n{\n\"classname\" \"mp_tdm_spawn\"\n\"origin\" \"0 0 0\"\n}\n"
        "// entity 2\n{\n\"classname\" \"mp_tdm_spawn\"\n\"origin\" \"64 0 0\"\n}\n"
        "// entity 3\n{\n\"classname\" \"mp_tdm_spawn_allies_start\"\n\"origin\" \"0 64 0\"\n}\n"
        "// entity 4\n{\n\"classname\" \"mp_tdm_spawn_axis_start\"\n\"origin\" \"0 -64 0\"\n}\n"
        "// entity 5\n{\n\"classname\" \"light\"\n\"origin\" \"0 0 64\"\n}\n";
    auto pm = parseMap(text, next);
    CHECK(pm.map.has_value());
    if (!pm.map) return;
    const MapFile& m = *pm.map;
    steps = firstMapSteps(m, true, false);
    auto done = [&](const std::string& t) {
        for (const auto& st : steps)
            if (st.title == t) return st.done;
        return false;
    };
    CHECK(done("Spawns") && done("Light") && done("Build"));
    CHECK(!done("Floor and walls") && !done("Play") && !done("Sky"));
}
