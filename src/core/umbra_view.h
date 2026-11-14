// IW8 Radiant Suite - the Umbra visibility view (plan M2.2): what a stock map's occlusion tome lets the game draw from
// a camera, as data the viewport can use. Built on core umbra_tome (research 110).
//
//   - umbraViewVisibility: one view query as bit sets by GfxWorld surface index (user ID class 0) and by static model
//     collection index (class 1; tests/umbra_tome_tests.cpp umbra_static_model_ids_on_shipment), plus counts per class.
//     A bit is set for what is visible AND for what the tome does not list at all (it cannot hide what it does not
//     know), so "bit clear" means exactly "the game culls this from here".
//   - umbraOverlayLines: the camera's cell, its portals and the visible cells near the camera as coloured lines.
#pragma once

#include <cstdint>
#include <vector>

#include "geometry.h"
#include "umbra_tome.h"

namespace iw8r {

// Totals of a tome, made once per tome: how many user IDs each class has, and the bit sets of the surfaces / static
// model collections the tome lists (the complement is "unknown to the tome").
struct UmbraTomeIndex {
    uint32_t idsByClass[kUmbraClassCount] = {};
    std::vector<uint32_t> knownSurfaces, knownModels;  // bit sets by index
};
UmbraTomeIndex umbraTomeIndex(const UmbraTome& tome);

struct UmbraViewResult {
    UmbraQueryStatus status = UmbraQueryStatus::Ok;
    bool truncated = false;
    // The cull is meaningful: the camera is in a cell. Otherwise (outside the tome, no tile, in solid) the bit sets
    // are empty: nothing is hidden.
    bool culls = false;
    std::vector<uint32_t> surfaceBits, modelBits;
    uint32_t visibleByClass[kUmbraClassCount] = {};
    uint32_t cells = 0;
    uint32_t tile = kUmbraNone;
    std::vector<uint32_t> startCells, visibleCells;  // global cell indices (for the overlay)
    double seconds = 0;  // the query's time
};
// forward / up as UmbraTome::queryView; tanHalfX / tanHalfY the tangents of the half field of view.
UmbraViewResult umbraViewVisibility(const UmbraTome& tome, const UmbraTomeIndex& index, const Vec3& pos, const Vec3& forward,
                                    const Vec3& up, double tanHalfX, double tanHalfY, const UmbraQueryOptions& opt = {});

const char* umbraStatusText(UmbraQueryStatus s);

struct UmbraLine {
    Vec3 a, b;
    uint32_t rgba = 0xffffffffu;  // R in the low byte
};
// The camera's cells (bright), their portals (yellow; gates magenta) and up to maxCells visible cells within `radius`
// of the eye (dim), as box and quad outlines.
std::vector<UmbraLine> umbraOverlayLines(const UmbraTome& tome, const UmbraViewResult& view, const Vec3& eye, double radius,
                                         size_t maxCells = 400);

}  // namespace iw8r
