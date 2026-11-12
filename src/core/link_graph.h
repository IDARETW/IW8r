// IW8 Radiant Suite - link graph over entity target/targetname and script_linkto/script_linkname.
//
// Link keys (read through Entity::value, so canonical-id spellings resolve too):
//   - target, target2 .. target8: the value is one targetname. Each entity whose targetname equals it is linked.
//   - script_linkto: the value is a space-separated list of linknames. Each linkname is matched against the
//     script_linkname of every entity (exact, case-sensitive).
// Edges point from the entity holding the key (`from`) to the entity it names (`to`). Targets that name nothing
// produce no edge; checkLinks reports them. Empty values are ignored. A key naming several entities gives one
// edge per match, in entity order.
//
// Rules (checkLinks; errors and warnings mirror gameplay_checks):
//   - error: a target key naming no entity (targetname) and a script_linkto token naming no script_linkname.
//   - error: node_negotiation_begin with no target edge to a node_negotiation_end.
//   - warning: trigger_* or node_negotiation_begin whose target key resolves to more than one entity.
//   - warning: an entity whose target key names its own targetname (self-target).
//   - warning: a cycle in the link graph (self-targets excluded). One issue per distinct cycle, on the entity where
//     the depth-first walk closed it.
//   - Targetnames that nothing targets are not reported (too noisy: most are read by the engine by name).
#pragma once

#include <map>
#include <string>
#include <vector>

#include "map_text.h"

namespace iw8r {

struct LinkEdge {
    Id from = kNoId;
    Id to = kNoId;
    std::string key;  // "target", "target2".."target8" or "script_linkto"
};

struct LinkGraph {
    std::vector<LinkEdge> edges;                        // in entity order, then key order, then match order
    std::map<std::string, std::vector<Id>> byTargetname;  // targetname -> entities carrying it (map order)
};

LinkGraph buildLinkGraph(const MapFile& map);

struct LinkIssue {
    Id entity = kNoId;
    bool error = false;  // false = warning
    std::string message;
};

std::vector<LinkIssue> checkLinks(const MapFile& map, const LinkGraph& graph);

// Maximal paths for drawing struct/heli paths. Each path starts at an entity with no incoming edge from another
// entity and at least one outgoing edge. Branches give one path per leaf. Paths stop before revisiting an entity,
// so a tail feeding into a loop is included but the loop is not repeated. Pure loops have no start and are
// omitted. Order: start entities by ascending Id, branches in edge order. Enumeration is exponential in the number
// of branches, which map link data does not reach in practice.
std::vector<std::vector<Id>> targetChains(const LinkGraph& graph);

// Every entity weakly connected to `id` (links followed in both directions, transitively). Excludes `id` itself.
// Ascending Id order. Empty when `id` has no links.
std::vector<Id> connectedTo(const LinkGraph& graph, Id id);

}  // namespace iw8r
