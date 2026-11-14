import io

# Converter (x64-zt worktree): the occlusion data of a built map covers its visibility cells (research 121).
# BuildUmbraTome sized the tome by the drawn geometry only, so the airspace the editor adds to the one cell
# (worldspawn iw8r_airspace) never reached it. Replay 1.20 fails the camera query when the camera is outside the
# tome's box (Umbra::PortalCuller::init 0x141F9FBB0, error 5) and then tests players and vehicles against a cleared
# occlusion buffer. The tome's box now also holds every cell's bounds.

ROOT = 'C:/zsw-iw8r/scriptables/'
p = 'src/games/iw8/common/replay_render.cpp'
s = io.open(ROOT + p, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'
a = '''    for (const auto &instance : staticModels.instances)
    {
        const auto bounds = TransformBounds(staticModels.models[instance.model].bounds, instance);
        for (unsigned axis = 0; axis < 3; ++axis)
        {
            minimum[axis] = std::min(minimum[axis], bounds.midpoint[axis] - bounds.halfSize[axis]);
            maximum[axis] = std::max(maximum[axis], bounds.midpoint[axis] + bounds.halfSize[axis]);
        }
    }
    if (!g_umbraOccluders.empty())
'''.replace('\n', nl)
b = '''    for (const auto &instance : staticModels.instances)
    {
        const auto bounds = TransformBounds(staticModels.models[instance.model].bounds, instance);
        for (unsigned axis = 0; axis < 3; ++axis)
        {
            minimum[axis] = std::min(minimum[axis], bounds.midpoint[axis] - bounds.halfSize[axis]);
            maximum[axis] = std::max(maximum[axis], bounds.midpoint[axis] + bounds.halfSize[axis]);
        }
    }
    // The tome covers the visibility cells (IW8 Radiant research 121). A cell may reach past the drawn geometry:
    // the airspace of a built map, where aircraft and their cameras fly. Replay 1.20 fails the camera query for a
    // camera outside the tome's box (Umbra::PortalCuller::init 0x141F9FBB0 sets error 5) and then tests every
    // scene entity against a cleared occlusion buffer (R_Umbra_CullSceneEnts 0x1418CC4A0): players and vehicles
    // were not drawn from such a camera. Every shipped tome reaches at least 131072 units from the origin.
    for (const auto &cell : m.cells)
    {
        bool finite = true;
        for (unsigned axis = 0; axis < 3; ++axis)
            finite = finite && std::isfinite(cell.bounds.midpoint[axis]) && std::isfinite(cell.bounds.halfSize[axis]) &&
                     cell.bounds.halfSize[axis] >= 0 && cell.bounds.halfSize[axis] < 1.0e6f;
        if (!finite)
            continue;
        for (unsigned axis = 0; axis < 3; ++axis)
        {
            minimum[axis] = std::min(minimum[axis], cell.bounds.midpoint[axis] - cell.bounds.halfSize[axis]);
            maximum[axis] = std::max(maximum[axis], cell.bounds.midpoint[axis] + cell.bounds.halfSize[axis]);
        }
    }
    if (!g_umbraOccluders.empty())
'''.replace('\n', nl)
assert s.count(a) == 1, s.count(a)
s = s.replace(a, b)
io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s)
print('ok')
