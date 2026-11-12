// Batch key tools (8.7): model replace / key find-replace, one undo step.
#include <string>

#include "document.h"
#include "key_replace.h"
#include "test_harness.h"

using namespace iw8r;

namespace {
const char* kKeyMap =
    "iwmap 4\n{\n\"classname\" \"worldspawn\"\n}\n"
    "{\n\"classname\" \"misc_model\"\n\"model\" \"tm/com_pallet_2\"\n\"origin\" \"0 0 0\"\n}\n"
    "{\n\"classname\" \"misc_model\"\n\"model\" \"tm/com_pallet_2\"\n\"origin\" \"64 0 0\"\n}\n"
    "{\n\"classname\" \"misc_model\"\n\"model\" \"com_junktire\"\n\"origin\" \"128 0 0\"\n}\n";
}  // namespace

TEST(key_replace_models) {
    Document doc;
    std::string err;
    CHECK(doc.loadFromText(kKeyMap, "", &err));
    const auto usage = keyValueUsage(doc.map(), "model");
    CHECK(usage.size() == 2 && usage[0].first == "tm/com_pallet_2" && usage[0].second == 2);
    CHECK(entitiesWithValue(doc.map(), "model", "pallet", true).size() == 2);
    CHECK(entitiesWithValue(doc.map(), "model", "pallet", false).empty());
    const std::string before = doc.text();
    const size_t undo = doc.undoPosition();
    CHECK(replaceKeyValues(doc, "model", "tm/com_pallet_2", "tm/cinderblock_pallet_01", false, false) == 2);
    CHECK(doc.undoPosition() == undo + 1);
    CHECK(keyValueUsage(doc.map(), "model")[0].first == "tm/cinderblock_pallet_01");
    CHECK(replaceKeyValues(doc, "model", "junk", "big_junk", true, false) == 1);
    CHECK(entitiesWithValue(doc.map(), "model", "com_big_junktire", false).size() == 1);
    doc.undo();
    doc.undo();
    CHECK(doc.text() == before);
    CHECK(replaceKeyValues(doc, "model", "tm/com_pallet_2", "x", false, true) == 0);  // nothing selected
}
