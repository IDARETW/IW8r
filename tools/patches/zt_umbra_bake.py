import io

# Converter (x64-zt worktree): an occlusion bake for a converted map (plan 2.4, research 118). When the extraction's
# world names occluder boxes ("umbra_occluders": [[x0, y0, z0, x1, y1, z1], ...]), the map's Umbra tome gets real
# cells and portals from IW8 Radiant's baker (src/core/umbra_bake.h, copied here as a header); without the key the
# one-cell tome is written as before. A bake that fails falls back to the one-cell tome.

ROOT = 'C:/zsw-iw8r/scriptables/'


def read(p):
    return io.open(ROOT + p, encoding='utf-8', newline='').read()


def write(p, s):
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s)


def patch(p, a, b, n=1):
    s = read(p)
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == n, (p, s.count(a), a[:80])
    write(p, s.replace(a, b))


src = io.open('E:/IW8-Radiant/src/core/umbra_bake.h', encoding='utf-8').read()
write('src/games/iw8/common/umbra_bake.hpp',
      '// Copied from IW8 Radiant (src/core/umbra_bake.h) by tools/patches/zt_umbra_bake.py: do not edit here.\n' + src)

H = 'src/games/iw8/common/replay_render.h'
patch(H, '''// The conservative tome itself (what BuildUmbraTome writes): one tile, one cell over minimum..maximum that holds every''',
      '''// Occluder boxes for the next BuildUmbraTome (x0, y0, z0, x1, y1, z1 each; empty: the one-cell tome). With boxes the
// tome gets real cells and portals (umbra_bake.hpp): Umbra then hides what lies behind them. maxCells limits the
// cells (0: the baker's default).
void SetUmbraOccluders(std::vector<std::array<float, 6>> occluders, uint32_t maxCells = 0);
// What the last BuildUmbraTome did with them ("" when it wrote the one-cell tome without being asked for more).
const std::string &UmbraBakeSummary();
// The conservative tome itself (what BuildUmbraTome writes): one tile, one cell over minimum..maximum that holds every''')
s = read(H)
for inc in ('#include <array>', '#include <string>'):
    if inc not in s:
        i = s.index('#include <')
        s = s[:i] + inc + '\n' + s[i:]
write(H, s)

C = 'src/games/iw8/common/replay_render.cpp'
patch(C, '''std::vector<uint8_t> BuildUmbraTome(const Mesh &m, const StaticModels &staticModels,
                                    const std::vector<uint32_t> &primaryLights,
                                    const uint32_t reflectionProbeInstances)
{''', '''namespace
{
std::vector<std::array<float, 6>> g_umbraOccluders;
uint32_t g_umbraMaxCells = 0;
std::string g_umbraBakeSummary;
} // namespace

void SetUmbraOccluders(std::vector<std::array<float, 6>> occluders, const uint32_t maxCells)
{
    g_umbraOccluders = std::move(occluders);
    g_umbraMaxCells = maxCells;
}

const std::string &UmbraBakeSummary()
{
    return g_umbraBakeSummary;
}

std::vector<uint8_t> BuildUmbraTome(const Mesh &m, const StaticModels &staticModels,
                                    const std::vector<uint32_t> &primaryLights,
                                    const uint32_t reflectionProbeInstances)
{
    g_umbraBakeSummary.clear();''')
