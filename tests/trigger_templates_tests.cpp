// Trigger templates (roadmap 6.17) and callout zones (6.20): kinds, one-step insertion, name validation, overlap check.
#include <algorithm>
#include <string>
#include <vector>

#include "document.h"
#include "map_text.h"
#include "test_harness.h"
#include "trigger_templates.h"

using namespace iw8r;

namespace {

const char* kWorldOnly = "iwmap 4\n{\n\"classname\" \"worldspawn\"\n}\n";

// One axis-aligned box brush with the trigger material (same shape as the auto_caulk test helper).
std::string boxText(int x0, int y0, int z0, int x1, int y1, int z1) {
    auto f = [&](const std::string& a, const std::string& b, const std::string& c) {
        return " ( " + a + " ) ( " + b + " ) ( " + c + " ) trigger 128 128 0 0 0 0\n";
    };
    const std::string X0 = std::to_string(x0), Y0 = std::to_string(y0), Z0 = std::to_string(z0), X1 = std::to_string(x1),
                      Y1 = std::to_string(y1), Z1 = std::to_string(z1);
    std::string t = "{\n";
    t += f(X1 + " " + Y1 + " " + Z0, X0 + " " + Y1 + " " + Z0, X0 + " " + Y0 + " " + Z0);
    t += f(X0 + " " + Y0 + " " + Z1, X0 + " " + Y1 + " " + Z1, X1 + " " + Y1 + " " + Z1);
    t += f(X0 + " " + Y0 + " " + Z0, X0 + " " + Y0 + " " + Z1, X1 + " " + Y0 + " " + Z1);
    t += f(X1 + " " + Y0 + " " + Z0, X1 + " " + Y0 + " " + Z1, X1 + " " + Y1 + " " + Z1);
    t += f(X1 + " " + Y1 + " " + Z0, X1 + " " + Y1 + " " + Z1, X0 + " " + Y1 + " " + Z1);
    t += f(X0 + " " + Y1 + " " + Z0, X0 + " " + Y1 + " " + Z1, X0 + " " + Y0 + " " + Z1);
    return t + "}\n";
}

// Parsed map of a text (the test fails if it does not parse).
MapFile parsed(const std::string& text) {
    Id next = 1;
    ParseResult pr = parseMap(text, next);
    CHECK(pr.map.has_value());
    return pr.map ? *pr.map : MapFile{};
}

// The entity with this classname (the first one), or nullptr.
const Entity* findClass(const MapFile& m, const std::string& cls) {
    for (const Entity& e : m.entities)
        if (e.classname() == cls) return &e;
    return nullptr;
}

size_t brushCount(const Entity& e) {
    size_t n = 0;
    for (const auto& it : e.items)
        if (const Primitive* p = it.prim())
            if (p->kind == PrimKind::Brush) ++n;
    return n;
}

AABB box(double x0, double y0, double z0, double x1, double y1, double z1) {
    return AABB{{x0, y0, z0}, {x1, y1, z1}};
}

}  // namespace

TEST(trigger_kinds_table) {
    const auto& kinds = triggerKinds();
    CHECK(!kinds.empty());
    // Only classes in the stock inventory or the defs; "once" does not exist there.
    const std::vector<std::string> allowed = {"trigger_multiple", "trigger_hurt", "trigger_use", "trigger_use_touch",
                                              "trigger_radius", "trigger_damage"};
    for (const TriggerKind& k : kinds) {
        CHECK(std::find(allowed.begin(), allowed.end(), k.classname) != allowed.end());
        CHECK(!k.title.empty());
        CHECK(!k.description.empty());
    }
    CHECK(findTriggerKind("once") == nullptr);
    const TriggerKind* hurt = findTriggerKind("hurt");
    CHECK(hurt != nullptr);
    if (hurt) {
        CHECK(hurt->classname == "trigger_hurt");
        CHECK(!hurt->point);
        CHECK(hurt->buildSupported);
        const bool dmg25 = std::any_of(hurt->defaultKeys.begin(), hurt->defaultKeys.end(),
                                       [](const auto& kv) { return kv.first == "dmg" && kv.second == "25"; });
        CHECK(dmg25);
    }
    const TriggerKind* radius = findTriggerKind("radius");
    CHECK(radius != nullptr);
    if (radius) CHECK(radius->point);
    // trigger_damage needs script_exploder, which no source gives: listed, but not offered.
    const TriggerKind* damage = findTriggerKind("damage");
    CHECK(damage != nullptr);
    if (damage) CHECK(!damage->buildSupported);
}

