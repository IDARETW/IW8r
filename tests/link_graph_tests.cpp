// Link graph (target/targetname, script_linkto/script_linkname): edges, checks, chains and connectivity.
#include <string>
#include <utility>
#include <vector>

#include "link_graph.h"
#include "map_text.h"
#include "test_harness.h"

using namespace iw8r;

namespace {

using KV = std::vector<std::pair<std::string, std::string>>;

std::string ent(const KV& kv) {
    std::string t = "{\n";
    for (const auto& [k, v] : kv) t += "\"" + k + "\" \"" + v + "\"\n";
    return t + "}\n";
}

MapFile parseText(const std::string& body) {
    Id next = 1;
    ParseResult pr = parseMap("iwmap 4\n" + body, next);
    CHECK(pr.map.has_value());
    return pr.map ? *pr.map : MapFile{};
}

size_t countIssues(const std::vector<LinkIssue>& issues, bool error) {
    size_t n = 0;
    for (const LinkIssue& i : issues) n += i.error == error;
    return n;
}

bool edgeIs(const LinkGraph& g, size_t i, Id from, Id to, const std::string& key) {
    if (i >= g.edges.size()) return false;
    const LinkEdge& e = g.edges[i];
    return e.from == from && e.to == to && e.key == key;
}

}  // namespace

TEST(link_graph_good_chain) {
    const MapFile m = parseText(ent({{"classname", "script_struct"}, {"targetname", "a"}, {"target", "b"}}) +
                                ent({{"classname", "script_struct"}, {"targetname", "b"}, {"target", "c"}}) +
                                ent({{"classname", "script_struct"}, {"targetname", "c"}}));
    CHECK(m.entities.size() == 3);
    if (m.entities.size() != 3) return;
    const Id a = m.entities[0].id, b = m.entities[1].id, c = m.entities[2].id;
    const LinkGraph g = buildLinkGraph(m);
    CHECK(g.edges.size() == 2);
    CHECK(edgeIs(g, 0, a, b, "target"));
    CHECK(edgeIs(g, 1, b, c, "target"));
    CHECK(g.byTargetname.size() == 3);
    CHECK(checkLinks(m, g).empty());
    CHECK(targetChains(g) == (std::vector<std::vector<Id>>{{a, b, c}}));
    CHECK(connectedTo(g, b) == (std::vector<Id>{a, c}));
    CHECK(connectedTo(g, a) == (std::vector<Id>{b, c}));
}

TEST(link_graph_missing_target) {
    const MapFile m = parseText(ent({{"classname", "trigger_multiple"}, {"targetname", "t"}, {"target", "nope"}}) +
                                ent({{"classname", "script_struct"}, {"targetname", "unused"}}));
    if (m.entities.size() != 2) {
        CHECK(false);
        return;
    }
    const LinkGraph g = buildLinkGraph(m);
    CHECK(g.edges.empty());
    const auto issues = checkLinks(m, g);
    // Only the dangling target is reported; a targetname nothing points at is not.
    CHECK(issues.size() == 1);
    CHECK(countIssues(issues, true) == 1);
    CHECK(!issues.empty() && issues[0].entity == m.entities[0].id);
    CHECK(targetChains(g).empty());
}

TEST(link_graph_cycle) {
    // A <-> B is a loop; X feeds into it. The loop is reported once, on its head (A, the first entity the walk visits).
    const MapFile m = parseText(ent({{"classname", "script_struct"}, {"targetname", "a"}, {"target", "b"}}) +
                                ent({{"classname", "script_struct"}, {"targetname", "b"}, {"target", "a"}}) +
                                ent({{"classname", "script_struct"}, {"targetname", "x"}, {"target", "a"}}));
    if (m.entities.size() != 3) {
        CHECK(false);
        return;
    }
    const Id a = m.entities[0].id, b = m.entities[1].id, x = m.entities[2].id;
    const LinkGraph g = buildLinkGraph(m);
    CHECK(g.edges.size() == 3);
    const auto issues = checkLinks(m, g);
    CHECK(issues.size() == 1);
    CHECK(countIssues(issues, false) == 1);
    CHECK(!issues.empty() && issues[0].entity == a && !issues[0].error);
    // The pure loop has no start; the chain from X stops before it repeats.
    CHECK(targetChains(g) == (std::vector<std::vector<Id>>{{x, a, b}}));
    CHECK(connectedTo(g, a) == (std::vector<Id>{b, x}));
}

