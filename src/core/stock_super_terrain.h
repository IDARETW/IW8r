// Stock super-terrain (Replay 1.20 `stterrain`, Verdansk mp_donetsk / Quarry mp_br_quarry): reads the StTerrain export
// and rebuilds the ground as plain triangle meshes from the per-surface height maps, for the viewport.
// Research: docs/research/102_stock_super_terrain.md (layout, height placement), 100 (clutter).
//
// Data (VERIFIED on the mp_donetsk export, 2026-11-13):
//   stterrain JSON  asset.fields.surfaces.values[s] (StDiskTerrainSurface): objToWld.origin (quat is identity on every
//                   surface seen; it is read and ignored), halfWldSize, surfaceMapSetIndex -> fields.maps.values[]
//                   {mapResolution, paddedResolution}, surfaceMapWorldSize (inches per height texel), heightOffset,
//                   heightScale, layerMaterials[] {material, stFromXYZ[2]}, heightMap / cutoutMap / combinedAlbedoMap /
//                   combinedNogMap (GfxImage names). The quadtree (`grid`, 4.5 M JSON nodes) is skipped, never parsed.
//   height map      GfxImage <map>_height_<s>: DDS DX10, DXGI 56 (R16_UNORM), paddedResolution^2 texels, 1 mip; only
//                   the first mapResolution^2 are used. Texel (i, j) (column i, row j; row 0 is the first row of the
//                   file) is at world origin.xy - halfWldSize + (i, j) * surfaceMapWorldSize, z = origin.z +
//                   heightOffset + H / 65535 * heightScale. No v flip: row 0 is the surface's -y edge.
//   cutout map      GfxImage <map>_cutout_<s>: DDS DX10, DXGI 42 (R32_UINT), one bit per height-map cell (the square
//                   between texels i..i+1, j..j+1): word (cx / 8, cy / 4), bit (cy % 4) * 8 + (cx % 8); 1 = the cell is
//                   drawn, 0 = cut out. Size ceil(cells / 8) + 1 by ceil(cells / 4) + 1 words (257 x 513 for 2048 cells).
//                   A surface whose cutoutMap is the builtin ",$white" has no cut-outs (cutoutImage is empty).
//                   The bit order and the meaning are INFERRED from the data, not from a shader: with this decode the
//                   27 Verdansk surfaces cover each point of their union exactly once (85.83 % of the sampled points
//                   once, 0.01 % twice on cell borders), and the rest are holes (14.2 %: sea, and the places where the
//                   world (gfx_map) geometry is the ground). The other bit orders give incoherent pictures.
//   combined albedo GfxImage <map>_combined_albedo_<s>: BC1_UNORM_SRGB (DXGI 72), 2048^2 on a 65,536-inch surface,
//                   1024^2 on 16,384 (12 / 11 mips, streamed). It is a baked colour of the whole surface (all layers).
//                   Same orientation as the height map (VERIFIED by eye and edge correlation on s1: roads and relief
//                   match; the flipped images do not); uv = (local + halfWldSize) / (2 halfWldSize) (texel-centre
//                   alignment INFERRED). This is the easiest way to texture the terrain in the viewport.
//   layer materials surfaces[s].layerMaterials[L].material (e.g. "mc/dirt_soil_01") with stFromXYZ[2] (float4 rows):
//                   s = row0.xyz . P + row0.w, t = row1.xyz . P + row1.w (game convention, v down). Layer 0 is
//                   1/128 per inch on most surfaces (one tile per 128 inches, t = -y / 128); some layers are rotated.
//                   P is INFERRED to be the world position (one world tiling is seamless across surfaces; the shader
//                   was not read). Which layer covers which cell (layer masks, flattenedImages) is NOT decoded.
//
// Mesh building: every surface is its height grid, sampled every `stride` texels (the last row and column are always
// included), two triangles per kept cell, counter-clockwise seen from +Z (the StockWorld / ImportedMesh convention;
// the game's own index lists are clockwise). At stride > 1 a coarse cell is kept when at least half of the fine cells
// it covers are present in the cutout map (exact at stride 1). Vertices are shared and only the ones a kept cell uses
// are emitted. Normals: central differences of the height field over +-stride texels (clamped at the edges).
// Surfaces overlap (the periphery ones surround the detailed ones); without the cutout map both are drawn.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "geometry.h"
#include "model_import.h"

