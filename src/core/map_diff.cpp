// Entity-level change report between two maps. Matching rule, record semantics and patch format: see map_diff.h.
#include "map_diff.h"

#include <algorithm>
#include <cstdio>
#include <map>
#include <optional>
#include <set>

#include "document.h"
#include "json_lite.h"

namespace iw8r {
namespace {

using Pairs = std::vector<std::pair<std::string, std::string>>;
constexpr const char* kPatchFormat = "iw8r-map-patch";

// Whitespace runs collapse to one space, so re-wrapped or re-spaced brush text compares equal.
std::string normalizeWs(const std::string& s) {
    std::string out;
    bool pendingSpace = false;
    for (char c : s) {
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            pendingSpace = !out.empty();
            continue;
        }
        if (pendingSpace) out += ' ';
        pendingSpace = false;
        out += c;
    }
    return out;
}

// What matching and reporting need to know about one entity.
struct EntInfo {
    std::string classname, targetname, model;
    std::optional<Vec3> origin;
    Pairs keys;                        // (name, value) in file order
    std::vector<std::string> brushes;  // normalized brush texts, sorted; empty for point entities
};

Pairs keysOf(const Entity& e) {
    Pairs out;
    for (const auto& it : e.items)
        if (const KeyValueLine* kv = it.kv()) out.emplace_back(kv->name(), kv->value);
    return out;
}

std::vector<std::string> sortedBrushes(const Entity& e) {
    std::vector<std::string> out;
    for (const auto& it : e.items)
        if (it.isPrimitive()) out.push_back(normalizeWs(it.text()));
    std::sort(out.begin(), out.end());  // sorted so the multiset compares with ==
    return out;
}

EntInfo infoOf(const Entity& e) {
    EntInfo x;
    x.classname = e.classname();
    if (const std::string* v = e.value("targetname")) x.targetname = *v;
    if (const std::string* v = e.value("model")) x.model = *v;
    x.origin = e.origin();
    x.keys = keysOf(e);
    x.brushes = sortedBrushes(e);
    return x;
}

std::vector<EntInfo> infosOf(const MapFile& m) {
    std::vector<EntInfo> out;
    out.reserve(m.entities.size());
    for (const auto& e : m.entities) out.push_back(infoOf(e));
    return out;
}

// Pairing per the rule in map_diff.h. Result: for each entity of A, the index of its partner in B, or -1.
std::vector<int> matchEntities(const std::vector<EntInfo>& A, const std::vector<EntInfo>& B) {
    std::vector<int> bOf(A.size(), -1);
    std::vector<char> bUsed(B.size(), 0);
    auto link = [&](size_t i, size_t j) {
        bOf[i] = int(j);
        bUsed[j] = 1;
    };

    // 0. worldspawn with worldspawn, first with first.
    for (size_t i = 0; i < A.size(); ++i) {
        if (bOf[i] >= 0 || A[i].classname != "worldspawn") continue;
        for (size_t j = 0; j < B.size(); ++j)
            if (!bUsed[j] && B[j].classname == "worldspawn") {
                link(i, j);
                break;
            }
    }

    // 1. classname + targetname, only when that pair is unique on both sides (a duplicated name is ambiguous).
    std::map<std::string, std::vector<size_t>> tnA, tnB;
    auto tnKey = [](const EntInfo& x) { return x.classname + '\x1f' + x.targetname; };
    for (size_t i = 0; i < A.size(); ++i)
        if (!A[i].targetname.empty()) tnA[tnKey(A[i])].push_back(i);
    for (size_t j = 0; j < B.size(); ++j)
        if (!B[j].targetname.empty()) tnB[tnKey(B[j])].push_back(j);
    for (const auto& [key, ia] : tnA) {
        auto it = tnB.find(key);
        if (ia.size() != 1 || it == tnB.end() || it->second.size() != 1) continue;
        const size_t i = ia[0], j = it->second[0];
        if (bOf[i] < 0 && !bUsed[j]) link(i, j);
    }

    // 2. classname + origin equal exactly.
    for (size_t i = 0; i < A.size(); ++i) {
        if (bOf[i] >= 0 || !A[i].origin) continue;
        for (size_t j = 0; j < B.size(); ++j)
            if (!bUsed[j] && B[j].origin && B[j].classname == A[i].classname && *B[j].origin == *A[i].origin) {
                link(i, j);
                break;
            }
    }

    // 3. classname + origin within 1 unit, nearest candidate wins. Covers float noise from a re-save, not real moves.
    for (size_t i = 0; i < A.size(); ++i) {
        if (bOf[i] >= 0 || !A[i].origin) continue;
        int best = -1;
        double bestD = 0;
        for (size_t j = 0; j < B.size(); ++j) {
            if (bUsed[j] || !B[j].origin || B[j].classname != A[i].classname) continue;
            const double d = length(*A[i].origin - *B[j].origin);
            if (d <= 1.0 && (best < 0 || d < bestD)) {
                best = int(j);
                bestD = d;
            }
        }
        if (best >= 0) link(i, size_t(best));
    }

    // 4. brush entities with the same classname and the same multiset of brush texts.
    for (size_t i = 0; i < A.size(); ++i) {
        if (bOf[i] >= 0 || A[i].brushes.empty()) continue;
        for (size_t j = 0; j < B.size(); ++j)
            if (!bUsed[j] && B[j].classname == A[i].classname && B[j].brushes == A[i].brushes) {
                link(i, j);
                break;
            }
    }
    return bOf;
}

// Brush identity inside a patch. worldspawn is unique per map and identified by its keys alone.
std::vector<std::string> identityBrushes(const EntInfo& x) {
    return x.classname == "worldspawn" ? std::vector<std::string>{} : x.brushes;
}

const std::string* findKey(const Pairs& keys, const std::string& name) {
    for (const auto& [k, v] : keys)
        if (k == name) return &v;
    return nullptr;
}

struct KeyDelta {
    std::string name;
    std::optional<std::string> from, to;  // nullopt = absent on that side
};

// Keys that differ between two paired entities. classname never differs (matching requires it). The origin key is
// left to Moved when both sides parse, so "0 0 0" vs "0.0 0 0" is not a change at all.
std::vector<KeyDelta> keyDeltas(const EntInfo& a, const EntInfo& b) {
    std::vector<std::string> names;
    std::set<std::string> seen;
    for (const auto& kv : a.keys)
        if (seen.insert(kv.first).second) names.push_back(kv.first);
    for (const auto& kv : b.keys)
        if (seen.insert(kv.first).second) names.push_back(kv.first);
    const bool bothOrigin = a.origin && b.origin;
    std::vector<KeyDelta> out;
    for (const auto& n : names) {
        if (n == "classname") continue;
        if (n == "origin" && bothOrigin) continue;
        const std::string* va = findKey(a.keys, n);
        const std::string* vb = findKey(b.keys, n);
        if (va && vb && *va == *vb) continue;
        KeyDelta d;
        d.name = n;
        if (va) d.from = *va;
        if (vb) d.to = *vb;
        out.push_back(std::move(d));
    }
    return out;
}

// Net count per normalized brush text: positive = more copies in A (removed), negative = more in B (added).
std::map<std::string, int> brushBalance(const EntInfo& a, const EntInfo& b) {
    std::map<std::string, int> bal;
    for (const auto& t : a.brushes) ++bal[t];
    for (const auto& t : b.brushes) --bal[t];
    return bal;
}

std::string labelOf(const EntInfo& x, size_t index) {
    if (!x.targetname.empty()) return x.targetname;
    if (!x.model.empty()) return x.model;
    return "entity " + std::to_string(index);
}

std::string keyChangeText(const KeyDelta& d) {
    return d.name + ": " + d.from.value_or("(absent)") + " -> " + d.to.value_or("(absent)");
}

std::string vecText(const Vec3& v) { return formatNumber(v.x) + " " + formatNumber(v.y) + " " + formatNumber(v.z); }

// ---- JSON output (json_lite is read-only, so the writer is here) ----

std::string jstr(const std::string& s) {
    std::string o = "\"";
    for (unsigned char c : s) {
        switch (c) {
        case '"': o += "\\\""; break;
        case '\\': o += "\\\\"; break;
        case '\n': o += "\\n"; break;
        case '\r': o += "\\r"; break;
        case '\t': o += "\\t"; break;
        default:
            if (c < 0x20) {
                char buf[8];
                std::snprintf(buf, sizeof(buf), "\\u%04x", unsigned(c));
                o += buf;
            } else {
                o += char(c);
            }
        }
    }
    return o + "\"";
}

std::string pairsJson(const Pairs& p) {
    std::string o = "[";
    for (size_t i = 0; i < p.size(); ++i) {
        if (i) o += ",";
        o += "[" + jstr(p[i].first) + "," + jstr(p[i].second) + "]";
    }
    return o + "]";
}

std::string listJson(const std::vector<std::string>& v) {
    std::string o = "[";
    for (size_t i = 0; i < v.size(); ++i) {
        if (i) o += ",";
        o += jstr(v[i]);
    }
    return o + "]";
}

// One object per line inside the array, so a patch can be read and diffed by eye.
std::string arrayJson(const std::vector<std::string>& items) {
    if (items.empty()) return "[]";
    std::string o = "[";
    for (size_t i = 0; i < items.size(); ++i) o += (i ? ",\n    " : "\n    ") + items[i];
    return o + "\n  ]";
}

// ---- JSON input ----

bool readPairs(const json::Value* v, Pairs& out) {
    out.clear();
    if (!v) return true;
    if (v->type != json::Value::Type::Array) return false;
    for (const auto& item : v->arr) {
        if (item.type != json::Value::Type::Array || item.arr.size() != 2) return false;
        const std::string* k = item.arr[0].string();
        const std::string* val = item.arr[1].string();
        if (!k || !val) return false;
        out.emplace_back(*k, *val);
    }
    return true;
}

bool readStrings(const json::Value* v, std::vector<std::string>& out) {
    out.clear();
    if (!v) return true;
    if (v->type != json::Value::Type::Array) return false;
    for (const auto& item : v->arr) {
        const std::string* s = item.string();
        if (!s) return false;
        out.push_back(*s);
    }
    return true;
}

// Members of an array member of `obj`: missing = empty, wrong type = false.
bool itemsOf(const json::Value& obj, const char* name, std::vector<const json::Value*>& out) {
    out.clear();
    const json::Value* list = obj.get(name);
    if (!list) return true;
    if (list->type != json::Value::Type::Array) return false;
    for (const auto& item : list->arr) out.push_back(&item);
    return true;
}

// Parses one brush or patch block (as Primitive::text writes it) by wrapping it in a one-entity map.
std::optional<Primitive> parsePrimitive(const std::string& raw) {
    const std::string wrapped = "iwmap 4\n{\n\"classname\" \"brush\"\n" + raw + "}\n";
    Id next = 1;
    ParseResult r = parseMap(wrapped, next);
    if (!r.map || r.map->entities.size() != 1) return std::nullopt;
    for (const auto& it : r.map->entities[0].items)
        if (const Primitive* p = it.prim()) return *p;
    return std::nullopt;
}

std::string describeKeys(const Pairs& keys) {
    std::string cls, tn;
    for (const auto& [k, v] : keys) {
        if (k == "classname") cls = v;
        else if (k == "targetname") tn = v;
    }
    return tn.empty() ? cls : cls + " " + tn;
}

// Finds the entity of `doc` that a patch entry describes: same full key list (file order), and for non-worldspawn
// entities the same sorted normalized brushes. Entities already claimed by an earlier entry are skipped, so two
// identical entities are resolved to two different targets.
std::optional<Id> resolveEntity(const Document& doc, const Pairs& keys, const std::vector<std::string>& brushes,
                                std::set<Id>& claimed) {
    for (const Entity& e : doc.map().entities) {
        if (claimed.count(e.id)) continue;
        if (keysOf(e) != keys) continue;
        if (e.classname() != "worldspawn" && sortedBrushes(e) != brushes) continue;
        claimed.insert(e.id);
        return e.id;
    }
    return std::nullopt;
}

}  // namespace