TEST(link_graph_self_target) {
    const MapFile m = parseText(ent({{"classname", "script_struct"}, {"targetname", "a"}, {"target", "a"}}));
    if (m.entities.size() != 1) {
        CHECK(false);
        return;
    }
    const LinkGraph g = buildLinkGraph(m);
    CHECK(g.edges.size() == 1);
    CHECK(edgeIs(g, 0, m.entities[0].id, m.entities[0].id, "target"));
    const auto issues = checkLinks(m, g);
    // A self-link is a warning (not a cycle error): one issue, not an error.
    CHECK(issues.size() == 1);
    CHECK(countIssues(issues, true) == 0);
    CHECK(targetChains(g).empty());
}

TEST(link_graph_multi_target) {
    const MapFile m = parseText(ent({{"classname", "trigger_multiple"}, {"targetname", "t"}, {"target", "b"}, {"target2", "c"}, {"target3", "nope"}}) +
                                ent({{"classname", "script_struct"}, {"targetname", "b"}}) +
                                ent({{"classname", "script_struct"}, {"targetname", "c"}}));
    if (m.entities.size() != 3) {
        CHECK(false);
        return;
    }
    const Id t = m.entities[0].id, b = m.entities[1].id, c = m.entities[2].id;
    const LinkGraph g = buildLinkGraph(m);
    CHECK(g.edges.size() == 2);
    CHECK(edgeIs(g, 0, t, b, "target"));
    CHECK(edgeIs(g, 1, t, c, "target2"));
    const auto issues = checkLinks(m, g);
    CHECK(issues.size() == 1);
    CHECK(countIssues(issues, true) == 1);  // target3 names nothing
    CHECK(!issues.empty() && issues[0].entity == t);
    CHECK(connectedTo(g, t) == (std::vector<Id>{b, c}));
    CHECK(connectedTo(g, b) == (std::vector<Id>{t, c}));  // b <- t -> c, followed in both directions
}

TEST(link_graph_script_linkto) {
    // Heli struct H1 names two linknames (space-separated, double space tolerated); H2 names one that does not exist.
    const MapFile m = parseText(ent({{"classname", "script_struct_heli"}, {"targetname", "h1"}, {"script_linkto", "p1  p2"}}) +
                                ent({{"classname", "script_struct"}, {"script_linkname", "p1"}}) +
                                ent({{"classname", "script_struct"}, {"script_linkname", "p2"}}) +
                                ent({{"classname", "script_struct_heli"}, {"targetname", "h2"}, {"script_linkto", "p1 nothere"}}));
    if (m.entities.size() != 4) {
        CHECK(false);
        return;
    }
    const Id h1 = m.entities[0].id, p1 = m.entities[1].id, p2 = m.entities[2].id, h2 = m.entities[3].id;
    const LinkGraph g = buildLinkGraph(m);
    CHECK(g.edges.size() == 3);
    CHECK(edgeIs(g, 0, h1, p1, "script_linkto"));
    CHECK(edgeIs(g, 1, h1, p2, "script_linkto"));
    CHECK(edgeIs(g, 2, h2, p1, "script_linkto"));
    const auto issues = checkLinks(m, g);
    CHECK(issues.size() == 1);
    CHECK(countIssues(issues, true) == 1);  // 'nothere'
    CHECK(!issues.empty() && issues[0].entity == h2);
    // The linkname holders have no targetname, so the chains start at the heli structs.
    CHECK(targetChains(g) == (std::vector<std::vector<Id>>{{h1, p1}, {h1, p2}, {h2, p1}}));
}

