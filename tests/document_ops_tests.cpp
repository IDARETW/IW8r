// Document operations used by the brush tools: editPrimitives, addEntity, groupSelection, ungroupSelection.
#include "document.h"
#include "test_harness.h"

using namespace iw8r;

namespace {

const char* kRoom =
    "iwmap 4\r\n"
    "\"000_Global\" flags active\r\n"
    "\"The Map\" flags \r\n"
    "// entity 0\r\n"
    "{\r\n"
    "\"classname\" \"worldspawn\"\r\n"
    "// brush 0\r\n"
    "{\r\n"
    " ( 64 64 0 ) ( 0 64 0 ) ( 0 0 0 ) caulk 64 64 0 0 0 0 lightmap_gray 16384 16384 0 0 0 0\r\n"
    " ( 0 0 32 ) ( 0 64 32 ) ( 64 64 32 ) floor 64 64 0 0 0 0 lightmap_gray 16384 16384 0 0 0 0\r\n"
    " ( 0 0 32 ) ( 64 0 32 ) ( 64 0 0 ) caulk 64 64 0 0 0 0 lightmap_gray 16384 16384 0 0 0 0\r\n"
    " ( 64 64 32 ) ( 0 64 32 ) ( 0 64 0 ) caulk 64 64 0 0 0 0 lightmap_gray 16384 16384 0 0 0 0\r\n"
    " ( 64 0 32 ) ( 64 64 32 ) ( 64 64 0 ) caulk 64 64 0 0 0 0 lightmap_gray 16384 16384 0 0 0 0\r\n"
    " ( 0 64 32 ) ( 0 0 32 ) ( 0 0 0 ) caulk 64 64 0 0 0 0 lightmap_gray 16384 16384 0 0 0 0\r\n"
    "}\r\n"
    "}\r\n";

Document room() {
    Document d;
    d.loadFromText(kRoom, "room.map", nullptr);
    return d;
}

const EntityItem* firstPrim(const Entity& e) {
    for (const auto& it : e.items)
        if (it.isPrimitive()) return &it;
    return nullptr;
}

}  // namespace

TEST(edit_primitives_insert_replace_delete_undo) {
    Document d = room();
    const Entity& w = d.map().entities[0];
    const EntityItem* brush = firstPrim(w);
    CHECK(brush != nullptr);
    Primitive moved = *brush->prim();
    moved.translate({128, 0, 0});
    // Insert a copy, replace the original with a moved version, then delete the copy: three separate steps.
    CHECK(d.editPrimitives({{w.id, kNoId, moved}}, "Create Brush"));
    CHECK(d.selection().size() == 1);
    size_t prims = 0;
    for (const auto& it : d.map().entities[0].items) prims += it.isPrimitive();
    CHECK(prims == 2);
    Primitive up = *firstPrim(d.map().entities[0])->prim();  // re-fetch: the insert reallocated the items
    up.translate({0, 0, 64});
    const Id originalItem = firstPrim(d.map().entities[0])->id;
    CHECK(d.editPrimitives({{w.id, originalItem, up}}, "Resize"));
    CHECK(d.text().find("( 0 0 96 )") != std::string::npos);
    CHECK(d.editPrimitives({{w.id, originalItem, std::nullopt}}, "Delete"));
    prims = 0;
    for (const auto& it : d.map().entities[0].items) prims += it.isPrimitive();
    CHECK(prims == 1);
    CHECK(d.history().size() == 3);
    while (d.undo()) {}
    CHECK(d.text() == kRoom);
    CHECK(d.redo() && d.redo() && d.redo());
    CHECK(d.undo() && d.undo() && d.undo());
    CHECK(d.text() == kRoom);
}

TEST(add_point_and_brush_entities) {
    Document d = room();
    const Id spawn = d.addEntity({{"classname", "mp_tdm_spawn"}, {"origin", "32 32 16"}, {"angles", "0 90 0"}}, {}, "Create mp_tdm_spawn");
    CHECK(spawn != kNoId);
    CHECK(d.map().entities.size() == 2);
    CHECK(d.isSelected({spawn, kNoId}));
    CHECK(d.findEntity(spawn)->origin()->x == 32);
    CHECK(d.text().find("// entity 1\r\n{\r\n\"classname\" \"mp_tdm_spawn\"\r\n\"origin\" \"32 32 16\"") != std::string::npos);
    Primitive trig = *firstPrim(d.map().entities[0])->prim();
    trig.translate({0, 128, 0});
    const Id t = d.addEntity({{"classname", "trigger_multiple"}, {"targetname", "t1"}}, {trig}, "Create trigger_multiple");
    CHECK(d.findEntity(t) && d.findEntity(t)->classname() == "trigger_multiple");
    CHECK(d.selection().size() == 1 && d.selection().begin()->entity == t);
    // A saved/reloaded document parses the new entities the same way.
    Document r;
    CHECK(r.loadFromText(d.text(), "r.map", nullptr));
    CHECK(r.map().entities.size() == 3);
    CHECK(r.map().entities[2].classname() == "trigger_multiple");
    CHECK(d.undo() && d.undo());
    CHECK(d.text() == kRoom);
}

