// IW8 Radiant Suite - custom model import (Wavefront OBJ) and export to the x64-zt extraction layout.
// The file formats written here are the ones x64-zt's ReadSourceModel reads (see
// docs/research/22_custom_models.md and docs/research/15_iw8_extraction_contract.md section 3.6).
#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "geometry.h"

namespace iw8r {

// One material group of an imported model. Vertices are deduplicated per group, so pos, normal and uv have
// one entry per vertex and idx indexes them. Triangles keep the file's winding (counter-clockwise seen from
// outside, the OBJ and glTF convention). Coordinates are the file's own (no axis or scale change).
struct ImportedMesh {
    std::string material;                    // usemtl name ("" when the faces name none)
    std::vector<Vec3> pos, normal;           // per vertex
    std::vector<std::array<double, 2>> uv;   // per vertex, OBJ convention (v = 0 at the bottom)
    std::vector<uint32_t> idx;               // 3 per triangle
    // glTF COLOR_0 as stored (0..1, RGBA; empty when the primitive has none). Stock models keep the game's raw vertex
    // colour bytes here: RGB in sRGB (the game's vertex shader linearises it and multiplies the albedo of VERTCOL
    // materials), alpha the flag / foliage wave weight (research 133 sections 4 and 6).
    std::vector<std::array<float, 4>> color;
    // Stock models (ACTS GLB attribute _SELFVIS, research 105 section 4): per vertex the self visibility, xyz = the
    // bent normal (turned like the normals) times the visible amount (0..1), w = the cone value of the game's
    // polynomial. Empty when the primitive has none.
    std::vector<std::array<float, 4>> selfVis;
    uint32_t sourceFlags = 0;  // glTF primitive extras.sourceFlags (the XSurface flags ACTS copies; 0x40 = self visibility)
    // Lightmapped stock models (ACTS GLB attribute _LMAPUV, research 105 section 3.2): per vertex the lightmap u, v
    // (0..1) within the placement's lmapLookup rectangle. Empty when the primitive has none.
    std::vector<std::array<float, 2>> lmapUv;
    // The second UV set (glTF TEXCOORD_1; stock `2` families' SECONDARY_TEXCOORDS, research 136), as stored. Empty
    // when the primitive has none.
    std::vector<std::array<double, 2>> uv1;
};

struct ImportedModel {
    std::string name;                        // file stem, sanitized to [a-z0-9_-]
    std::vector<ImportedMesh> meshes;        // LOD 0 for glTF
    AABB bounds;                             // over every vertex of `meshes`, in file coordinates
    std::vector<std::string> warnings;       // non-fatal: generated normals, missing uv, skipped primitives
    // glTF only (empty for OBJ). lods[i] is LOD i+1: the geometry of the nodes named *_lod<i+1> (same name as
    // this model). Read-only for existing users: they only look at meshes.
    std::vector<ImportedModel> lods;
    // glTF only: the geometry of nodes named collision* or UCX_* (case-insensitive). Null when there is none.
    std::shared_ptr<const ImportedModel> collision;
};

// Parses v / vt / vn / f (polygons are fan-triangulated; negative indices are relative) and usemtl.
// Faces are grouped by material, one mesh per material in order of first use. Missing vn are generated
// as area-weighted averages of the adjacent faces. The mtllib files are read only for map_Kd hints.
bool importObj(const std::string& path, ImportedModel& out, std::string* err);

// By extension: .obj -> importObj, .glb / .gltf -> importGltf (4.19). Other extensions fail with an error.
bool importModelFile(const std::string& path, ImportedModel& out, std::string* err);
// Custom model file extensions, in lookup order (a name.obj wins over name.glb over name.gltf).
inline constexpr const char* kCustomModelExtensions[] = {".obj", ".glb", ".gltf"};

// Reads a glTF 2.0 file: .gltf (JSON with external .bin files or base64 data: URIs) or .glb (binary container).
// The format is detected from the file's first bytes. Only the default scene (or the root nodes when the file has
// no scenes) is read; node transforms (matrix or TRS, through the hierarchy) are applied. Coordinates stay in the
// file's own space: glTF is Y-up metres, and the exporter (writeExtractionModel) maps Y-up to Z-up and applies the
// model scale, the same as for OBJ. Only TRIANGLES primitives (mode 4 or absent) are imported; other modes are
// skipped with a warning. One ImportedMesh per material name (primitives sharing a name are merged; no material
// is ""). Missing NORMAL gets area-weighted normals; missing TEXCOORD_0 gets (0,0). V is flipped to the OBJ
// convention (v = 0 at the bottom). Nodes are split by name: *_lod1..*_lod5 go to lods, collision* / UCX_* go to
// collision, everything else (including *_lod0) is LOD 0. A child node without a name tag inherits its parent's.
bool importGltf(const std::string& path, ImportedModel& out, std::string* err);

struct ModelExportOptions {
    double scale = 1.0;                      // applied to positions (must be > 0)
    bool zUp = true;                         // input is Y-up (OBJ/glTF default); rotate to Radiant's Z-up
    std::vector<double> lodDistances = {0};  // 1..6 non-decreasing; each LOD repeats the same geometry for now
    std::string materialPrefix;              // prepended to each material name, e.g. "iw8r/"
};

// Writes, under extractedDir (the extraction's `extracted` folder):
//   xmodel/<name>.json          OAT-style table: type rigid, collLod 0 (LOD 0 collides), lods[].distance
//   xmodel/<name>.replay.json   geometry: schema 1, name, bone_count 1, lods[].surfaces[] (one per material)
//   materials/<prefix><mat>.json one grey material per surface material; an existing file is kept
// Triangles are written clockwise seen from outside (IW3/IW7 model convention, x64-zt iw3_fastfile.cpp
// lines ~3061 and ~5150). V is flipped to the D3D top-left origin. Tangents are generated from UVs.
// Fails when the model has more than 255 surfaces over all LODs, a surface over 65535 vertices or
// triangles, or a name outside [A-Za-z0-9_-].
bool writeExtractionModel(const ImportedModel& m, const std::string& extractedDir,
                          const ModelExportOptions& opt, std::string* err);

}  // namespace iw8r
