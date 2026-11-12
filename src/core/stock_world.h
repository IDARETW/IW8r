// Stock Replay world geometry (IW8 1.20, MW2019 profile) read from ACTS exports of a zone:
//   gfx_map         GfxWorld: one GfxSurface per draw surface (index range, vertex count, material name)
//   gfx_map_trzone  GfxWorldTransientZone: the surface draw data. drawVerts.posData holds float3 positions (12 bytes,
//                   Z-up inches), drawVerts.indices holds uint16 indices (surface-relative)
// Route, commands, sizes and the verified layout: docs/research/30_stock_world_geometry.md.
// Read-only. Static props (smodels), normals, tangents and vertex colours are not read; texture coordinates and
// lightmap UVs (StockWorld::lightmapUv, research 94) are.
//
// Texture coordinates (solved 2026-11-10). Each surface names its per-vertex streams through its GfxWorldSurfData
// record, the vertex data is the drawVerts.auxData stream of gfx_map_trzone (hex "bytes", 15,129,844 bytes on mp_shipment):
//   - gfx_map: surfaces.surfaces.values[i].surfDataIndex -> surfaces.surfData.values[k] (88 bytes, GfxWorldSurfData:
//     layerCount, xyzOffset, tangentFrameOffset, lmapCoordOffset, colorOffset, texCoordOffset, normalTransformOffset[7],
//     displacementOffset[8]). x64-zt: src/games/iw8/structs.hpp:11990 (GfxSurface, surfDataIndex), :12012 (GfxWorldSurfData),
//     :23500-23510 (offsets 0x8 xyz ... 0x18 texCoord, 0x1C normalTransform); neutral/gfxworld.cpp:2746 and :2814.
//   - xyzOffset is the posData byte offset of the surface's vertex block (equal to tris.posOffset for every surface).
//   - Vertex j of a surface (its surface-relative index) has its texture coordinates at
//     texCoordOffset + 8 * layerCount * j: layer 0 first, two IEEE float32 (u, v), then layer 1 ... Interleaved per
//     vertex. x64-zt src/convert/iw3_iw8/resources/replay_world_vertex.hlsl:91-93 reads the same way (Load2 at
//     textureOffset + id * layerCount * 8), and src/games/iw8/common/replay_render.cpp:1880-1892 writes the Replay form.
//   - lmapCoord, 8 bytes/vertex (float32 u, v), is read into StockWorld::lightmapUv (research 94 section 2).
//   - The other aux regions per surface (not read): tangentFrame 4 bytes/vertex, colour
//     4 bytes/vertex when colorOffset is set, normalTransform 4 bytes/vertex per layer above 0, displacement 4 bytes/vertex.
//     Together with the texCoord regions they tile the aux stream on mp_shipment with no overlap; 3,156 bytes are not in
//     any region (852 of them the displacement array of one surface, 2,304 in no surface record).
//   - Validation on the mp_shipment cache export (2026-11-10, independent script over the same bytes): coplanar adjacent triangle pairs in one vertex block whose uv
//     are consistent with one affine projection: interleaved 94.4 % (layerCount 1: 93.8 %, 2: 98.0 %, 3: 98.7 %);
//     the planar layout (layer blocks) 0.8 % to 1.1 %; random uv 0 %.
//   - ImportedMesh::uv is OBJ convention (v = 0 at the bottom, model_import.h:22). The game stores v with v = 0 at the top
//     (the writer flips with 1 - v, model_import.cpp:1144), so the reader stores u and 1 - v.
// unpackStockTexCoords decodes the PackedTexCoords word (two binary16 halves, x64-zt Vec2UnpackTexCoords). The world
// texture stream is float32, not this packing; the helper is kept for model vertices and is not used by the reader.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "geometry.h"
#include "model_import.h"

