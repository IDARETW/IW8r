import io

# Research 105 section 4 / research 133 section 7: the static-model self visibility (SELFVIS), exported by the ACTS fork
# (3667413c) as the GLB attribute _SELFVIS (the vertex's u32 as four bytes). Core: decodeSelfVisibility, the
# ImportedMesh field (direction turned like the normals, times the amount; w the cone value), the primitive's
# extras.sourceFlags, and a test.

ROOT = 'E:/IW8-Radiant/'


def patch(p, a, b, count=1):
    s = io.open(ROOT + p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == count, (p, s.count(a), a[:70])
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s.replace(a, b))


MI = 'src/core/model_import.h'
patch(MI, '''    std::vector<std::array<float, 4>> color;
};''', '''    std::vector<std::array<float, 4>> color;
    // Stock models (ACTS GLB attribute _SELFVIS, research 105 section 4): per vertex the self visibility, xyz = the
    // bent normal (turned like the normals) times the visible amount (0..1), w = the cone value of the game's
    // polynomial. Empty when the primitive has none.
    std::vector<std::array<float, 4>> selfVis;
    uint32_t sourceFlags = 0;  // glTF primitive extras.sourceFlags (the XSurface flags ACTS copies; 0x40 = self visibility)
};''')

GH = 'src/core/glb_mesh.h'
patch(GH, '''// Same as readGlb for bytes already in memory (out.name is left as the caller set it).''', '''// The static-model self-visibility word (XModelSurfs vertex +8; Replay's static-model VS, research 105 section 4):
// octahedral direction in bits 0-8 / 9-17 (folded when z < 0), amount in 18-24, cone in 25-31. Returns the unit
// direction times the amount (xyz) and the cone (w).
std::array<float, 4> decodeSelfVisibility(uint32_t word);

// Same as readGlb for bytes already in memory (out.name is left as the caller set it).''')
patch(GH, '''#include <string>
''', '''#include <array>
#include <cstdint>
#include <string>
''')

G = 'src/core/glb_mesh.cpp'
patch(G, '''            if (const json::Value* ind = prim.get("indices")) {
                size_t ni = 0;''', '''            if (const json::Value* sv = attrs->get("_SELFVIS")) {
                // ACTS: the vertex's u32 as VEC4 UNSIGNED_BYTE, not normalised (research 105 section 4).
                size_t ns = 0;
                if (!readAccessor(int(sv->num), 4, v, ns)) return false;
                if (ns != n) return fail("_SELFVIS count differs from POSITION");
                m.selfVis.resize(n);
                for (size_t i = 0; i < n; ++i) {
                    const uint32_t word = uint32_t(v[i * 4]) | uint32_t(v[i * 4 + 1]) << 8 | uint32_t(v[i * 4 + 2]) << 16 |
                                          uint32_t(v[i * 4 + 3]) << 24;
                    const std::array<float, 4> d = decodeSelfVisibility(word);
                    const double amount = std::sqrt(double(d[0]) * d[0] + double(d[1]) * d[1] + double(d[2]) * d[2]);
                    const Vec3 dir = amount > 0 ? world.normal(Vec3{d[0] / amount, d[1] / amount, d[2] / amount}) : Vec3{0, 0, 0};
                    m.selfVis[i] = {float(dir.x * amount), float(dir.y * amount), float(dir.z * amount), d[3]};
                }
            }
            if (const json::Value* ex = prim.get("extras"))
                if (const json::Value* sf = ex->get("sourceFlags")) m.sourceFlags = uint32_t(std::max(0.0, sf->num));
            if (const json::Value* ind = prim.get("indices")) {
                size_t ni = 0;''')
patch(G, '''bool parseGlb(const std::string& bytes, ImportedModel& out, std::string* err, const GlbReadOptions& opt) {''',
      '''std::array<float, 4> decodeSelfVisibility(uint32_t word) {
    double x = double(word & 511u) / 511.0 * 2.0 - 1.0, y = double((word >> 9) & 511u) / 511.0 * 2.0 - 1.0;
    double z = 1.0 - std::fabs(x) - std::fabs(y);
    if (z < 0) {  // the lower hemisphere, folded
        const double fx = (1.0 - std::fabs(y)) * (x < 0 ? -1.0 : 1.0), fy = (1.0 - std::fabs(x)) * (y < 0 ? -1.0 : 1.0);
        x = fx, y = fy;
    }
    const double len = std::sqrt(x * x + y * y + z * z);
    const double amount = double((word >> 18) & 127u) / 127.0;
    const double k = len > 0 ? amount / len : 0.0;
    return {float(x * k), float(y * k), float(z * k), float(double((word >> 25) & 127u) / 127.0)};
}

bool parseGlb(const std::string& bytes, ImportedModel& out, std::string* err, const GlbReadOptions& opt) {''')
print('ok')