std::vector<EntityChange> diffMapEntities(const MapFile& a, const MapFile& b) {
    const std::vector<EntInfo> A = infosOf(a), B = infosOf(b);
    const std::vector<int> bOf = matchEntities(A, B);
    std::vector<char> bUsed(B.size(), 0);
    for (int j : bOf)
        if (j >= 0) bUsed[size_t(j)] = 1;

    std::vector<EntityChange> out;
    for (size_t i = 0; i < A.size(); ++i) {
        if (bOf[i] < 0) {
            EntityChange c;
            c.kind = EntityChange::Removed;
            c.classname = A[i].classname;
            c.label = labelOf(A[i], i);
            if (A[i].origin) c.from = *A[i].origin;
            out.push_back(std::move(c));
            continue;
        }
        const size_t j = size_t(bOf[i]);
        const EntInfo& x = A[i];
        const EntInfo& y = B[j];
        const std::string label = labelOf(y, j);

        if (x.origin && y.origin && *x.origin != *y.origin) {
            EntityChange c;
            c.kind = EntityChange::Moved;
            c.classname = y.classname;
            c.label = label;
            c.from = *x.origin;
            c.to = *y.origin;
            out.push_back(std::move(c));
        }
        const auto deltas = keyDeltas(x, y);
        if (!deltas.empty()) {
            EntityChange c;
            c.kind = EntityChange::KeysChanged;
            c.classname = y.classname;
            c.label = label;
            for (const auto& d : deltas) c.keyChanges.push_back(keyChangeText(d));
            out.push_back(std::move(c));
        }
        int added = 0, removed = 0;
        for (const auto& [text, n] : brushBalance(x, y)) {
            (void)text;
            if (n > 0) removed += n;
            else added -= n;
        }
        if (added || removed) {
            EntityChange c;
            c.kind = EntityChange::BrushesChanged;
            c.classname = y.classname;
            c.label = label;
            c.brushesAdded = added;
            c.brushesRemoved = removed;
            out.push_back(std::move(c));
        }
    }
    for (size_t j = 0; j < B.size(); ++j) {
        if (bUsed[j]) continue;
        EntityChange c;
        c.kind = EntityChange::Added;
        c.classname = B[j].classname;
        c.label = labelOf(B[j], j);
        if (B[j].origin) c.to = *B[j].origin;
        out.push_back(std::move(c));
    }
    return out;
}