namespace iw8r {

struct StockWorld {
    // One mesh per surface material name (the material asset name, without the leading comma ACTS prints), in order
    // of first use. Positions are Z-up inches (the Radiant unit, no scale or axis change). Triangles are
    // counter-clockwise seen from outside, the ImportedModel convention; the ACTS data is clockwise (checked against
    // the floor surfaces of mp_shipment). Normals are area-weighted face normals. uv is layer 0 of the stock texture
    // coordinates, with v flipped to the ImportedMesh convention (see above); zero where the export has no aux stream or
    // no surfData for the surface (a warning names the count). Vertices are shared inside a mesh, so the count is the
    // unique (position, texture coordinate, lightmap UV) records.
    std::vector<ImportedMesh> meshes;
    // Lightmap UVs (docs/research/94_stock_lightmaps.md section 2), parallel to meshes: lightmapUv[m][k] belongs to
    // meshes[m].pos[k]. float32 (u, v) as stored in the aux stream at GfxWorldSurfData.lmapCoordOffset +
    // 8 * (firstVertex + j), normalised to the map's lightmap ([0, 1]); NO V flip: v = 0 is the first row of the
    // image (StockLightmap, stock_lightmap.h), texel = (u * width, v * height). (-1, -1) for a vertex without one:
    // its surface has lightmapIndex 510 (shadow casters, sky), its surfData has no lmapCoordOffset, or the transient
    // zone has no aux data. Only one lightmap per map is supported, so the surface's lightmapIndex is not kept.
    std::vector<std::vector<std::array<float, 2>>> lightmapUv;
    size_t lightmapSurfaces = 0;        // surfaces whose vertices got a lightmap UV
    AABB bounds;                        // over every vertex used by a triangle (includes sky geometry)
    std::vector<std::string> warnings;  // non-fatal: surfaces skipped, index range problems
};

// Parses the gfx_map JSON text and the gfx_map_trzone JSON texts (one per transient zone). Surfaces whose transient
// zone is not given are skipped with a warning. Fails when the JSON or the required fields are missing.
bool parseStockWorld(const std::string& gfxMapJson, const std::vector<std::string>& trzoneJson, StockWorld& out,
                     std::string* err = nullptr);

// Reads an ACTS export folder: <zoneDir>/assets/gfx_map/**/*.json (exactly one) and
// <zoneDir>/assets/gfx_map_trzone/**/*.json (one or more). zoneDir is <out>/mw19replay/<zone>. The streamed tiles a
// big map was given (exports of its _tr zones under <zoneDir>/streamed/**/gfx_map_trzone) are read too.
bool readStockWorld(const std::string& zoneDir, StockWorld& out, std::string* err = nullptr);

// The streamed world tiles of a big map (mp_br_quarry, mp_donetsk; research 87 section 1.2): a surface of the main
// zone names its transient zone, and every transient zone whose buffers the export does not hold is streamed from
// <map>_<NNNNN>_tr.ff (<map> = the folder name of zoneDir). For each: its index, that zone name (without .ff) and the
// bounds of the surfaces that use it (gfx_map surfaceBounds, one 56-byte record per surface: Bounds midPoint, halfSize
// first). Sorted by index.
struct StreamedWorldZone {
    int64_t index = 0;
    std::string name;
    AABB bounds;
    size_t surfaces = 0;
};
bool readStreamedWorldZones(const std::string& zoneDir, std::vector<StreamedWorldZone>& out, std::string* err = nullptr);

// The streamed zones whose surfaces come within `radius` of `centre` on X and Y, nearest first, at most `limit`.
std::vector<StreamedWorldZone> streamedZonesNear(const std::vector<StreamedWorldZone>& zones, double x, double y, double radius,
                                                 size_t limit);

// Decodes one PackedTexCoords word: u = IEEE binary16 in the low 16 bits, v = IEEE binary16 in the high 16 bits
// (x64-zt Vec2PackTexCoords / Vec2UnpackTexCoords). Zero, normal, subnormal, infinite and NaN halves are all handled
// by the IEEE rules. Not used for stock world surfaces (those are float32 pairs, see above).
void unpackStockTexCoords(uint32_t packed, double& u, double& v);

}  // namespace iw8r