TEST(set_material_keeps_other_fields) {
    Document d = room();
    Primitive p = *firstPrim(d.map().entities[0])->prim();
    CHECK(p.setMaterial("tm2co/me_curb_stone_01", 1));
    CHECK(p.faces[1].material() == "tm2co/me_curb_stone_01");
    CHECK(p.faces[0].material() == "caulk");
    CHECK(p.lines[3].text == " ( 0 0 32 ) ( 0 64 32 ) ( 64 64 32 ) tm2co/me_curb_stone_01 64 64 0 0 0 0 lightmap_gray 16384 16384 0 0 0 0");
    CHECK(p.setMaterial("tm2co/me_curb_stone_01"));
    for (const auto& f : p.faces) CHECK(f.material() == "tm2co/me_curb_stone_01");
    CHECK(!p.setMaterial("tm2co/me_curb_stone_01"));  // no change
    CHECK(!p.setMaterial("bad name"));
}

TEST(texture_fields_edit_only_those_tokens) {
    Document d = room();
    Primitive p = *firstPrim(d.map().entities[0])->prim();
    auto f = p.textureFields(1);
    CHECK(f && (*f)[0] == 64 && (*f)[1] == 64 && (*f)[2] == 0 && (*f)[4] == 0);
    Primitive::TexFields tf;
    tf.width = 128;
    tf.rot = 45;
    CHECK(p.setTextureFields(tf, 1));
    CHECK(p.lines[3].text == " ( 0 0 32 ) ( 0 64 32 ) ( 64 64 32 ) floor 128 64 0 0 45 0 lightmap_gray 16384 16384 0 0 0 0");
    CHECK(p.lines[2].text.find("caulk 64 64 0 0 0 0") != std::string::npos);  // other faces untouched
    tf = {};
    tf.xoff = -16.5;
    CHECK(p.setTextureFields(tf));  // every face
    for (size_t i = 0; i < p.faces.size(); ++i) CHECK((*p.textureFields(i))[2] == -16.5);
    CHECK(!p.setTextureFields(tf));  // unchanged
}

TEST(group_and_ungroup_selection) {
    Document d = room();
    const Entity& w = d.map().entities[0];
    d.select({w.id, firstPrim(w)->id}, false);
    const Id g = d.groupSelection("script_brushmodel");
    CHECK(g != kNoId);
    CHECK(d.map().entities.size() == 2);
    CHECK(firstPrim(d.map().entities[0]) == nullptr);
    CHECK(d.findEntity(g)->classname() == "script_brushmodel");
    CHECK(firstPrim(*d.findEntity(g)) != nullptr);
    const std::string grouped = d.text();
    CHECK(d.undo());
    CHECK(d.text() == kRoom);
    CHECK(d.redo());
    CHECK(d.text() == grouped);
    CHECK(d.ungroupSelection());
    CHECK(d.map().entities.size() == 1);
    CHECK(firstPrim(d.map().entities[0]) != nullptr);
    CHECK(d.undo());
    CHECK(d.text() == grouped);
}

TEST(compound_groups_make_one_undo_step) {
    Document d = room();
    const Id w = d.map().entities[0].id;
    Primitive copy = *firstPrim(d.map().entities[0])->prim();
    copy.translate({128, 0, 0});
    d.beginGroup("Compound");
    CHECK(d.editPrimitives({{w, kNoId, copy}}, "Create Brush"));
    d.beginGroup("Inner");  // nested: the outermost name wins
    const Id spawn = d.addEntity({{"classname", "mp_tdm_spawn"}, {"origin", "0 0 0"}}, {}, "Create mp_tdm_spawn");
    CHECK(d.setKeyValue(spawn, "angles", "0 90 0"));
    d.endGroup();
    CHECK(d.inGroup());
    d.endGroup();
    CHECK(!d.inGroup());
    CHECK(d.history().size() == 1 && d.history()[0].name == "Compound");
    const std::string after = d.text();
    CHECK(d.undo() && d.text() == kRoom && !d.canUndo());
    CHECK(d.redo() && d.text() == after);
    // An empty group adds nothing; the next edit after a group is its own step.
    d.beginGroup("Empty");
    d.endGroup();
    CHECK(d.history().size() == 1);
    CHECK(d.setKeyValue(spawn, "angles", "0 180 0"));
    CHECK(d.history().size() == 2);
    // A group opened after an undo truncates the redo tail once and then merges.
    CHECK(d.undo() && d.undo());
    d.beginGroup("Again");
    CHECK(d.editPrimitives({{w, kNoId, copy}}, "Create Brush"));
    CHECK(d.editPrimitives({{w, kNoId, copy}}, "Create Brush"));
    d.endGroup();
    CHECK(d.history().size() == 1 && d.history()[0].name == "Again");
    CHECK(d.undo() && d.text() == kRoom);
}