std::string formatMapDiff(const std::vector<EntityChange>& changes) {
    std::vector<EntityChange> sorted = changes;
    std::stable_sort(sorted.begin(), sorted.end(), [](const EntityChange& p, const EntityChange& q) {
        if (p.kind != q.kind) return p.kind < q.kind;
        if (p.classname != q.classname) return p.classname < q.classname;
        return p.label < q.label;
    });
    std::string out;
    for (const auto& c : sorted) {
        const std::string who = c.classname + " " + c.label;
        switch (c.kind) {
        case EntityChange::Added: out += "Added " + who; break;
        case EntityChange::Removed: out += "Removed " + who; break;
        case EntityChange::Moved:
            out += "Moved " + who + " from " + vecText(c.from) + " to " + vecText(c.to);
            break;
        case EntityChange::KeysChanged:
            out += "Keys " + who + ": ";
            for (size_t i = 0; i < c.keyChanges.size(); ++i) out += (i ? "; " : "") + c.keyChanges[i];
            break;
        case EntityChange::BrushesChanged:
            out += "Brushes " + who + ":";
            if (c.brushesAdded) out += " +" + std::to_string(c.brushesAdded);
            if (c.brushesRemoved) out += " -" + std::to_string(c.brushesRemoved);
            break;
        }
        out += '\n';
    }
    return out;
}

