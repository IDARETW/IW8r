import io

# umbra_bake.h: a view-tree leaf never lists several cells. Stock tomes hold exactly one cell per leaf (research 110,
# section 6), so what the game does with a longer list is unknown. The bake now builds the view tree right after
# cutting the cells and, when the tree does not fit the node budget, cuts again with half the cells.

p = 'E:/IW8-Radiant/src/core/umbra_bake.h'
s = io.open(p, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'
a = s.index('    // Free space, cut along the occluders\' faces.')
b = s.index('    res.stats.viewNodes = uint32_t(codes.size());')
new = '''    // Free space, cut along the occluders' faces; then the view tree, which maps a point to its cell. A leaf of that
    // tree must lie in one cell (stock tomes hold exactly one cell per leaf, research 110), so when the tree does not
    // fit its node budget the space is cut again with half the cells.
    struct Portal {
        uint32_t target;
        uint8_t face;
        uint16_t plane;
        uint16_t rect[2][2];
    };
    struct Node {
        QBox q;
        std::vector<uint32_t> touching;
    };
    // The scene box is the free space to start with. The rest of the cube is outside the map: no cell (a camera
    // there gets the nearest one), so it joins nothing.
    QBox whole{{0, 0, 0}, {kSide, kSide, kSide}};
    for (int k = 0; k < 3; ++k) whole.hi[k] = std::max<uint32_t>(grid, snapUp(quantum(in.scene.mx[k], k, true)));
    std::vector<QBox> cells, solid;
    std::vector<uint8_t> codes;
    std::vector<std::vector<uint32_t>> leafCells;
    const auto nearestCell = [&](const QBox& q) {
        uint32_t best = 0;
        double bestD = 1e300;
        for (uint32_t i = 0; i < uint32_t(cells.size()); ++i) {
            double d = 0;
            for (int k = 0; k < 3; ++k) {
                const double gap = q.lo[k] >= cells[i].hi[k] ? double(q.lo[k]) - cells[i].hi[k] + 1 : (cells[i].lo[k] >= q.hi[k] ? double(cells[i].lo[k]) - q.hi[k] + 1 : 0.0);
                d += gap * gap;
            }
            if (d < bestD) bestD = d, best = i;
        }
        return best;
    };
    uint32_t maxCells = std::clamp<uint32_t>(in.maxCells, 2u, 60000u);
    for (;;) {
        cells.assign(1, whole);
        solid.clear();
        res.stats.occludersUsed = 0;
        for (const Occ& o : occ) {
            size_t touched = 0;
            for (const QBox& c : cells) touched += overlaps(c, o.q) ? 1 : 0;
            if (!touched) continue;  // already inside other occluders
            if (cells.size() + touched * 6 > maxCells) continue;  // would make too many cells: this one hides nothing
            std::vector<QBox> next;
            next.reserve(cells.size() + touched * 6);
            for (const QBox& c : cells) {
                if (!overlaps(c, o.q)) {
                    next.push_back(c);
                    continue;
                }
                QBox rest = c;  // what is left of the cell; the slabs outside the occluder are cut off it, axis by axis
                for (int k = 0; k < 3; ++k) {
                    if (o.q.lo[k] > rest.lo[k]) {
                        QBox below = rest;
                        below.hi[k] = o.q.lo[k];
                        next.push_back(below);
                        rest.lo[k] = o.q.lo[k];
                    }
                    if (o.q.hi[k] < rest.hi[k]) {
                        QBox above = rest;
                        above.lo[k] = o.q.hi[k];
                        next.push_back(above);
                        rest.hi[k] = o.q.hi[k];
                    }
                }
                solid.push_back(rest);  // inside the occluder
            }
            cells.swap(next);
            ++res.stats.occludersUsed;
        }
        if (cells.empty()) return fail("the occluders fill the whole scene");

        // The view tree: the cube split at midpoints until every leaf lies in one cell, or in no cell (solid, or
        // outside the map), which gets the nearest cell. A node is split along an axis on which a boundary of a cell
        // it touches lies strictly inside it, the longest such axis first. Breadth first, so the k-th inner node's
        // children are nodes 2k + 1 and 2k + 2.
        codes.clear();
        leafCells.clear();
        bool fits = true;
        std::vector<Node> level;
        Node root;
        root.q = QBox{{0, 0, 0}, {kSide, kSide, kSide}};
        root.touching.resize(cells.size());
        for (uint32_t i = 0; i < uint32_t(cells.size()); ++i) root.touching[i] = i;
        level.push_back(std::move(root));
        size_t pending = 1;  // nodes not yet given a code
        while (!level.empty() && fits) {
            std::vector<Node> nextLevel;
            for (Node& n : level) {
                --pending;
                int axis = -1;
                for (int k = 0; k < 3; ++k) {
                    const uint32_t size = n.q.hi[k] - n.q.lo[k];
                    if (size < 2 || (axis >= 0 && size <= n.q.hi[axis] - n.q.lo[axis])) continue;
                    bool cut = false;
                    for (const uint32_t c : n.touching)
                        if ((cells[c].lo[k] > n.q.lo[k] && cells[c].lo[k] < n.q.hi[k]) || (cells[c].hi[k] > n.q.lo[k] && cells[c].hi[k] < n.q.hi[k])) {
                            cut = true;
                            break;
                        }
                    if (cut) axis = k;
                }
                if (axis < 0) {
                    codes.push_back(3);
                    if (n.touching.empty()) n.touching.push_back(nearestCell(n.q));
                    leafCells.push_back(std::move(n.touching));
                    continue;
                }
                if (codes.size() + pending + nextLevel.size() + 2 > in.maxViewNodes) {
                    fits = false;
                    break;
                }
                codes.push_back(uint8_t(axis));
                const uint32_t mid = n.q.lo[axis] + (n.q.hi[axis] - n.q.lo[axis]) / 2;
                Node lo, hi;
                lo.q = hi.q = n.q;
                lo.q.hi[axis] = mid;
                hi.q.lo[axis] = mid;
                for (const uint32_t c : n.touching) {
                    if (overlaps(cells[c], lo.q)) lo.touching.push_back(c);
                    if (overlaps(cells[c], hi.q)) hi.touching.push_back(c);
                }
                nextLevel.push_back(std::move(lo));
                nextLevel.push_back(std::move(hi));
            }
            pending += nextLevel.size();
            level.swap(nextLevel);
        }
        if (fits) break;
        if (cells.size() <= 1) return fail("the view tree does not fit even for one cell");
        maxCells = std::max<uint32_t>(2u, std::min<uint32_t>(maxCells, uint32_t(cells.size())) / 2);
        ++res.stats.retries;
    }
    const uint32_t cellCount = uint32_t(cells.size());
    res.stats.cells = cellCount;
    res.stats.solidBoxes = uint32_t(solid.size());

    // Portals: the shared rectangle of two cells that touch on a face. Found per plane.
    std::vector<std::vector<Portal>> portals(cellCount);
    for (int a = 0; a < 3; ++a) {
        const int b = (a + 1) % 3, c = (a + 2) % 3;
        std::unordered_map<uint32_t, std::vector<uint32_t>> endAt, startAt;  // plane -> cells
        for (uint32_t i = 0; i < cellCount; ++i) {
            endAt[cells[i].hi[a]].push_back(i);
            startAt[cells[i].lo[a]].push_back(i);
        }
        for (const auto& [plane, lows] : endAt) {
            const auto it = startAt.find(plane);
            if (it == startAt.end()) continue;
            for (const uint32_t lo : lows)
                for (const uint32_t hi : it->second) {
                    const uint32_t b0 = std::max(cells[lo].lo[b], cells[hi].lo[b]), b1 = std::min(cells[lo].hi[b], cells[hi].hi[b]);
                    const uint32_t c0 = std::max(cells[lo].lo[c], cells[hi].lo[c]), c1 = std::min(cells[lo].hi[c], cells[hi].hi[c]);
                    if (b0 >= b1 || c0 >= c1) continue;
                    Portal p;
                    p.plane = uint16_t(plane);
                    p.rect[0][0] = uint16_t(b0), p.rect[0][1] = uint16_t(b1 - 1);
                    p.rect[1][0] = uint16_t(c0), p.rect[1][1] = uint16_t(c1 - 1);
                    p.target = hi, p.face = uint8_t(a * 2 + 1);  // from the low cell towards +axis
                    portals[lo].push_back(p);
                    p.target = lo, p.face = uint8_t(a * 2);      // and back
                    portals[hi].push_back(p);
                }
        }
    }
    for (auto& list : portals)
        std::sort(list.begin(), list.end(), [](const Portal& x, const Portal& y) {
            return x.face != y.face ? x.face < y.face : (x.target != y.target ? x.target < y.target : x.plane < y.plane);
        });

    // Which cells list each object.
    std::vector<std::vector<uint32_t>> lists(cellCount);
    for (uint32_t oi = 0; oi < objectCount; ++oi) {
        const Object& o = in.objects[oi];
        bool valid = !o.everywhere;
        for (int k = 0; k < 3 && valid; ++k) valid = std::isfinite(o.box.mn[k]) && std::isfinite(o.box.mx[k]) && o.box.mn[k] <= o.box.mx[k];
        if (!valid) {
            for (auto& l : lists) l.push_back(oi);
            ++res.stats.objectsEverywhere;
            continue;
        }
        QBox q;
        for (int k = 0; k < 3; ++k) {
            q.lo[k] = quantum(o.box.mn[k] - in.objectMargin, k, false);
            q.hi[k] = std::max(quantum(o.box.mx[k] + in.objectMargin, k, true), q.lo[k] + 1);
            if (q.lo[k] > 0) --q.lo[k];  // a face on a cell's boundary belongs to both sides
            if (q.hi[k] < kSide) ++q.hi[k];
        }
        bool any = false;
        for (uint32_t i = 0; i < cellCount; ++i)
            if (overlaps(q, cells[i])) lists[i].push_back(oi), any = true;
        if (!any) {
            lists[nearestCell(q)].push_back(oi);
            ++res.stats.objectsByNearestCell;
        }
    }
'''.replace('\n', nl)
s = s[:a] + new + s[b:]


def rep(x, y):
    global s
    x = x.replace('\n', nl); y = y.replace('\n', nl)
    assert s.count(x) == 1, x[:60]
    s = s.replace(x, y)


rep('''    uint32_t viewNodes = 0, viewLeaves = 0, listEntries = 0, objectsEverywhere = 0, objectsByNearestCell = 0;''',
    '''    uint32_t viewNodes = 0, viewLeaves = 0, listEntries = 0, objectsEverywhere = 0, objectsByNearestCell = 0;
    uint32_t retries = 0;  // times the space was cut again with half the cells, for the view tree to fit''')
rep('''    uint32_t maxViewNodes = 400000;''', '''    uint32_t maxViewNodes = 1000000;  // the view tree's size limit (a leaf never lists several cells)''')
rep('''//   - the view tree: the tile's cube split at midpoints until a leaf lies in one cell; a leaf in no cell (solid, or
//     outside the map) lists the nearest cell, so a camera is never without a cell. Occluder faces are moved inward
//     to a grid of about 4 units so that the midpoints can meet them.''', '''//   - the view tree: the tile's cube split at midpoints until a leaf lies in one cell; a leaf in no cell (solid, or
//     outside the map) lists the nearest cell, so a camera is never without a cell. Occluder faces are moved inward
//     to a grid of about 4 units so that the midpoints can meet them. A leaf never lists several cells (stock tomes
//     never do): when the tree would outgrow its budget, the space is cut again with half the cells.''')
io.open(p, 'w', encoding='utf-8', newline='').write(s)

t = 'E:/IW8-Radiant/tests/umbra_bake_tests.cpp'
s = io.open(t, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'
x = '''    umbrabake::Input bad = in;
    bad.scene = box(0, 0, 0, 0, 10, 10);'''.replace('\n', nl)
y = '''    // A view tree that would outgrow its budget: the space is cut again with fewer cells, and no leaf lists several.
    umbrabake::Input tight;
    tight.scene = box(0, 0, 0, 4096, 4096, 512);
    tight.objects = {object(1, 100, 100, 100), object(2, 4000, 4000, 100)};
    for (int i = 0; i < 12; ++i)
        for (int j = 0; j < 12; ++j) tight.occluders.push_back(box(float(i) * 340 + 13, float(j) * 340 + 29, 0, float(i) * 340 + 213, float(j) * 340 + 229, 400));
    tight.maxViewNodes = 3000;
    const umbrabake::Result squeezed = umbrabake::bake(tight);
    UmbraTome t4;
    CHECK(squeezed.ok && squeezed.stats.retries > 0 && squeezed.stats.viewNodes <= 3000 && t4.parse(squeezed.tome));
    if (squeezed.ok && !t4.cells().empty()) {
        const UmbraTomeReport rr = t4.validate();
        CHECK(rr.ok && rr.multiCellViewLeaves == 0 && rr.solidViewLeaves == 0 && rr.unmatchedInternal == 0);
    }
    umbrabake::Input bad = in;
    bad.scene = box(0, 0, 0, 0, 10, 10);'''.replace('\n', nl)
assert s.count(x) == 1
s = s.replace(x, y)
x2 = '''    CHECK(queries > 0);'''
assert s.count(x2) == 1
s = s.replace(x2, '''    CHECK(queries > 0 && rep.multiCellViewLeaves == 0);''')
io.open(t, 'w', encoding='utf-8', newline='').write(s)
print('ok')
