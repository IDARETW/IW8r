// Auto-caulk (2.6): faces pressed against another solid brush become caulk; open faces and tool faces stay.
#include <string>

#include "auto_caulk.h"
#include "document.h"
#include "test_harness.h"

using namespace iw8r;

namespace {
std::string box(int x0, int y0, int z0, int x1, int y1, int z1, const std::string& mat) {
    auto f = [&](const std::string& a, const std::string& b, const std::string& c) {
        return " ( " + a + " ) ( " + b + " ) ( " + c + " ) " + mat + " 64 64 0 0 0 0 lightmap_gray 16384 16384 0 0 0 0\n";
    };
    const std::string X0 = std::to_string(x0), Y0 = std::to_string(y0), Z0 = std::to_string(z0), X1 = std::to_string(x1),
                      Y1 = std::to_string(y1), Z1 = std::to_string(z1);
    std::string t = "{\n";
    t += f(X1 + " " + Y1 + " " + Z0, X0 + " " + Y1 + " " + Z0, X0 + " " + Y0 + " " + Z0);  // bottom
    t += f(X0 + " " + Y0 + " " + Z1, X0 + " " + Y1 + " " + Z1, X1 + " " + Y1 + " " + Z1);  // top
    t += f(X0 + " " + Y0 + " " + Z0, X0 + " " + Y0 + " " + Z1, X1 + " " + Y0 + " " + Z1);  // -y
    t += f(X1 + " " + Y0 + " " + Z0, X1 + " " + Y0 + " " + Z1, X1 + " " + Y1 + " " + Z1);  // +x
    t += f(X1 + " " + Y1 + " " + Z0, X1 + " " + Y1 + " " + Z1, X0 + " " + Y1 + " " + Z1);  // +y
    t += f(X0 + " " + Y1 + " " + Z0, X0 + " " + Y1 + " " + Z1, X0 + " " + Y0 + " " + Z1);  // -x
    return t + "}\n";
}
}  // namespace

TEST(auto_caulk_touching_faces) {
    // A floor slab with a small box standing on it: the box's bottom is hidden (inside the slab's top); the slab's top is
    // not (it is bigger than the box). A trigger box touching the slab hides nothing.
    std::string m = "iwmap 4\n{\n\"classname\" \"worldspawn\"\n";
    m += box(0, 0, -16, 256, 256, 0, "tm2co/me_curb_stone_01");
    m += box(64, 64, 0, 128, 128, 64, "tm2co/me_curb_stone_01");
    m += box(256, 0, -16, 320, 256, 0, "trigger");
    m += "}\n";
    Document doc;
    std::string err;
    CHECK(doc.loadFromText(m, "", &err));
    const std::string before = doc.text();
    const size_t n = autoCaulk(doc, false);
    CHECK(n == 1);
    size_t caulked = 0;
    for (const auto& e : doc.map().entities)
        for (const auto& it : e.items)
            if (const Primitive* p = it.prim())
                for (const auto& f : p->faces) caulked += f.material() == "caulk";
    CHECK(caulked == 1);
    CHECK(autoCaulk(doc, false) == 0);  // nothing left to do
    doc.undo();
    CHECK(doc.text() == before);
}