std::string mapPatchJson(const MapFile& a, const MapFile& b) {
    const std::vector<EntInfo> A = infosOf(a), B = infosOf(b);
    const std::vector<int> bOf = matchEntities(A, B);
    std::vector<char> bUsed(B.size(), 0);
    for (int j : bOf)
        if (j >= 0) bUsed[size_t(j)] = 1;

    std::vector<std::string> changed, removed, added;
    for (size_t i = 0; i < A.size(); ++i) {
        const EntInfo& x = A[i];
        if (bOf[i] < 0) {
            removed.push_back("{\"keys\":" + pairsJson(x.keys) + ",\"brushes\":" + listJson(identityBrushes(x)) + "}");
            continue;
        }
        const size_t j = size_t(bOf[i]);
        const EntInfo& y = B[j];
        const bool moved = x.origin && y.origin && *x.origin != *y.origin;
        const auto deltas = keyDeltas(x, y);

        // Brush edits: additions carry raw text (it must be re-parsed), removals carry normalized identity.
        auto bal = brushBalance(x, y);
        std::vector<std::string> brushAdd, brushRemove;
        for (const auto& it : b.entities[j].items) {
            if (!it.isPrimitive()) continue;
            auto f = bal.find(normalizeWs(it.text()));
            if (f != bal.end() && f->second < 0) {
                brushAdd.push_back(it.text());
                ++f->second;
            }
        }
        for (const auto& [text, n] : bal)
            for (int k = 0; k < n; ++k) brushRemove.push_back(text);

        if (!moved && deltas.empty() && brushAdd.empty() && brushRemove.empty()) continue;

        std::string s = "{\"match\":{\"keys\":" + pairsJson(x.keys) + ",\"brushes\":" + listJson(identityBrushes(x)) + "}";
        if (moved) s += ",\"origin\":" + jstr(*findKey(y.keys, "origin"));
        Pairs set;
        std::vector<std::string> remove;
        for (const auto& d : deltas) {
            if (d.to) set.emplace_back(d.name, *d.to);
            else remove.push_back(d.name);
        }
        s += ",\"set\":" + pairsJson(set) + ",\"remove\":" + listJson(remove);
        s += ",\"brushesAdd\":" + listJson(brushAdd) + ",\"brushesRemove\":" + listJson(brushRemove) + "}";
        changed.push_back(std::move(s));
    }
    for (size_t j = 0; j < B.size(); ++j) {
        if (bUsed[j]) continue;
        std::vector<std::string> raw;
        for (const auto& it : b.entities[j].items)
            if (it.isPrimitive()) raw.push_back(it.text());
        added.push_back("{\"keys\":" + pairsJson(B[j].keys) + ",\"brushes\":" + listJson(raw) + "}");
    }

    return std::string("{\n  \"format\": \"") + kPatchFormat + "\",\n  \"version\": 1,\n  \"changed\": " + arrayJson(changed) +
           ",\n  \"removed\": " + arrayJson(removed) + ",\n  \"added\": " + arrayJson(added) + "\n}\n";
}

