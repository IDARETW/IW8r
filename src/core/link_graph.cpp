// IW8 Radiant Suite - link graph over target/targetname and script_linkto/script_linkname (see link_graph.h).
#include "link_graph.h"

#include "traversal.h"

#include <algorithm>
#include <functional>
#include <set>

namespace iw8r {
namespace {

bool startsWith(const std::string& s, const std::string& p) { return s.compare(0, p.size(), p) == 0; }

bool isTargetKey(const std::string& k) {
    static const char* const kKeys[] = {"target", "target2", "target3", "target4", "target5", "target6", "target7", "target8"};
    for (const char* name : kKeys)
        if (k == name) return true;
    return false;
}

// Calls fn(key, value) for every non-empty link key of `e`, in fixed key order (target, target2..8, script_linkto).
// Keys are read through Entity::value, so canonical-id spellings resolve to the same names.
template <class Fn>
void forEachLinkKey(const Entity& e, Fn&& fn) {
    static const char* const kOrder[] = {"target", "target2", "target3", "target4", "target5",
                                         "target6", "target7", "target8", "script_linkto"};
    for (const char* key : kOrder) {
        const std::string* v = e.value(key);
        if (v && !v->empty()) fn(std::string(key), *v);
    }
}

}  // namespace

LinkGraph buildLinkGraph(const MapFile& map) {
    LinkGraph g;
    std::map<std::string, std::vector<Id>> linknames;  // script_linkname -> entities
    for (const Entity& e : map.entities) {
        if (const std::string* t = e.value("targetname"); t && !t->empty()) g.byTargetname[*t].push_back(e.id);
        if (const std::string* n = e.value("script_linkname"); n && !n->empty()) linknames[*n].push_back(e.id);
    }
    for (const Entity& e : map.entities) {
        forEachLinkKey(e, [&](const std::string& key, const std::string& value) {
            if (key == "script_linkto") {
                // One edge per (token, matching entity). Tokens are whitespace-separated linknames.
                for (const std::string& tok : splitTokens(value)) {
                    const auto it = linknames.find(tok);
                    if (it == linknames.end()) continue;
                    for (Id to : it->second) g.edges.push_back({e.id, to, key});
                }
                return;
            }
            // A target key names one targetname; an unresolved name yields no edge (checkLinks reports it).
            const auto it = g.byTargetname.find(value);
            if (it == g.byTargetname.end()) return;
            for (Id to : it->second) g.edges.push_back({e.id, to, key});
        });
    }
    return g;
}

std::vector<LinkIssue> checkLinks(const MapFile& map, const LinkGraph& graph) {
    std::vector<LinkIssue> out;

    std::map<Id, std::string> classOf;
    std::set<std::string> linkNames;
    for (const Entity& e : map.entities) {
        classOf[e.id] = e.classname();
        if (const std::string* n = e.value("script_linkname"); n && !n->empty()) linkNames.insert(*n);
    }

    // Begins that reach a node_negotiation_end through a target key. Built once from the edges.
    std::set<Id> pairedBegins;
    for (const LinkEdge& ed : graph.edges) {
        if (ed.key == "script_linkto") continue;
        if (classOf[ed.from] == "node_negotiation_begin" && classOf[ed.to] == "node_negotiation_end") pairedBegins.insert(ed.from);
    }

    for (const Entity& e : map.entities) {
        const std::string cls = e.classname();
        forEachLinkKey(e, [&](const std::string& key, const std::string& value) {
            if (key == "script_linkto") {
                for (const std::string& tok : splitTokens(value))
                    if (!linkNames.count(tok))
                        out.push_back({e.id, true, "script_linkto '" + tok + "' names no entity with that script_linkname"});
                return;
            }
            const auto it = graph.byTargetname.find(value);
            if (it == graph.byTargetname.end()) {
                out.push_back({e.id, true, key + " '" + value + "' names no entity with that targetname"});
                return;
            }
            // Triggers and negotiation begins fire at one target; several matches make the choice ambiguous.
            if (it->second.size() > 1 && (startsWith(cls, "trigger_") || cls == "node_negotiation_begin"))
                out.push_back({e.id, false, key + " '" + value + "' resolves to " + std::to_string(it->second.size()) + " entities"});
        });
        if (cls == "node_negotiation_begin" && !pairedBegins.count(e.id))
            out.push_back({e.id, true, "node_negotiation_begin has no matching node_negotiation_end"});
        // 6.23: the animation a bot plays. Empty: the converter leaves the traversal out; not one of the nine the stock
        // maps use: whether bots have it is not known.
        if (cls == "node_negotiation_begin") {
            const std::string* anim = e.value("animscript");
            if (!anim || anim->empty())
                out.push_back({e.id, true, "node_negotiation_begin has no animscript (the build leaves the traversal out)"});
            else if (!findTraversalAnim(*anim))
                out.push_back({e.id, false, "animscript '" + *anim + "' is not one of the stock traversal animations (jump_down_80, "
                                            "jump_up_40, ladder_up, ...): bots may not have it"});
        }
    }

    // Self-targets: a key whose value is the entity's own targetname.
    for (const LinkEdge& ed : graph.edges)
        if (ed.from == ed.to) out.push_back({ed.from, false, ed.key + " targets the entity's own targetname"});

    // Cycles over non-self edges. Depth-first walk in ascending Id order; a back edge closes a cycle whose head is
    // the back-edge target. Each distinct member set is reported once.
    std::map<Id, std::vector<Id>> adj;
    for (const LinkEdge& ed : graph.edges) {
        if (ed.from == ed.to) continue;
        auto& nbrs = adj[ed.from];
        if (std::find(nbrs.begin(), nbrs.end(), ed.to) == nbrs.end()) nbrs.push_back(ed.to);
    }
    std::map<Id, int> color;  // 0 unvisited, 1 on stack, 2 done
    std::vector<Id> stack;
    std::set<std::vector<Id>> seen;
    std::function<void(Id)> dfs = [&](Id u) {
        color[u] = 1;
        stack.push_back(u);
        const auto it = adj.find(u);
        if (it != adj.end()) {
            for (Id v : it->second) {
                if (color[v] == 0) {
                    dfs(v);
                } else if (color[v] == 1) {
                    const auto pos = std::find(stack.begin(), stack.end(), v);
                    std::vector<Id> members(pos, stack.end());
                    std::sort(members.begin(), members.end());
                    if (seen.insert(members).second)
                        out.push_back({v, false, "target chain loops back on itself through " + std::to_string(members.size()) + " entities"});
                }
            }
        }
        stack.pop_back();
        color[u] = 2;
    };
    for (const auto& [u, nbrs] : adj) {
        (void)nbrs;
        if (color[u] == 0) dfs(u);
    }
    return out;
}

std::vector<std::vector<Id>> targetChains(const LinkGraph& graph) {
    // Outgoing adjacency without self-links or duplicate neighbours; `hasIn` marks entities linked from another entity.
    std::map<Id, std::vector<Id>> out;
    std::set<Id> hasIn;
    for (const LinkEdge& ed : graph.edges) {
        if (ed.from == ed.to) continue;
        auto& nbrs = out[ed.from];
        if (std::find(nbrs.begin(), nbrs.end(), ed.to) == nbrs.end()) nbrs.push_back(ed.to);
        hasIn.insert(ed.to);
    }

    std::vector<std::vector<Id>> chains;
    std::vector<Id> path;
    std::set<Id> onPath;
    std::function<void(Id)> walk = [&](Id u) {
        path.push_back(u);
        onPath.insert(u);
        bool extended = false;
        const auto it = out.find(u);
        if (it != out.end()) {
            for (Id v : it->second) {
                if (onPath.count(v)) continue;  // stop before a loop repeats
                extended = true;
                walk(v);
            }
        }
        if (!extended) chains.push_back(path);  // leaf: maximal path
        onPath.erase(u);
        path.pop_back();
    };
    for (const auto& [u, nbrs] : out)
        if (!hasIn.count(u) && !nbrs.empty()) walk(u);
    return chains;
}

std::vector<Id> connectedTo(const LinkGraph& graph, Id id) {
    // Both directions, so a single adjacency list holds every edge twice.
    std::map<Id, std::vector<Id>> adj;
    for (const LinkEdge& ed : graph.edges) {
        if (ed.from == ed.to) continue;
        adj[ed.from].push_back(ed.to);
        adj[ed.to].push_back(ed.from);
    }
    std::set<Id> seen{id};
    std::vector<Id> work{id};
    while (!work.empty()) {
        const Id u = work.back();
        work.pop_back();
        const auto it = adj.find(u);
        if (it == adj.end()) continue;
        for (Id v : it->second)
            if (seen.insert(v).second) work.push_back(v);
    }
    seen.erase(id);
    return std::vector<Id>(seen.begin(), seen.end());
}

}  // namespace iw8r
