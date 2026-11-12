// Layers: header declarations, per-primitive layer lines, header replacement through the undo history.
#include "document.h"
#include "layers.h"
#include "test_harness.h"

using namespace iw8r;

namespace {

const char* kLayered =
    "iwmap 4\r\n"
    "\"000_Global\" flags  active\r\n"
    "\"The Map\" flags \r\n"
    "// entity 0\r\n"
    "{\r\n"
    "\"classname\" \"worldspawn\"\r\n"
    "// brush 0\r\n"
    "{\r\n"
    "layer \"000_Global/Floors\"\r\n"
    " ( 64 64 0 ) ( 0 64 0 ) ( 0 0 0 ) caulk 64 64 0 0 0 0 lightmap_gray 16384 16384 0 0 0 0\r\n"
    " ( 0 0 32 ) ( 0 64 32 ) ( 64 64 32 ) floor 64 64 0 0 0 0 lightmap_gray 16384 16384 0 0 0 0\r\n"
    " ( 0 0 32 ) ( 64 0 32 ) ( 64 0 0 ) caulk 64 64 0 0 0 0 lightmap_gray 16384 16384 0 0 0 0\r\n"
    " ( 64 64 32 ) ( 0 64 32 ) ( 0 64 0 ) caulk 64 64 0 0 0 0 lightmap_gray 16384 16384 0 0 0 0\r\n"
    " ( 64 0 32 ) ( 64 64 32 ) ( 64 64 0 ) caulk 64 64 0 0 0 0 lightmap_gray 16384 16384 0 0 0 0\r\n"
    " ( 0 64 32 ) ( 0 0 32 ) ( 0 0 0 ) caulk 64 64 0 0 0 0 lightmap_gray 16384 16384 0 0 0 0\r\n"
    "}\r\n"
    "// brush 1\r\n"
    "{\r\n"
    " mesh\r\n"
    " {\r\n"
    "  toolFlags splitGeo;\r\n"
    "  tm2co/ee_trim_beam_airport_concrete\r\n"
    "  lightmap_gray\r\n"
    "  2 2 16 8\r\n"
    "  (\r\n"
    "\tv 64 128 0 t 0 0 0 0\r\n"
    "\tv 64 128 64 t 0 -64 0 4\r\n"
    "  )\r\n"
    "  (\r\n"
    "\tv 128 128 0 t 64 0 4 0\r\n"
    "\tv 128 128 64 t 64 -64 4 4\r\n"
    "  )\r\n"
    " }\r\n"
    "}\r\n"
    "}\r\n";

const Primitive* nth(const Document& d, size_t n) {
    size_t k = 0;
    for (const auto& it : d.map().entities[0].items)
        if (it.isPrimitive() && k++ == n) return it.prim();
    return nullptr;
}

}  // namespace

TEST(layers_declarations_and_header_edit) {
    Document d;
    CHECK(d.loadFromText(kLayered, "l.map", nullptr));
    const auto decl = declaredLayers(d.map().header);
    CHECK(decl.size() == 2 && decl[0].name == "000_Global" && decl[0].flags == "  active" && decl[1].name == "The Map");
    const std::string h2 = headerWithLayer(d.map().header, "detail props", "\r\n");
    CHECK(declaredLayers(h2).size() == 3 && declaredLayers(h2)[2].name == "detail props");
    CHECK(h2.find("\"The Map\" flags \r\n\"detail props\" flags \r\n") != std::string::npos);
    CHECK(headerWithLayer(h2, "detail props", "\r\n") == h2);  // already declared
    CHECK(headerWithLayer(h2, "bad\"name", "\r\n") == h2);
    CHECK(d.setHeader(h2, "New Layer"));
    CHECK(d.text().find("\"detail props\" flags ") != std::string::npos);
    CHECK(d.undo() && d.text() == kLayered);
    CHECK(d.redo() && declaredLayers(d.map().header).size() == 3);
}

TEST(layers_primitive_lines_only_change_that_line) {
    Document d;
    CHECK(d.loadFromText(kLayered, "l.map", nullptr));
    Primitive brush = *nth(d, 0);
    Primitive patch = *nth(d, 1);
    CHECK(primitiveLayer(brush) == "000_Global/Floors");
    CHECK(primitiveLayer(patch).empty());
    // Replace an existing layer line.
    CHECK(setPrimitiveLayer(brush, "detail props", "\r\n"));
    CHECK(brush.lines.size() == nth(d, 0)->lines.size() && primitiveLayer(brush) == "detail props");
    size_t diff = 0;
    for (size_t i = 0; i < brush.lines.size(); ++i) diff += brush.lines[i].text != nth(d, 0)->lines[i].text;
    CHECK(diff == 1);
    CHECK(brush.faces.size() == 6);
    CHECK(!setPrimitiveLayer(brush, "detail props", "\r\n"));  // unchanged
    // Insert into a patch: after the inner `{`, material and vertices still parse.
    CHECK(setPrimitiveLayer(patch, "high town", "\r\n"));
    CHECK(primitiveLayer(patch) == "high town");
    CHECK(patch.kind == PrimKind::Patch && patch.patchMaterial == "tm2co/ee_trim_beam_airport_concrete" && patch.verts.size() == 4);
    CHECK(patch.text().find(" mesh\r\n {\r\nlayer \"high town\"\r\n  toolFlags splitGeo;") != std::string::npos);
    // Remove again: back to the original bytes.
    CHECK(setPrimitiveLayer(patch, "", "\r\n"));
    CHECK(patch.text() == nth(d, 1)->text());
    // Through the document: one undo step restores the text.
    CHECK(d.editPrimitives({{d.map().entities[0].id, d.map().entities[0].items[2].id, brush}}, "Move To Layer"));
    CHECK(d.undo() && d.text() == kLayered);
}