patch(C, '''    return BuildConservativeUmbraTome(userIDList, minimum, maximum);
}
''', '''    if (!g_umbraOccluders.empty())
    {
        // An occlusion bake (IW8 Radiant plan 2.4): cells and portals from the occluder boxes. Objects in user ID
        // order; a world surface and a static model are listed by the cells their real box touches, everything else
        // (lights, probes, volumetrics, decal collections, the sky) by every cell.
        umbrabake::Input bake;
        for (unsigned axis = 0; axis < 3; ++axis)
        {
            bake.scene.mn[axis] = minimum[axis];
            bake.scene.mx[axis] = maximum[axis];
        }
        if (g_umbraMaxCells)
            bake.maxCells = g_umbraMaxCells;
        for (const auto &box : g_umbraOccluders)
        {
            umbrabake::Box b;
            for (unsigned axis = 0; axis < 3; ++axis)
            {
                b.mn[axis] = box[axis];
                b.mx[axis] = box[3 + axis];
            }
            bake.occluders.push_back(b);
        }
        bake.objects.resize(userIDList.size());
        for (size_t object = 0; object < userIDList.size(); ++object)
        {
            bake.objects[object].userId = userIDList[object];
            bake.objects[object].everywhere = true;
        }
        for (uint32_t surface = 0; surface < worldSurfaceCount; ++surface)
        {
            float midpoint[3], halfSize[3];
            std::memcpy(midpoint, m.bounds.data() + size_t(surface) * 56, sizeof(midpoint));
            std::memcpy(halfSize, m.bounds.data() + size_t(surface) * 56 + 12, sizeof(halfSize));
            bool finite = true;
            for (unsigned axis = 0; axis < 3; ++axis)
                finite = finite && std::isfinite(midpoint[axis]) && std::isfinite(halfSize[axis]) &&
                         halfSize[axis] >= 0 && halfSize[axis] < 50000.0f;
            if (!finite)
                continue; // the sky and other unbounded surfaces: every cell
            auto &o = bake.objects[surface];
            o.everywhere = false;
            for (unsigned axis = 0; axis < 3; ++axis)
            {
                o.box.mn[axis] = midpoint[axis] - halfSize[axis];
                o.box.mx[axis] = midpoint[axis] + halfSize[axis];
            }
        }
        for (uint32_t model = 0; model < staticModelCount; ++model)
        {
            const auto bounds = TransformBounds(staticModels.models[staticModels.instances[model].model].bounds,
                                                staticModels.instances[model]);
            auto &o = bake.objects[size_t(worldSurfaceCount) + model];
            o.everywhere = false;
            for (unsigned axis = 0; axis < 3; ++axis)
            {
                o.box.mn[axis] = bounds.midpoint[axis] - bounds.halfSize[axis];
                o.box.mx[axis] = bounds.midpoint[axis] + bounds.halfSize[axis];
            }
        }
        auto baked = umbrabake::bake(bake);
        if (baked.ok)
        {
            g_umbraBakeSummary = "occlusion bake: " + std::to_string(baked.stats.occludersUsed) + " of " +
                                 std::to_string(baked.stats.occludersGiven) + " occluder box(es), " +
                                 std::to_string(baked.stats.cells) + " cell(s), " + std::to_string(baked.stats.portals) +
                                 " portal(s), " + std::to_string(baked.stats.viewNodes) + " view tree node(s), " +
                                 std::to_string(userIDList.size()) + " object(s) in " + std::to_string(baked.stats.listEntries) +
                                 " list entries, " + std::to_string(baked.tome.size()) + " bytes";
            return std::move(baked.tome);
        }
        g_umbraBakeSummary = "occlusion bake failed (" + baked.error + "): the one-cell tome is written";
    }
    return BuildConservativeUmbraTome(userIDList, minimum, maximum);
}
''')
s = read(C)
if '#include "umbra_bake.hpp"' not in s:
    i = s.index('#include "')
    s = s[:i] + '#include "umbra_bake.hpp"\n' + s[i:]
write(C, s)

W = 'src/games/iw8/assets/write_maps.cpp'
patch(W, '''    const auto umbraTome = replayrender::BuildUmbraTome(
        mesh, staticModels, lights.umbraLights,
        replayrender::ReflectionProbeInstanceCount(mesh));
''', '''    const auto umbraTome = replayrender::BuildUmbraTome(
        mesh, staticModels, lights.umbraLights,
        replayrender::ReflectionProbeInstanceCount(mesh));
    if (!replayrender::UmbraBakeSummary().empty())
        std::printf("%s\\n", replayrender::UmbraBakeSummary().c_str());
''')
s = read(W)
if '#include <cstdio>' not in s:
    i = s.index('#include <')
    s = s[:i] + '#include <cstdio>\n' + s[i:]
    write(W, s)

R = 'src/convert/iw3_iw8/iw3_render_assets.cpp'
patch(R, '''    g_surfaceShaderSource.clear();
    if (const auto custom = world.find("iw8r_surface_shader");''', '''    // "umbra_occluders": [[x0, y0, z0, x1, y1, z1], ...] (IW8 Radiant plan 2.4): solid boxes for the occlusion bake of
    // the map's Umbra tome; "umbra_max_cells" limits its cells. Without the key the one-cell tome is written.
    {
        std::vector<std::array<float, 6>> occluders;
        if (const auto boxes = world.find("umbra_occluders"); boxes != world.end() && boxes->is_array())
            for (const auto &box : *boxes)
            {
                if (!box.is_array() || box.size() != 6)
                    throw std::runtime_error("umbra_occluders: a box is six numbers (minimum and maximum corner)");
                std::array<float, 6> values{};
                for (size_t k = 0; k < 6; ++k)
                    values[k] = box[k].get<float>();
                occluders.push_back(values);
            }
        replayrender::SetUmbraOccluders(std::move(occluders), world.value("umbra_max_cells", 0u));
    }
    g_surfaceShaderSource.clear();
    if (const auto custom = world.find("iw8r_surface_shader");''')
s = read(R)
if '#include <iw8/common/replay_render.h>' not in s:
    i = s.index('#include "iw3_render_assets.h"')
    j = s.index('\n', i) + 1
    s = s[:j] + '#include <iw8/common/replay_render.h>\n#include <array>\n' + s[j:]
    write(R, s)
print('ok')
