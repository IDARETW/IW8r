// IW8 Radiant - which passes draw a piece of the stock reference (header only: core tests use it).
//
// Editor-only markers (the amber and red boxes of stock props whose model has not arrived or failed) never affect
// game-like rendering: they are drawn on screen only, never in a probe or clean capture and never in the sun shadow
// map. Pieces that stand for real geometry are drawn in every pass.
#pragma once

#include <cstddef>
#include <vector>

namespace iw8r::app {

enum class ReferencePass { Screen, Capture, Shadow };

inline bool drawsReferencePiece(bool editorMarker, ReferencePass pass) { return !editorMarker || pass == ReferencePass::Screen; }

// Vertices a set of instanced meshes (tris, rows of 12 floats per instance, editorMarker) draws in a pass.
template <class Mesh>
size_t referenceVertices(const std::vector<Mesh>& meshes, ReferencePass pass) {
    size_t n = 0;
    for (const Mesh& m : meshes)
        if (drawsReferencePiece(m.editorMarker, pass)) n += m.tris.size() * (m.rows.size() / 12);
    return n;
}

}  // namespace iw8r::app
