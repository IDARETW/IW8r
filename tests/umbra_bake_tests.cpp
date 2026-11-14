#include "umbra_bake.h"
#include "umbra_tome.h"
#include "test_harness.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <random>

using namespace iw8r;

namespace {

umbrabake::Box box(float x0, float y0, float z0, float x1, float y1, float z1) {
    umbrabake::Box b;
    b.mn[0] = x0, b.mn[1] = y0, b.mn[2] = z0;
    b.mx[0] = x1, b.mx[1] = y1, b.mx[2] = z1;
    return b;
}
umbrabake::Object object(uint32_t id, float x, float y, float z, float half = 16) {
    umbrabake::Object o;
    o.userId = id;
    o.box = box(x - half, y - half, z - half, x + half, y + half, z + half);
    return o;
}
bool has(const std::vector<uint32_t>& sorted, uint32_t v) { return std::binary_search(sorted.begin(), sorted.end(), v); }

// Does the segment a -> b pass through the open inside of the box?
bool segmentHits(const umbrabake::Box& bx, const double a[3], const double b[3]) {
    double t0 = 0, t1 = 1;
    for (int k = 0; k < 3; ++k) {
        const double d = b[k] - a[k];
        if (std::fabs(d) < 1e-12) {
            if (a[k] <= bx.mn[k] || a[k] >= bx.mx[k]) return false;
            continue;
        }
        double u0 = (bx.mn[k] - a[k]) / d, u1 = (bx.mx[k] - a[k]) / d;
        if (u0 > u1) std::swap(u0, u1);
        t0 = std::max(t0, u0), t1 = std::min(t1, u1);
    }
    return t1 - t0 > 1e-9;
}

UmbraVisibility visibleFrom(const UmbraTome& tome, float x, float y, float z) {
    UmbraQueryOptions opt;
    opt.distanceCull = false;
    const float p[3] = {x, y, z};
    return tome.queryPoint(p, opt);
}

}  // namespace

// Plan 2.4: two rooms with a solid wall between them. The baked tome passes the stock tome reader's checks, and each
// room sees its own object and the "everywhere" one, not the other room's.
TEST(umbra_bake_two_sealed_rooms) {
    umbrabake::Input in;
    in.scene = box(0, 0, 0, 2048, 1024, 256);
    in.occluders = {box(1000, 0, 0, 1032, 1024, 256)};
    in.objects = {object(0x00000000u, 500, 500, 100), object(0x10000000u, 1500, 500, 100)};
    umbrabake::Object light;
    light.userId = 0x40000001u;
    light.everywhere = true;
    in.objects.push_back(light);
    const umbrabake::Result r = umbrabake::bake(in);
    CHECK(r.ok && r.error.empty());
    CHECK(r.stats.occludersUsed == 1 && r.stats.cells >= 2 && r.stats.solidBoxes == 1 && r.stats.objectsEverywhere == 1);
    UmbraTome tome;
    std::string err;
    CHECK(tome.parse(r.tome, &err));
    if (!r.ok || tome.cells().empty()) return;
    const UmbraTomeReport rep = tome.validate();
    for (const std::string& e : rep.errors) std::printf("  umbra bake: %s\n", e.c_str());
    CHECK(rep.ok && rep.checksum == UmbraChecksum::Valid);
    CHECK(rep.cells == r.stats.cells && rep.portals == r.stats.portals && rep.internalPortals == r.stats.portals);
    CHECK(rep.unmatchedInternal == 0 && rep.outsidePortals == 0 && rep.objectsReferenced == 3 && rep.userIdCount == 3);
    CHECK(rep.viewLeaves == r.stats.viewLeaves && rep.solidViewLeaves == 0);  // a camera is never without a cell
    CHECK(tome.header().numTiles == 1 && tome.tiles()[0].flags == 3 && tome.tiles()[0].hasViewTree());

    const UmbraVisibility left = visibleFrom(tome, 200, 200, 100), right = visibleFrom(tome, 1800, 800, 60);
    CHECK(left.status == UmbraQueryStatus::Ok && right.status == UmbraQueryStatus::Ok);
    CHECK(has(left.userIds, 0x00000000u) && !has(left.userIds, 0x10000000u) && has(left.userIds, 0x40000001u));
    CHECK(has(right.userIds, 0x10000000u) && !has(right.userIds, 0x00000000u) && has(right.userIds, 0x40000001u));
    // Inside the wall: still a cell (the nearest), so something is drawn.
    const UmbraVisibility inWall = visibleFrom(tome, 1016, 500, 100);
    CHECK(inWall.status == UmbraQueryStatus::Ok && !inWall.startCells.empty() && has(inWall.userIds, 0x40000001u));
}

