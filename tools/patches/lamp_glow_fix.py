import io

# The lamp glow of the viewport's volumetric air (iw8r_volumetric) never reached the renderer: prepareGameLook read
# the frame's pixel light list right after it had been emptied and before it was filled. A light is now turned into
# its renderer record by one function, used by both.

ROOT = 'E:/IW8-Radiant/'


def patch(p, a, b, count=1):
    s = io.open(ROOT + p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == count, (p, s.count(a), a[:70])
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s.replace(a, b))


H = 'src/qtapp/camera_view.h'
patch(H, '''    bool gameLookOn() const;
    void prepareGameLook();''', '''    bool gameLookOn() const;
    void prepareGameLook();
    // A light entity as the renderer's record: position, colour, cone; with `game` in the converter's light units (the
    // reference distance is radius / 2, or a spot's axis to the first surface).
    app::SceneRenderer::PixelLight pixelLightOf(const Scene::PreviewLight& L, bool game) const;''')

C = 'src/qtapp/camera_view.cpp'
patch(C, '''            for (const auto& L : ctx_->scene.lights) {
                if (pl.size() >= size_t(app::SceneRenderer::kMaxPixelLights)) break;
                app::SceneRenderer::PixelLight p{};
                p.pos[0] = float(L.pos.x), p.pos[1] = float(L.pos.y), p.pos[2] = float(L.pos.z), p.radius = float(L.radius);
                p.color[0] = float(L.color.x), p.color[1] = float(L.color.y), p.color[2] = float(L.color.z);
                p.cosOuter = float(L.cosOuter), p.cosInner = float(L.cosInner);
                p.dir[0] = float(L.dir.x), p.dir[1] = float(L.dir.y), p.dir[2] = float(L.dir.z);
                if (game) {
                    // The converter's light units (core game_look): the reference is radius / 2, or a spot's axis to the
                    // first surface (convert.cpp traces the render triangles; here the collision world).
                    double ref = L.radius * 0.5;
                    if (L.cosOuter > -1.5)
                        if (const CollisionWorld* cw = ctx_->scene.shadowWorld(ctx_->doc)) {
                            const double f = cw->traceRay(L.pos, L.pos + L.dir * ref).fraction;
                            if (f < 1.0) ref = std::max(1.0, f * ref);
                        }
                    const double gi = gameLightIntensity(L.radius, ref);
                    for (float& c : p.color) c = float(c * gi);
                }
                pl.push_back(p);
            }''', '''            for (const auto& L : ctx_->scene.lights) {
                if (pl.size() >= size_t(app::SceneRenderer::kMaxPixelLights)) break;
                pl.push_back(pixelLightOf(L, game));
            }''')
patch(C, '''bool CameraView::gameLookOn() const {''', '''app::SceneRenderer::PixelLight CameraView::pixelLightOf(const Scene::PreviewLight& L, bool game) const {
    app::SceneRenderer::PixelLight p{};
    p.pos[0] = float(L.pos.x), p.pos[1] = float(L.pos.y), p.pos[2] = float(L.pos.z), p.radius = float(L.radius);
    p.color[0] = float(L.color.x), p.color[1] = float(L.color.y), p.color[2] = float(L.color.z);
    p.cosOuter = float(L.cosOuter), p.cosInner = float(L.cosInner);
    p.dir[0] = float(L.dir.x), p.dir[1] = float(L.dir.y), p.dir[2] = float(L.dir.z);
    if (game) {
        // The converter's light units (core game_look): the reference is radius / 2, or a spot's axis to the first
        // surface (convert.cpp traces the render triangles; here the collision world).
        double ref = L.radius * 0.5;
        if (L.cosOuter > -1.5)
            if (const CollisionWorld* cw = ctx_->scene.shadowWorld(ctx_->doc)) {
                const double f = cw->traceRay(L.pos, L.pos + L.dir * ref).fraction;
                if (f < 1.0) ref = std::max(1.0, f * ref);
            }
        const double gi = gameLightIntensity(L.radius, ref);
        for (float& c : p.color) c = float(c * gi);
    }
    return p;
}

bool CameraView::gameLookOn() const {''')
patch(C, '''                std::vector<std::pair<double, size_t>> order;
                const auto& pl = pixelLightScratch_;
                for (size_t k = 0; k < ctx_->scene.lights.size() && k < pl.size(); ++k)
                    if (ctx_->scene.lights[k].volumetric) {''', '''                // (Straight from the light entities: this runs before the frame's per-pixel light list is filled.)
                std::vector<std::pair<double, size_t>> order;
                for (size_t k = 0; k < ctx_->scene.lights.size(); ++k)
                    if (ctx_->scene.lights[k].volumetric) {''')
patch(C, '''                for (int i = 0; i < g.volLampCount; ++i) {
                    const auto& l = pl[order[size_t(i)].second];
                    float* r = g.volLamps[i];''', '''                for (int i = 0; i < g.volLampCount; ++i) {
                    const app::SceneRenderer::PixelLight l = pixelLightOf(ctx_->scene.lights[order[size_t(i)].second], true);
                    float* r = g.volLamps[i];''')
print('ok')
