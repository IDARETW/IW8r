import io

# Game Lighting shows what the game shows: the editor's dark brush outlines on textured faces are off there by default
# (owner 2026-11-13: lines at the slices of wall geometry; they were these outlines, visible at every brush seam).
# View > Game Lighting Options > Brush Outlines brings them back; selected brushes always keep theirs.
# Core face_edges: a fold under 2 degrees between brushes is a seam.


def patch(p, a, b, n=1):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == n, (p, s.count(a), a[:80])
    s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


patch('E:/IW8-Radiant/src/viewport/scene.h', '''    bool gameLook = false;''', '''    // Game Lighting: the editor's dark outlines of unselected textured brush faces (off: the view shows the surfaces
    // as the game does; View > Game Lighting Options > Brush Outlines).
    bool gameOutlines = false;
    bool gameLook = false;''')
patch('E:/IW8-Radiant/src/viewport/camera_geometry.cpp', '''        if (!g->edges.empty() || (g->meshPoly(i) && !sel && !wire)) continue;
        const uint32_t ec = sel ? kSelEdge : (wire ? kEdgeWire : kEdgeDark);''', '''        if (!g->edges.empty() || (g->meshPoly(i) && !sel && !wire)) continue;
        // Game Lighting: a textured face is drawn as the game draws it, without the editor's outline (seams between
        // the brushes of one wall would show as lines).
        if (!sel && !wire && texId >= 0 && sc.gameLookActive() && !sc.gameOutlines) continue;
        const uint32_t ec = sel ? kSelEdge : (wire ? kEdgeWire : kEdgeDark);''')
patch('E:/IW8-Radiant/src/qtapp/main_window.cpp', '''        toggle("GamePostFx", "Bloom and Vignette", "game/postFx", [this](bool on) { camera_->setPostFxEnabled(on); });''',
      '''        toggle("GamePostFx", "Bloom and Vignette", "game/postFx", [this](bool on) { camera_->setPostFxEnabled(on); });
        // Off by default: the game draws no brush outlines, and they show every seam between the brushes of a wall.
        toggle("GameBrushOutlines", "Brush Outlines (editor only)", "game/brushOutlines", [this](bool on) {
            ctx_.scene.gameOutlines = on;
            camera_->materialImagesChanged();  // the kept geometry is made again
        }, false);''')
patch('E:/IW8-Radiant/src/qtapp/camera_view.cpp', '''    mix(ctx_->scene.tomeLinesVersion * 7 + 5);''', '''    mix(ctx_->scene.tomeLinesVersion * 7 + 5);
    mix(ctx_->scene.gameOutlines ? 11 : 13);''')
patch('E:/IW8-Radiant/src/core/face_edges.cpp', '''                double fold = 0;
                if (cosang < 0.99999) fold = dot(nF, outward) > 0 ? std::acos(std::clamp(cosang, -1.0, 1.0)) : 0.0;''',
      '''                // Within 2 degrees of the plane the surface simply carries on (a seam); a larger convex bend is a soft fold.
                double fold = 0;
                if (cosang < 0.9994) fold = dot(nF, outward) > 0 ? std::acos(std::clamp(cosang, -1.0, 1.0)) : 0.0;''')
print('ok')