// A wall with a doorway: the room beyond is reached through the door.
TEST(umbra_bake_doorway) {
    umbrabake::Input in;
    in.scene = box(0, 0, 0, 2048, 1024, 256);
    in.occluders = {box(1000, 0, 0, 1032, 400, 256), box(1000, 600, 0, 1032, 1024, 256), box(1000, 400, 128, 1032, 600, 256)};
    in.objects = {object(1, 500, 500, 64), object(2, 1500, 500, 64), object(3, 1500, 100, 64)};
    const umbrabake::Result r = umbrabake::bake(in);
    CHECK(r.ok && r.stats.occludersUsed == 3);
    UmbraTome tome;
    CHECK(tome.parse(r.tome));
    if (!r.ok || tome.cells().empty()) return;
    CHECK(tome.validate().ok && tome.validate().unmatchedInternal == 0);
    // Straight in front of the door: the object behind it must be seen.
    const UmbraVisibility v = visibleFrom(tome, 300, 500, 64);
    CHECK(has(v.userIds, 1) && has(v.userIds, 2));
}

// The promise of the bake: whatever a straight line can see, the tome shows. Rooms on a grid with doorways, objects
// in every room, cameras at random free points: every object some ray reaches is in the query's answer. And the
// tome does hide things.
TEST(umbra_bake_never_hides_what_a_ray_sees) {
    umbrabake::Input in;
    in.scene = box(0, 0, 0, 4096, 4096, 384);
    in.minOccluderSide = 64;
    // Walls every 1024 units along x and y, each with a 192-wide doorway per room, full height.
    for (int w = 1; w <= 3; ++w)
        for (int room = 0; room < 4; ++room) {
            const float a = float(room) * 1024, door = a + 416;
            in.occluders.push_back(box(float(w) * 1024 - 16, a, 0, float(w) * 1024 + 16, door, 384));
            in.occluders.push_back(box(float(w) * 1024 - 16, door + 192, 0, float(w) * 1024 + 16, a + 1024, 384));
            in.occluders.push_back(box(a, float(w) * 1024 - 16, 0, door, float(w) * 1024 + 16, 384));
            in.occluders.push_back(box(door + 192, float(w) * 1024 - 16, 0, a + 1024, float(w) * 1024 + 16, 384));
        }
    uint32_t id = 0;
    for (int rx = 0; rx < 4; ++rx)
        for (int ry = 0; ry < 4; ++ry)
            for (int k = 0; k < 3; ++k) in.objects.push_back(object(id++, float(rx) * 1024 + 200 + float(k) * 250, float(ry) * 1024 + 300 + float(k) * 180, 64 + float(k) * 90, 20));
    const umbrabake::Result r = umbrabake::bake(in);
    CHECK(r.ok);
    UmbraTome tome;
    CHECK(tome.parse(r.tome));
    if (!r.ok || tome.cells().empty()) return;
    const UmbraTomeReport rep = tome.validate();
    for (const std::string& e : rep.errors) std::printf("  umbra bake: %s\n", e.c_str());
    CHECK(rep.ok && rep.unmatchedInternal == 0 && rep.objectsReferenced == in.objects.size());
    std::mt19937 rng(12345);
    std::uniform_real_distribution<double> ux(40, 4056), uz(20, 360);
    size_t cameras = 0, seenByRay = 0, missed = 0, hiddenTotal = 0;
    while (cameras < 60) {
        const double cam[3] = {ux(rng), ux(rng), uz(rng)};
        bool inSolid = false;
        for (const auto& o : in.occluders)
            if (cam[0] > o.mn[0] - 4 && cam[0] < o.mx[0] + 4 && cam[1] > o.mn[1] - 4 && cam[1] < o.mx[1] + 4 && cam[2] > o.mn[2] - 4 && cam[2] < o.mx[2] + 4) inSolid = true;
        if (inSolid) continue;
        ++cameras;
        const UmbraVisibility v = visibleFrom(tome, float(cam[0]), float(cam[1]), float(cam[2]));
        CHECK(v.status == UmbraQueryStatus::Ok && !v.truncated);
        hiddenTotal += in.objects.size() - v.userIds.size();
        for (const umbrabake::Object& o : in.objects) {
            // Rays to the centre and the eight corners of the object's box.
            bool reached = false;
            for (int s = 0; s < 9 && !reached; ++s) {
                double target[3];
                for (int k = 0; k < 3; ++k)
                    target[k] = s == 8 ? 0.5 * (double(o.box.mn[k]) + o.box.mx[k]) : ((s >> k) & 1 ? double(o.box.mx[k]) : double(o.box.mn[k]));
                bool blocked = false;
                for (const auto& oc : in.occluders)
                    if (segmentHits(oc, cam, target)) {
                        blocked = true;
                        break;
                    }
                reached = !blocked;
            }
            if (!reached) continue;
            ++seenByRay;
            if (!has(v.userIds, o.userId)) ++missed;
        }
    }
    std::printf("  umbra bake: %u cells, %u portals, %u view nodes (%u leaves), %zu bytes; %zu cameras: %zu object sightings by ray, %zu missed, "
                "%.0f%% of the objects hidden on average\n",
                r.stats.cells, r.stats.portals, r.stats.viewNodes, r.stats.viewLeaves, r.tome.size(), cameras, seenByRay, missed,
                100.0 * double(hiddenTotal) / double(cameras * in.objects.size()));
    CHECK(missed == 0);
    CHECK(seenByRay > cameras);            // the cameras do see things
    CHECK(hiddenTotal > cameras * 5);      // and the tome hides a good part of the 48 objects
    CHECK(r.stats.viewNodes > 300 && rep.multiCellViewLeaves == 0);  // a real rank directory; one cell per leaf
}

