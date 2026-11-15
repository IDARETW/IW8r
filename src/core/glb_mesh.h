// Minimal glTF 2.0 binary (.glb) reader: JSON chunk + BIN chunk, meshes and their primitives (POSITION, NORMAL,
// TEXCOORD_0, indices of u8/u16/u32), node transforms (matrix or TRS, parent chains). Enough for the ACTS
// xmodelsurfs sidecar (docs/research/23_model_preview_cache.md). Triangles only; read-only.
#pragma once

#include <array>
#include <cstdint>
#include <string>

#include "model_import.h"

namespace iw8r {

struct GlbReadOptions {
    // Apply the node transforms (glTF default). ACTS puts a -90 degree X rotation and a 0.0254 scale on its node,
    // so the default result is glTF Y-up metres. false returns the accessor data as stored (ACTS: Z-up inches).
    bool applyNodeTransforms = true;
    // Multiplies every position after the node transforms. 1/0.0254 turns the ACTS output into inches.
    double scale = 1.0;
};

// Reads a .glb file. out.name is the file stem; out.bounds covers every vertex in output units.
// One ImportedMesh per primitive (material ""). Primitives other than triangles are skipped with a warning; a
// missing NORMAL becomes (0,0,1) and a missing TEXCOORD_0 becomes (0,0), both with a warning. A node whose
// transform mirrors (negative determinant) reverses its triangles so they still face outward.
bool readGlb(const std::string& path, ImportedModel& out, std::string* err = nullptr, const GlbReadOptions& opt = {});

// The static-model self-visibility word (XModelSurfs vertex +8; Replay's static-model VS, research 105 section 4):
// octahedral direction in bits 0-8 / 9-17 (folded when z < 0), amount in 18-24, cone in 25-31. Returns the unit
// direction times the amount (xyz) and the cone (w).
std::array<float, 4> decodeSelfVisibility(uint32_t word);

// Same as readGlb for bytes already in memory (out.name is left as the caller set it).
bool parseGlb(const std::string& bytes, ImportedModel& out, std::string* err = nullptr, const GlbReadOptions& opt = {});

}  // namespace iw8r
