// Scripted object presets (6.5): destructible and scripted props, one undo step each.
#include <set>
#include <string>

#include "document.h"
#include "scripted_objects.h"
#include "test_harness.h"

using namespace iw8r;

namespace {
const char* kMap =
    "iwmap 4\n{\n\"classname\" \"worldspawn\"\n}\n"
    "{\n\"classname\" \"misc_model\"\n\"model\" \"com_junktire\"\n\"origin\" \"0 0 0\"\n}\n"
    "{\n\"classname\" \"misc_model\"\n\"model\" \"tm/com_pallet_2\"\n\"origin\" \"64 0 0\"\n}\n"
    "{\n\"classname\" \"script_origin\"\n\"origin\" \"0 0 0\"\n}\n";

std::set<Id> modelIds(const Document& doc) {
    std::set<Id> ids;
    for (const auto& e : doc.map().entities)
        if (e.value("model")) ids.insert(e.id);
    return ids;
}
}  // namespace

TEST(scripted_objects_destructible) {
    Document doc;
    std::string err;
    CHECK(doc.loadFromText(kMap, "", &err));
    const std::string before = doc.text();
    const size_t undo = doc.undoPosition();
    CHECK(makeDestructible(doc, modelIds(doc)) == 2);
    CHECK(doc.undoPosition() == undo + 1);
    int destructable = 0;
    for (const auto& e : doc.map().entities)
        if (e.value("model") && e.classname() == "script_model" && e.value("targetname") && *e.value("targetname") == "destructable" &&
            e.value("footprint_mask_group"))
            ++destructable;
    CHECK(destructable == 2);
    doc.undo();
    CHECK(doc.text() == before);
}

TEST(scripted_objects_scripted) {
    Document doc;
    std::string err;
    CHECK(doc.loadFromText(kMap, "", &err));
    std::string names;
    CHECK(makeScripted(doc, modelIds(doc), &names) == 2);
    CHECK(names == "scripted_prop_1, scripted_prop_2");
    int targeting = 0;
    for (const auto& e : doc.map().entities)
        if (e.classname() == "script_origin" && e.value("target")) ++targeting;
    CHECK(targeting == 2);
    // Running it again keeps the names and adds no second script_origin.
    CHECK(makeScripted(doc, modelIds(doc), &names) == 2);
    targeting = 0;
    for (const auto& e : doc.map().entities)
        if (e.classname() == "script_origin" && e.value("target")) ++targeting;
    CHECK(targeting == 2);
}
