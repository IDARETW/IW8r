#include "clipboard_ops.h"
#include "test_harness.h"

#include <string>

using namespace iw8r;

namespace {

const char* kBox =
    "{\n"
    " ( 64 64 64 ) ( -64 64 64 ) ( -64 -64 64 ) caulk 128 128 0 0 0 0 lightmap_gray 16384 16384 0 0 0 0\n"
    " ( -64 -64 0 ) ( -64 64 0 ) ( 64 64 0 ) caulk 128 128 0 0 0 0 lightmap_gray 16384 16384 0 0 0 0\n"
    " ( -64 -64 0 ) ( 64 -64 0 ) ( 64 -64 64 ) caulk 128 128 0 0 0 0 lightmap_gray 16384 16384 0 0 0 0\n"
    " ( 64 64 0 ) ( -64 64 0 ) ( -64 64 64 ) caulk 128 128 0 0 0 0 lightmap_gray 16384 16384 0 0 0 0\n"
    " ( 64 -64 0 ) ( 64 64 0 ) ( 64 64 64 ) caulk 128 128 0 0 0 0 lightmap_gray 16384 16384 0 0 0 0\n"
    " ( -64 64 0 ) ( -64 -64 0 ) ( -64 -64 64 ) caulk 128 128 0 0 0 0 lightmap_gray 16384 16384 0 0 0 0\n"
    "}\n";

std::string mapWith() {
    return std::string("iwmap 4\n// entity 0\n{\n\"classname\" \"worldspawn\"\n") + kBox + kBox +
           "}\n// entity 1\n{\n\"classname\" \"trigger_multiple\"\n\"targetname\" \"t1\"\n" + kBox +
           "}\n// entity 2\n{\n\"classname\" \"light\"\n\"origin\" \"0 0 96\"\n}\n";
}

}  // namespace

TEST(clipboard_copy_paste_between_documents) {
    Document a, b;
    std::string err;
    CHECK(a.loadFromText(mapWith(), "a.map", &err));
    CHECK(b.loadFromText("iwmap 4\n// entity 0\n{\n\"classname\" \"worldspawn\"\n}\n", "b.map", &err));
    // Select one worldspawn brush and the whole trigger (its brush) and the light.
    const auto& es = a.map().entities;
    for (const auto& it : es[0].items)
        if (it.isPrimitive()) {
            a.select({es[0].id, it.id}, true);
            break;
        }
    for (const auto& it : es[1].items)
        if (it.isPrimitive()) a.select({es[1].id, it.id}, true);
    a.select({es[2].id, kNoId}, true);
    size_t items = 0;
    const std::string text = selectionToClipboardText(a, &items);
    CHECK(items == 3);
    CHECK(text.find("trigger_multiple") != std::string::npos && text.find("\"light\"") != std::string::npos);
    CHECK(pasteClipboardText(b, text, &err) == 3);
    const auto& bs = b.map().entities;
    CHECK(bs.size() == 3);
    size_t worldPrims = 0;
    for (const auto& it : bs[0].items) worldPrims += it.isPrimitive();
    CHECK(worldPrims == 1);
    bool trigger = false, light = false;
    for (const auto& e : bs) {
        if (e.classname() == "trigger_multiple") {
            size_t p = 0;
            for (const auto& it : e.items) p += it.isPrimitive();
            trigger = p == 1 && e.value("targetname") && *e.value("targetname") == "t1";
        }
        light |= e.classname() == "light" && e.origin() && e.origin()->z == 96;
    }
    CHECK(trigger && light);
    CHECK(b.selection().size() == 3);
    // One undo step removes the whole paste.
    CHECK(b.undo());
    CHECK(b.map().entities.size() == 1);
}

TEST(clipboard_rejects_non_map_text) {
    Document d;
    std::string err;
    CHECK(d.loadFromText("iwmap 4\n// entity 0\n{\n\"classname\" \"worldspawn\"\n}\n", "d.map", &err));
    CHECK(pasteClipboardText(d, "hello world", &err) == 0 && !err.empty());
    CHECK(selectionToClipboardText(d).empty());
}
