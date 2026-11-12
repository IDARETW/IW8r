// Header-only helpers of the camera's scene (src/viewport): the model a scriptable entity is drawn with
// (scriptable_model.h) and which passes draw a piece of the stock reference (reference_markers.h, through render.h).
#include "../src/viewport/render.h"
#include "../src/viewport/scriptable_model.h"

#include <string>
#include <vector>

#include "map_text.h"
#include "scriptable_catalog.h"
#include "test_harness.h"

using namespace iw8r;

namespace {

Entity entityOf(const std::string& body) {
    Id next = 1;
    ParseResult pr = parseMap("iwmap 4\n{\n" + body + "}\n", next);
    return pr.map && !pr.map->entities.empty() ? pr.map->entities.front() : Entity{};
}

// Two definitions, one with a model and one without (16 tab-separated columns, as the catalogue file).
ScriptableCatalog testCatalog() {
    ScriptableCatalog c;
    c.parse(
        "name\tzones\talways_loaded\tparts\tserver_instanced_parts\tusable_parts\tmax_dynent_parts\tphysics_dynents\t"
        "instance_dynent\tsparse_occluder\tcollision_flags\tphysics_limit\tflags\tmodel\tsource\tverified\n"
        "iw8r_test_crate\tcommon_mp\t1\t1\t0\t0\t1\t0\t1\t0\t0\t0\t0x0\tiw8r_test_crate_model\tobserved\t1\n"
        "iw8r_test_empty\tcommon_mp\t1\t1\t0\t0\t1\t0\t1\t0\t0\t0\t0x0\t\tobserved\t1\n");
    return c;
}

}  // namespace

TEST(scriptable_model_name_from_catalogue) {
    const ScriptableCatalog cat = testCatalog();
    CHECK(cat.size() == 2);
    const Entity s = entityOf("\"classname\" \"scriptable\"\n\"scriptable\" \"IW8R_Test_Crate\"\n\"origin\" \"0 0 0\"\n");
    CHECK(app::scriptableModelName(s, cat) == "iw8r_test_crate_model");
    // Drawn exactly as a misc_model with that model key: the copy carries the key, the rest is kept.
    const Entity m = app::withModelKey(s, app::scriptableModelName(s, cat));
    CHECK(m.value("model") && *m.value("model") == "iw8r_test_crate_model");
    CHECK(m.classname() == "scriptable" && m.origin().has_value() && m.id == s.id);
    // Otherwise the box: an own model key, a definition without a model, an unknown one, no definition, other classes.
    CHECK(app::scriptableModelName(entityOf("\"classname\" \"scriptable\"\n\"scriptable\" \"iw8r_test_crate\"\n\"model\" \"x\"\n"), cat).empty());
    CHECK(app::scriptableModelName(entityOf("\"classname\" \"scriptable\"\n\"scriptable\" \"iw8r_test_empty\"\n"), cat).empty());
    CHECK(app::scriptableModelName(entityOf("\"classname\" \"scriptable\"\n\"scriptable\" \"no_such_def\"\n"), cat).empty());
    CHECK(app::scriptableModelName(entityOf("\"classname\" \"scriptable\"\n\"origin\" \"0 0 0\"\n"), cat).empty());
    CHECK(app::scriptableModelName(entityOf("\"classname\" \"misc_model\"\n\"scriptable\" \"iw8r_test_crate\"\n"), cat).empty());
    // An empty model key counts as none, and is set in place rather than added twice.
    const Entity e = entityOf("\"classname\" \"scriptable\"\n\"scriptable\" \"iw8r_test_crate\"\n\"model\" \"\"\n");
    CHECK(app::scriptableModelName(e, cat) == "iw8r_test_crate_model");
    const Entity em = app::withModelKey(e, "iw8r_test_crate_model");
    size_t modelKeys = 0;
    for (const auto& it : em.items)
        if (const KeyValueLine* kv = it.kv(); kv && kv->name() == "model") ++modelKeys;
    CHECK(modelKeys == 1 && em.value("model") && *em.value("model") == "iw8r_test_crate_model");
    // No lookup at all: nothing.
    CHECK(app::scriptableModelName(s, std::function<std::string(const std::string&)>{}).empty());
}

TEST(reference_markers_left_out_of_captures_and_shadows) {
    std::vector<app::InstancedMesh> meshes(2);
    meshes[0].tris.resize(36);       // a prop's mesh, two placements
    meshes[0].rows.resize(2 * 12);
    meshes[1].tris.resize(36);       // the amber marker cube, three placements
    meshes[1].rows.resize(3 * 12);
    meshes[1].editorMarker = true;
    CHECK(app::referenceVertices(meshes, app::ReferencePass::Screen) == 36 * 5);
    CHECK(app::referenceVertices(meshes, app::ReferencePass::Capture) == 36 * 2);
    CHECK(app::referenceVertices(meshes, app::ReferencePass::Shadow) == 36 * 2);
    CHECK(app::drawsReferencePiece(false, app::ReferencePass::Capture));
    CHECK(app::drawsReferencePiece(true, app::ReferencePass::Screen));
    CHECK(!app::drawsReferencePiece(true, app::ReferencePass::Capture));
    CHECK(!app::drawsReferencePiece(true, app::ReferencePass::Shadow));
    // Unmarked (the default) is real geometry.
    meshes[1].editorMarker = false;
    CHECK(app::referenceVertices(meshes, app::ReferencePass::Capture) == 36 * 5);
}