bool applyMapPatch(Document& doc, const std::string& json, std::string* err) {
    auto fail = [&](const std::string& msg) {
        if (err) *err = msg;
        return false;
    };
    std::string perr;
    const auto root = json::parse(json, &perr);
    if (!root) return fail("patch JSON: " + perr);
    const json::Value* fmt = root->get("format");
    if (!fmt || !fmt->string() || *fmt->string() != kPatchFormat) return fail("not an iw8r map patch");

    // Pass 1: resolve every target and parse every brush. Nothing is modified until all of this has succeeded, so a
    // bad entry leaves the document untouched.
    struct ChangePlan {
        Id entity = kNoId;
        std::optional<std::string> origin;
        Pairs set;
        std::vector<std::string> remove;
        std::vector<Document::PrimEdit> brushEdits;
    };
    struct AddPlan {
        Pairs keys;
        std::vector<Primitive> brushes;
    };
    std::set<Id> claimed;
    std::vector<ChangePlan> changes;
    std::vector<Id> removals;
    std::vector<AddPlan> adds;
    std::vector<const json::Value*> items;

    if (!itemsOf(*root, "changed", items)) return fail("patch: \"changed\" is not an array");
    for (const json::Value* c : items) {
        ChangePlan p;
        Pairs keys;
        std::vector<std::string> brushes, brushAdd, brushRemove;
        const json::Value* m = c->get("match");
        if (!m || !readPairs(m->get("keys"), keys) || !readStrings(m->get("brushes"), brushes))
            return fail("patch: changed entry without a valid match");
        const auto id = resolveEntity(doc, keys, brushes, claimed);
        if (!id) return fail("patch: entity to change not found: " + describeKeys(keys));
        p.entity = *id;
        if (const json::Value* o = c->get("origin")) {
            if (!o->string()) return fail("patch: origin is not a string");
            p.origin = *o->string();
        }
        if (!readPairs(c->get("set"), p.set) || !readStrings(c->get("remove"), p.remove) ||
            !readStrings(c->get("brushesAdd"), brushAdd) || !readStrings(c->get("brushesRemove"), brushRemove))
            return fail("patch: bad set/remove/brush list for " + describeKeys(keys));

        const Entity* ent = doc.findEntity(*id);
        std::set<Id> usedItems;
        for (const auto& t : brushRemove) {
            Id hit = kNoId;
            for (const auto& it : ent->items)
                if (it.isPrimitive() && !usedItems.count(it.id) && normalizeWs(it.text()) == t) {
                    hit = it.id;
                    break;
                }
            if (hit == kNoId) return fail("patch: brush to remove not found in " + describeKeys(keys));
            usedItems.insert(hit);
            p.brushEdits.push_back({*id, hit, std::nullopt});
        }
        for (const auto& raw : brushAdd) {
            auto prim = parsePrimitive(raw);
            if (!prim) return fail("patch: brush text does not parse");
            p.brushEdits.push_back({*id, kNoId, std::move(*prim)});
        }
        changes.push_back(std::move(p));
    }

    if (!itemsOf(*root, "removed", items)) return fail("patch: \"removed\" is not an array");
    for (const json::Value* r : items) {
        Pairs keys;
        std::vector<std::string> brushes;
        if (!readPairs(r->get("keys"), keys) || !readStrings(r->get("brushes"), brushes))
            return fail("patch: removed entry without a valid match");
        const auto id = resolveEntity(doc, keys, brushes, claimed);
        if (!id) return fail("patch: entity to remove not found: " + describeKeys(keys));
        removals.push_back(*id);
    }

    if (!itemsOf(*root, "added", items)) return fail("patch: \"added\" is not an array");
    for (const json::Value* r : items) {
        AddPlan a;
        std::vector<std::string> raw;
        if (!readPairs(r->get("keys"), a.keys) || !readStrings(r->get("brushes"), raw))
            return fail("patch: added entry is malformed");
        for (const auto& t : raw) {
            auto prim = parsePrimitive(t);
            if (!prim) return fail("patch: brush text does not parse");
            a.brushes.push_back(std::move(*prim));
        }
        adds.push_back(std::move(a));
    }

    // Pass 2: apply inside one group, so the whole patch is one undo step.
    const std::set<SelRef> savedSel = doc.selection();
    doc.beginGroup("Apply Map Patch");
    for (const auto& p : changes) {
        if (p.origin) doc.setKeyValue(p.entity, "origin", *p.origin);
        for (const auto& [k, v] : p.set) doc.setKeyValue(p.entity, k, v);
        for (const auto& k : p.remove) doc.removeKey(p.entity, k);
        if (!p.brushEdits.empty()) doc.editPrimitives(p.brushEdits, "Patch brushes", false);
    }
    if (!removals.empty()) {
        // Entity delete goes through the selection: a whole-entity ref is removed by deleteSelection.
        doc.deselectAll();
        for (Id id : removals) doc.select(SelRef{id, kNoId}, true);
        doc.deleteSelection();
    }
    for (const auto& a : adds) doc.addEntity(a.keys, a.brushes, "Patch: add entity");

    // The selection was used as a tool above; put back whatever still exists.
    doc.deselectAll();
    for (const SelRef& r : savedSel)
        if (doc.findEntity(r.entity) && (r.item == kNoId || doc.findItem(r.entity, r.item))) doc.select(r, true);
    doc.endGroup();
    return true;
}

}  // namespace iw8r
