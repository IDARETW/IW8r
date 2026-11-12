// Roadmap 6.23: traversals for bots (node_negotiation_begin / _end with a stock animation).
#include "traversal.h"
#include "link_graph.h"
#include "map_text.h"
#include "test_harness.h"

#include <cmath>

using namespace iw8r;

TEST(traversal_stock_anims_and_pair) {
    CHECK(stockTraversalAnims().size() == 9 && findTraversalAnim("jump_down_80") && !findTraversalAnim("wall_run"));
    const TraversalAnim* down = findTraversalAnim("jump_down_80");
    if (!down) return;
    const auto [begin, end] = traversalPairKeys(*down, Vec3(100, 200, 64), 90, "traverse_1");
    const auto value = [](const std::vector<std::pair<std::string, std::string>>& kv, const std::string& k) {
        for (const auto& [key, v] : kv)
            if (key == k) return v;
        return std::string();
    };
    CHECK(value(begin, "classname") == "node_negotiation_begin" && value(begin, "animscript") == "jump_down_80");
    CHECK(value(begin, "target") == "traverse_1_end" && value(end, "targetname") == "traverse_1_end");
    CHECK(value(begin, "origin") == "100 200 64" && value(begin, "angles") == "0 90 0");
    // Facing +Y (yaw 90): 48 forward becomes +48 in y; 80 down.
    CHECK(value(end, "origin") == "100 248 -16");
    // The pair passes the link checks; a begin with another animation warns, one without errs.
    std::string text = "iwmap 4\n";
    const auto add = [&](const std::vector<std::pair<std::string, std::string>>& kv) {
        text += "{\n";
        for (const auto& [k, v] : kv) text += "\"" + k + "\" \"" + v + "\"\n";
        text += "}\n";
    };
    add(begin);
    add(end);
    add({{"classname", "node_negotiation_begin"}, {"target", "traverse_1_end"}, {"animscript", "wall_run_left"}, {"origin", "0 0 0"}});
    add({{"classname", "node_negotiation_begin"}, {"target", "traverse_1_end"}, {"origin", "0 0 0"}});
    Id next = 1;
    ParseResult pr = parseMap(text, next);
    CHECK(pr.map.has_value());
    if (!pr.map) return;
    const auto issues = checkLinks(*pr.map, buildLinkGraph(*pr.map));
    bool unknown = false, missing = false, pairOk = true;
    for (const auto& i : issues) {
        unknown |= !i.error && i.message.find("wall_run_left") != std::string::npos;
        missing |= i.error && i.message.find("no animscript") != std::string::npos;
        pairOk &= i.entity != pr.map->entities[0].id;
    }
    CHECK(unknown && missing && pairOk);
}