TEST(trigger_each_kind_one_undo_step) {
    for (const TriggerKind& k : triggerKinds()) {
        if (!k.buildSupported) continue;
        Document doc;
        std::string err;
        CHECK(doc.loadFromText(kWorldOnly, "", &err));
        const std::string before = doc.text();
        const size_t pos = doc.undoPosition();
        const Id id = insertTrigger(doc, k.id, box(0, 0, 0, 128, 96, 64), "", &err);
        CHECK(id != kNoId);
        CHECK(doc.undoPosition() == pos + 1);
        const MapFile m = parsed(doc.text());
        const Entity* e = findClass(m, k.classname);
        CHECK(e != nullptr);
        if (e) CHECK(e->classname() == k.classname);
        CHECK(doc.undo());
        CHECK(doc.text() == before);
    }
}

TEST(trigger_brush_kinds_have_one_box) {
    Document doc;
    std::string err;
    CHECK(doc.loadFromText(kWorldOnly, "", &err));
    const Id id = insertTrigger(doc, "hurt", box(0, 0, 0, 128, 128, 64), "hurt_pit", &err);
    CHECK(id != kNoId);
    const MapFile m = parsed(doc.text());
    const Entity* e = findClass(m, "trigger_hurt");
    CHECK(e != nullptr);
    if (!e) return;
    CHECK(brushCount(*e) == 1);
    const std::string* dmg = e->value("dmg");
    CHECK(dmg && *dmg == "25");
    const std::string* tn = e->value("targetname");
    CHECK(tn && *tn == "hurt_pit");
    const std::optional<Vec3> o = e->origin();
    CHECK(o.has_value());
    if (o) {
        CHECK(o->x == 64);
        CHECK(o->y == 64);
        CHECK(o->z == 32);
    }
}

TEST(trigger_radius_keys) {
    Document doc;
    std::string err;
    CHECK(doc.loadFromText(kWorldOnly, "", &err));
    // 128 x 96 footprint, 64 tall, starting at z 16: radius = half the wider side = 64, height = 64.
    const Id id = insertTrigger(doc, "radius", box(0, 0, 16, 128, 96, 80), "", &err);
    CHECK(id != kNoId);
    const MapFile m = parsed(doc.text());
    const Entity* e = findClass(m, "trigger_radius");
    CHECK(e != nullptr);
    if (!e) return;
    CHECK(brushCount(*e) == 0);  // point entity, no brush
    const std::string* r = e->value("radius");
    const std::string* h = e->value("height");
    CHECK(r && *r == "64");
    CHECK(h && *h == "64");
    const std::optional<Vec3> o = e->origin();
    CHECK(o.has_value());
    if (o) {  // bottom centre of the box
        CHECK(o->x == 64);
        CHECK(o->y == 48);
        CHECK(o->z == 16);
    }
}

TEST(trigger_rejects_bad_input) {
    Document doc;
    std::string err;
    CHECK(doc.loadFromText(kWorldOnly, "", &err));
    const std::string before = doc.text();
    const size_t pos = doc.undoPosition();

    err.clear();
    CHECK(insertTrigger(doc, "nosuchkind", box(0, 0, 0, 8, 8, 8), "", &err) == kNoId);
    CHECK(!err.empty());
    err.clear();
    CHECK(insertTrigger(doc, "damage", box(0, 0, 0, 8, 8, 8), "", &err) == kNoId);  // not kept by the build
    CHECK(!err.empty());
    err.clear();
    CHECK(insertTrigger(doc, "multiple", box(0, 0, 0, 0, 8, 8), "", &err) == kNoId);  // flat box
    CHECK(!err.empty());
    err.clear();
    CHECK(insertTrigger(doc, "multiple", box(0, 0, 0, 8, 8, 8), "bad\"name", &err) == kNoId);  // quote in targetname
    CHECK(!err.empty());

    CHECK(doc.text() == before);
    CHECK(doc.undoPosition() == pos);
}