// Limits and refusals.
TEST(umbra_bake_limits) {
    umbrabake::Input in;
    in.scene = box(0, 0, 0, 1024, 1024, 256);
    CHECK(!umbrabake::bake(in).ok);  // no objects
    in.objects = {object(7, 100, 100, 100)};
    // No occluders: one cell that lists everything, like the converter's own tome.
    const umbrabake::Result open = umbrabake::bake(in);
    CHECK(open.ok && open.stats.cells == 1 && open.stats.portals == 0);
    UmbraTome tome;
    CHECK(tome.parse(open.tome) && tome.validate().ok);
    CHECK(has(visibleFrom(tome, 900, 900, 200).userIds, 7));
    // Small occluders are left out; a cell budget stops the cutting but keeps the tome valid.
    in.occluders = {box(100, 100, 0, 110, 110, 200)};
    CHECK(umbrabake::bake(in).stats.occludersUsed == 0);
    in.occluders.clear();
    for (int i = 0; i < 40; ++i) in.occluders.push_back(box(float(i) * 24, 0, 0, float(i) * 24 + 8, 600, 200));
    in.maxCells = 20;
    const umbrabake::Result capped = umbrabake::bake(in);
    CHECK(capped.ok && capped.stats.cells <= 20 && capped.stats.occludersUsed < 40);
    UmbraTome t2;
    CHECK(t2.parse(capped.tome) && t2.validate().ok && has(visibleFrom(t2, 1000, 1000, 250).userIds, 7));
    // A view tree that would outgrow its budget: the space is cut again with fewer cells, and no leaf lists several.
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
    bad.scene = box(0, 0, 0, 0, 10, 10);
    CHECK(!umbrabake::bake(bad).ok);
    // An object whose box touches nothing free still belongs to a cell.
    umbrabake::Input buried;
    buried.scene = box(0, 0, 0, 1024, 1024, 256);
    buried.occluders = {box(200, 200, 0, 800, 800, 256)};
    buried.objects = {object(1, 500, 500, 128, 4), object(2, 100, 100, 100)};
    const umbrabake::Result b = umbrabake::bake(buried);
    UmbraTome t3;
    CHECK(b.ok && b.stats.objectsByNearestCell == 1 && t3.parse(b.tome) && t3.validate().objectsReferenced == 2);
}

// A built map's tome, read back (set IW8R_TEST_GFXMAP_JSON to the ACTS gfx_map export of the built zone; skipped
// without it): it parses, passes the stock reader's checks, and from the cells' centres it hides part of the objects.
TEST(umbra_bake_built_zone_reads_back) {
    const char* path = std::getenv("IW8R_TEST_GFXMAP_JSON");
    if (!path || !*path) return;
    std::vector<uint8_t> bytes;
    std::string err;
    CHECK(extractUmbraTomeFromGfxMapJson(path, bytes, &err));
    UmbraTome tome;
    CHECK(tome.parse(bytes, &err));
    if (tome.cells().empty()) {
        std::printf("  built tome: %s\n", err.c_str());
        return;
    }
    const UmbraTomeReport rep = tome.validate();
    for (const std::string& e : rep.errors) std::printf("  built tome: %s\n", e.c_str());
    CHECK(rep.ok && rep.unmatchedInternal == 0 && rep.objectsReferenced == tome.objectCount());
    size_t queries = 0, seen = 0;
    const size_t step = std::max<size_t>(1, tome.cells().size() / 200);
    for (size_t c = 0; c < tome.cells().size(); c += step) {
        float mn[3], mx[3];
        tome.cellBounds(uint32_t(c), mn, mx);
        const UmbraVisibility v = visibleFrom(tome, 0.5f * (mn[0] + mx[0]), 0.5f * (mn[1] + mx[1]), 0.5f * (mn[2] + mx[2]));
        if (v.status != UmbraQueryStatus::Ok) continue;
        ++queries;
        seen += v.objects.size();
    }
    std::printf("  built tome: %zu bytes, %u cells, %u portals, %u objects (%u surfaces, %u models), %u view leaves (%u with several cells); "
                "from %zu cell centres %.0f%% of the objects are drawn on average\n",
                bytes.size(), rep.cells, rep.portals, tome.objectCount(), rep.userIdsByClass[0], rep.userIdsByClass[1], rep.viewLeaves,
                rep.multiCellViewLeaves, queries, queries ? 100.0 * double(seen) / double(queries * tome.objectCount()) : 0.0);
    CHECK(queries > 0 && rep.multiCellViewLeaves == 0);
}