TEST(link_graph_negotiation_pair) {
    const MapFile good = parseText(ent({{"classname", "node_negotiation_begin"}, {"targetname", "nb"}, {"target", "ne"}}) +
                                   ent({{"classname", "node_negotiation_end"}, {"targetname", "ne"}}));
    if (good.entities.size() != 2) {
        CHECK(false);
        return;
    }
    const LinkGraph gg = buildLinkGraph(good);
    CHECK(edgeIs(gg, 0, good.entities[0].id, good.entities[1].id, "target"));
    CHECK(checkLinks(good, gg).empty());

    // Begin aimed at a non-end, and a begin with no target: both lack a matching end.
    const MapFile bad = parseText(ent({{"classname", "node_negotiation_begin"}, {"targetname", "nb2"}, {"target", "s"}}) +
                                  ent({{"classname", "script_struct"}, {"targetname", "s"}}) +
                                  ent({{"classname", "node_negotiation_begin"}, {"targetname", "nb3"}}));
    if (bad.entities.size() != 3) {
        CHECK(false);
        return;
    }
    const LinkGraph gb = buildLinkGraph(bad);
    const auto issues = checkLinks(bad, gb);
    CHECK(issues.size() == 2);
    CHECK(countIssues(issues, true) == 2);
    CHECK(issues.size() == 2 && issues[0].entity == bad.entities[0].id && issues[1].entity == bad.entities[2].id);
}

TEST(link_graph_ambiguous_trigger_target) {
    // Two entities share targetname d. A trigger warns; a script_struct linking to the same name does not.
    const MapFile m = parseText(ent({{"classname", "trigger_multiple"}, {"target", "d"}}) +
                                ent({{"classname", "script_struct"}, {"targetname", "d"}}) +
                                ent({{"classname", "script_struct"}, {"targetname", "d"}}) +
                                ent({{"classname", "script_struct"}, {"target", "d"}}));
    if (m.entities.size() != 4) {
        CHECK(false);
        return;
    }
    const LinkGraph g = buildLinkGraph(m);
    CHECK(g.edges.size() == 4);  // T -> D1, D2 and S -> D1, D2
    const auto issues = checkLinks(m, g);
    CHECK(issues.size() == 1);
    CHECK(countIssues(issues, false) == 1);
    CHECK(!issues.empty() && issues[0].entity == m.entities[0].id);
}

TEST(link_graph_chains_output_order) {
    // Starts (no incoming edge): A (0), D (3), E (4). Each path ends at a leaf. The G <-> H loop has no start.
    const MapFile m = parseText(ent({{"classname", "script_struct"}, {"targetname", "a1"}, {"target", "m1"}}) +   // 0
                                ent({{"classname", "script_struct"}, {"targetname", "m1"}, {"target", "z1"}}) +   // 1
                                ent({{"classname", "script_struct"}, {"targetname", "z1"}}) +                      // 2
                                ent({{"classname", "script_struct"}, {"targetname", "d1"}, {"target", "m1"}}) +   // 3
                                ent({{"classname", "script_struct"}, {"targetname", "e1"}, {"target", "f1"}, {"target2", "f2"}}) +  // 4
                                ent({{"classname", "script_struct"}, {"targetname", "f1"}}) +                     // 5
                                ent({{"classname", "script_struct"}, {"targetname", "f2"}}) +                     // 6
                                ent({{"classname", "script_struct"}, {"targetname", "g"}, {"target", "h"}}) +     // 7
                                ent({{"classname", "script_struct"}, {"targetname", "h"}, {"target", "g"}}));     // 8
    if (m.entities.size() != 9) {
        CHECK(false);
        return;
    }
    std::vector<Id> id;
    for (const Entity& e : m.entities) id.push_back(e.id);
    const LinkGraph g = buildLinkGraph(m);
    const std::vector<std::vector<Id>> expected = {
        {id[0], id[1], id[2]},  // A -> M -> Z
        {id[3], id[1], id[2]},  // D -> M -> Z
        {id[4], id[5]},         // E -> F1 (target, first)
        {id[4], id[6]},         // E -> F2 (target2)
    };
    CHECK(targetChains(g) == expected);
    // Same graph built twice gives the same chains.
    CHECK(targetChains(buildLinkGraph(m)) == expected);
}

TEST(link_graph_isolated_entity) {
    const MapFile m = parseText(ent({{"classname", "script_struct"}, {"targetname", "lonely"}}));
    if (m.entities.size() != 1) {
        CHECK(false);
        return;
    }
    const LinkGraph g = buildLinkGraph(m);
    CHECK(connectedTo(g, m.entities[0].id).empty());
    CHECK(connectedTo(g, 999999).empty());
    CHECK(targetChains(g).empty());
}