namespace iw8r {

struct StockTerrainLayer {
    std::string material;          // Material asset name without the leading ',' ACTS prints
    double stFromXYZ[2][4] = {{0, 0, 0, 0}, {0, 0, 0, 0}};
    double textureScale[2] = {0, 0};  // layerData.textureScale (informational; equals the row lengths seen)
};

struct StockTerrainSurface {
    int index = 0;                 // row in StTerrain.surfaces
    Vec3 origin;                   // objToWld.origin (world inches)
    double halfWldSize = 0;        // the surface square is origin.xy +- halfWldSize
    double texelSize = 0;          // surfaceMapWorldSize: inches per height texel
    double heightOffset = 0, heightScale = 0;
    int mapResolution = 0;         // texels per side that span the surface (cells = mapResolution - 1)
    int paddedResolution = 0;      // texels per side in the image
    std::string heightImage;       // GfxImage names (no leading ','); "" when absent
    std::string cutoutImage;       // "" when absent or the builtin ",$white" (no cut-outs)
    std::string combinedAlbedoImage, combinedNogImage;
    std::vector<StockTerrainLayer> layers;  // layerMaterials
    AABB wldBounds;                // StDiskTerrainSurface.wldBounds (midPoint +- halfSize)

    int cells() const { return mapResolution > 1 ? mapResolution - 1 : 0; }
    // The world position of texel (i, j) without the height.
    double texelX(double i) const { return origin.x - halfWldSize + i * texelSize; }
    double texelY(double j) const { return origin.y - halfWldSize + j * texelSize; }
    double heightToZ(uint16_t h) const { return origin.z + heightOffset + double(h) / 65535.0 * heightScale; }
};

struct StockTerrain {
    std::string name;              // asset name (the map)
    std::vector<StockTerrainSurface> surfaces;
    std::vector<std::string> warnings;
};

// A decoded height map: row-major, data[j * width + i] is texel (i, j) (row 0 = the surface's -y edge).
struct StockTerrainHeightGrid {
    int width = 0, height = 0;
    std::vector<uint16_t> data;
    uint16_t at(int i, int j) const { return data[size_t(j) * size_t(width) + size_t(i)]; }
};

// A decoded cutout map (R32_UINT words, row-major).
struct StockTerrainCutout {
    int wordsX = 0, wordsY = 0;
    std::vector<uint32_t> words;
    // Cell (cx, cy) is drawn. Cells outside the words count as drawn.
    bool present(int cx, int cy) const {
        if (cx < 0 || cy < 0) return true;
        const int wx = cx >> 3, wy = cy >> 2;
        if (wx >= wordsX || wy >= wordsY) return true;
        return (words[size_t(wy) * size_t(wordsX) + size_t(wx)] >> ((cy & 3) * 8 + (cx & 7))) & 1u;
    }
};

// Parses the stterrain export (the whole JSON text, about 93 MB on Verdansk). Only the surface table, the maps table
// and the name are parsed; the quadtree is skipped by a string-aware brace scan, so memory stays near the text size.
// Surfaces without a heightMap are kept with heightImage "" and a warning. Fails when the JSON is malformed or has no
// surfaces array.
bool parseStockTerrain(const std::string& stterrainJson, StockTerrain& out, std::string* err = nullptr);

// Reads <path> when it is a file, else the one assets/stterrain/**/*.json under the directory (an ACTS zone export,
// <out>/mw19replay/<zone>).
bool readStockTerrain(const std::string& pathOrZoneDir, StockTerrain& out, std::string* err = nullptr);

// What the caller must export with `acts ... -a image -n <comma-separated names> --xpak <every zone/*.xpak over 64 KB>`
// (the heights and cut-outs are streamed from pak_magma1.xpak, the albedo from pak_magma1 / pak_tu21). Distinct names
// in surface order. heights: <map>_height_<s>; cutouts: <map>_cutout_<s>; albedo: <map>_combined_albedo_<s>.
enum StockTerrainImageKind : unsigned { kTerrainHeight = 1, kTerrainCutout = 2, kTerrainAlbedo = 4, kTerrainNog = 8 };
std::vector<std::string> stockTerrainImageNames(const StockTerrain& terrain, unsigned kinds = kTerrainHeight | kTerrainCutout);

// The DDS file ACTS wrote for image `name` in `imageDir` (<out>/mw19replay/<zone>/assets/image): "<name>.<n>.dds".
// "" when there is none.
std::string findStockTerrainImage(const std::string& imageDir, const std::string& name);

// DDS (DX10 header) decoders. Height: DXGI 56 R16_UNORM. Cutout: DXGI 42 R32_UINT. The first mip only.
bool decodeStockTerrainHeightDds(const std::string& ddsBytes, StockTerrainHeightGrid& out, std::string* err = nullptr);
bool decodeStockTerrainCutoutDds(const std::string& ddsBytes, StockTerrainCutout& out, std::string* err = nullptr);
bool readStockTerrainHeightDds(const std::string& path, StockTerrainHeightGrid& out, std::string* err = nullptr);
bool readStockTerrainCutoutDds(const std::string& path, StockTerrainCutout& out, std::string* err = nullptr);

// One surface's mesh.
struct StockTerrainMesh {
    int surface = -1;
    int stride = 1;
    // material: the surface's layer 0 material (the base ground; "" when there are no layers).
    // pos: world inches, Z up. normal: from the height field. uv: layer 0's planar texture coordinate
    // (s, 1 - t) from stFromXYZ applied to the world position, in the ImportedMesh convention (v = 0 at the bottom).
    // idx: counter-clockwise from +Z.
    ImportedMesh mesh;
    // The combined albedo coordinate of each vertex, ImportedMesh convention: u = (x - origin.x + half) / (2 half),
    // v = 1 - (y - origin.y + half) / (2 half). Parallel to mesh.pos.
    std::vector<std::array<double, 2>> albedoUv;
    std::string albedoImage;       // the surface's combinedAlbedoImage
    size_t cellsTotal = 0;         // coarse cells of the surface at this stride
    size_t cellsKept = 0;          // cells drawn (2 triangles each)
    AABB bounds;                   // over the emitted vertices
};

struct StockTerrainBuildOptions {
    int stride = 1;                // texels per grid step (>= 1)
    bool applyCutout = true;       // leave out the cells the cutout map clears (when a cutout is given)
};

// Builds one surface. heights must be at least mapResolution x mapResolution; cutout may be null.
bool buildStockTerrainMesh(const StockTerrain& terrain, int surface, const StockTerrainHeightGrid& heights,
                           const StockTerrainCutout* cutout, const StockTerrainBuildOptions& opt, StockTerrainMesh& out,
                           std::string* err = nullptr);

// Builds every surface whose heights[s] is non-empty (heights and cutouts are indexed by surface; cutouts may be
// shorter or hold empty entries). Surfaces without heights are skipped with a warning.
struct StockTerrainMeshes {
    std::vector<StockTerrainMesh> meshes;
    AABB bounds;
    size_t vertices = 0, triangles = 0;
    std::vector<std::string> warnings;
};
bool buildStockTerrainMeshes(const StockTerrain& terrain, const std::vector<StockTerrainHeightGrid>& heights,
                             const std::vector<StockTerrainCutout>& cutouts, const StockTerrainBuildOptions& opt,
                             StockTerrainMeshes& out, std::string* err = nullptr);

// Grid samples per side of a surface with `cells` cells at `stride`: ceil(cells / stride) + 1.
int stockTerrainSamples(int cells, int stride);

// Triangles at `stride` over every surface with a height map: 2 * ceil(cells / stride)^2 each (no cut-outs, an upper
// bound). With cutouts (indexed by surface, may be empty), the exact count buildStockTerrainMeshes would emit.
size_t stockTerrainTriangleCount(const StockTerrain& terrain, int stride,
                                 const std::vector<StockTerrainCutout>* cutouts = nullptr);

// The smallest stride (1 .. maxStride) whose triangle count is <= maxTriangles; maxStride when none is.
int stockTerrainStrideForBudget(const StockTerrain& terrain, size_t maxTriangles,
                                const std::vector<StockTerrainCutout>* cutouts = nullptr, int maxStride = 256);

}  // namespace iw8r