TEST(callout_name_validation) {
    CHECK(isValidCalloutArea("containers"));
    CHECK(isValidCalloutArea("a"));
    CHECK(isValidCalloutArea("bcs_2"));
    CHECK(isValidCalloutArea(std::string(32, 'x')));
    CHECK(!isValidCalloutArea(""));
    CHECK(!isValidCalloutArea(std::string(33, 'x')));
    CHECK(!isValidCalloutArea("Containers"));
    CHECK(!isValidCalloutArea("has-dash"));
    CHECK(!isValidCalloutArea("has space"));
    CHECK(!isValidCalloutArea("dot.name"));

    Document doc;
    std::string err;
    CHECK(doc.loadFromText(kWorldOnly, "", &err));
    const std::string before = doc.text();
    CHECK(insertCalloutZone(doc, "Bad Name", box(0, 0, 0, 64, 64, 64), &err) == kNoId);
    CHECK(!err.empty());
    CHECK(doc.text() == before);
}

TEST(callout_insert_one_undo_step) {
    Document doc;
    std::string err;
    CHECK(doc.loadFromText(kWorldOnly, "", &err));
    const std::string before = doc.text();
    const size_t pos = doc.undoPosition();
    const Id id = insertCalloutZone(doc, "containers", box(0, 0, 0, 256, 256, 128), &err);
    CHECK(id != kNoId);
    CHECK(doc.undoPosition() == pos + 1);
    const MapFile m = parsed(doc.text());
    CHECK(findClass(m, "trigger_multiple_bcs_containers") != nullptr);
    const auto names = calloutNamesInMap(m);
    CHECK(names == std::vector<std::string>{"containers"});
    CHECK(doc.undo());
    CHECK(doc.text() == before);
}

TEST(callout_names_sorted_unique) {
    const std::string text = std::string(kWorldOnly) + "{\n\"classname\" \"trigger_multiple_bcs_shops\"\n" + boxText(0, 0, 0, 64, 64, 64) +
                             "}\n{\n\"classname\" \"trigger_multiple_bcs_center\"\n" + boxText(200, 0, 0, 264, 64, 64) +
                             "}\n{\n\"classname\" \"trigger_multiple_bcs_shops\"\n" + boxText(400, 0, 0, 464, 64, 64) + "}\n";
    const MapFile m = parsed(text);
    CHECK((calloutNamesInMap(m) == std::vector<std::string>{"center", "shops"}));
    // Duplicate names are fine: no warning for the pair of "shops" zones.
    CHECK(checkCallouts(m).empty());
}

TEST(callout_overlap_warning) {
    // A and B overlap (different names). C touches A's far face only (no overlap). D repeats A's name.
    const std::string text = std::string(kWorldOnly) +
                             "{\n\"classname\" \"trigger_multiple_bcs_alpha\"\n" + boxText(0, 0, 0, 128, 128, 128) + "}\n" +
                             "{\n\"classname\" \"trigger_multiple_bcs_beta\"\n" + boxText(64, 64, 0, 192, 192, 128) + "}\n" +
                             "{\n\"classname\" \"trigger_multiple_bcs_gamma\"\n" + boxText(128, 0, 0, 256, 64, 128) + "}\n" +
                             "{\n\"classname\" \"trigger_multiple_bcs_alpha\"\n" + boxText(32, 32, 32, 96, 96, 96) + "}\n";
    const MapFile m = parsed(text);
    const auto warnings = checkCallouts(m);
    size_t overlaps = 0;
    bool alphaBeta = false, alphaGamma = false;
    for (const auto& w : warnings) {
        if (w.find("overlaps") == std::string::npos) continue;
        ++overlaps;
        alphaBeta |= w.find("'alpha'") != std::string::npos && w.find("'beta'") != std::string::npos;
        alphaGamma |= w.find("'gamma'") != std::string::npos;
    }
    CHECK(overlaps == 2);  // alpha-beta and beta-alpha(D)
    CHECK(alphaBeta);
    CHECK(!alphaGamma);  // touching faces are not an overlap
}

TEST(callout_invalid_name_and_flat_zone_warning) {
    // A name with a dash is written in the classname by a mapper; a flat zone has no volume.
    const std::string text = std::string(kWorldOnly) + "{\n\"classname\" \"trigger_multiple_bcs_bad-name\"\n" + boxText(0, 0, 0, 64, 64, 64) +
                             "}\n{\n\"classname\" \"trigger_multiple_bcs_flat\"\n" + boxText(300, 0, 0, 364, 64, 0) + "}\n";
    const MapFile m = parsed(text);
    const auto warnings = checkCallouts(m);
    bool invalid = false, zero = false;
    for (const auto& w : warnings) {
        invalid |= w.find("invalid") != std::string::npos && w.find("bad-name") != std::string::npos;
        zero |= w.find("zero-volume") != std::string::npos || w.find("no brush") != std::string::npos;
    }
    CHECK(invalid);
    CHECK(zero);
}
