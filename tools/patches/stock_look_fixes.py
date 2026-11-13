import io


def patch(p, pairs):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    for a, b in pairs:
        a = a.replace('\n', nl); b = b.replace('\n', nl)
        assert s.count(a) == 1, (p, s.count(a), a[:90])
        s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


# ---------------- shader and renderer
patch('src/viewport/render.cpp', [
 ('''        float3 Nf = dot(i.n, i.n) > 0.5 ? normalize(i.n) : normalize(cross(ddx(i.w), ddy(i.w)));
        float3 N = Nf;
        if (batchFlags.x > 0.5 && dot(i.n, i.n) > 0.5) {
            float4 nm = nrm0.Sample(samp0, i.uv);''', '''        float3 Nf = dot(i.n, i.n) > 0.5 ? normalize(i.n) : normalize(cross(ddx(i.w), ddy(i.w)));
        // A normal from the screen derivatives has no reliable side (instanced props have none of their own): it faces
        // the viewer.
        if (dot(i.n, i.n) <= 0.5 && dot(Nf, eyeStart.xyz - i.w) < 0) Nf = -Nf;
        float3 N = Nf;
        if (batchFlags.x > 0.5 && dot(i.n, i.n) > 0.5) {
            float4 nm = nrm0.Sample(samp0, i.uv);'''),
 ('''        float3 indirect = gameSky.rgb * (0.6 + 0.4 * saturate(N.z));''', '''        // Without a lightmap texel (props, models, new brushes): on a stock map the game lights them from its light grid;
        // here from the reflection probes' blurriest level (luminance-normalised) at the map's mean baked light
        // (gameSky.rgb, gameSky.w = 1); otherwise the unbaked light.
        float3 indirect = gameSky.rgb * (0.6 + 0.4 * saturate(N.z));
        if (gameSky.w > 0.5 && probeCount.x > 0.5)
            indirect = stockProbeLight(i.w, N, 0.0) * dot(gameSky.rgb, float3(0.2126, 0.7152, 0.0722));'''),
 ('''                         game_.sky[0], game_.sky[1], game_.sky[2], 0};''', '''                         game_.sky[0], game_.sky[1], game_.sky[2], game_.skyIsStock ? 1.0f : 0.0f};'''),
 # instanced props: the stock formula (their vertex colours are an editor shade) when a stock map is shown
 ('''    ctx_->PSSetShader(texPsOverride_ ? texPsOverride_ : psTex_, nullptr, 0);
    ctx_->PSSetSamplers(0, 1, &sampler_);
    if (cbBatch_) {
        const float flags[8] = {0, 0, 0, 0, 0, 0, 0, 0};''', '''    ctx_->PSSetShader(texPsOverride_ ? texPsOverride_ : psTex_, nullptr, 0);
    ctx_->PSSetSamplers(0, 1, &sampler_);
    if (cbBatch_) {
        const float flags[8] = {0, 0, 0, 0, 0, 0, game_.skyIsStock ? 1.0f : 0.0f, 0};'''),
])
patch('src/viewport/render.h', [
 ('''        float sky[3] = {0.2f, 0.2f, 0.2f};''', '''        float sky[3] = {0.2f, 0.2f, 0.2f};
        bool skyIsStock = false;  // sky = the stock map's mean baked light: unlit surfaces take the probes at that level'''),
])

# ---------------- mean baked light of the stock lightmap
patch('src/viewport/game_lightmap.h', [
 ('''    // Per lightmap index: its rectangle in the layers (x, y, width, height, all 0..1); zero = not loaded.
    std::vector<std::array<float, 4>> rects;''', '''    // Per lightmap index: its rectangle in the layers (x, y, width, height, all 0..1); zero = not loaded.
    std::vector<std::array<float, 4>> rects;
    // The mean irradiance C over the lightmapped texels (Game Lighting lights props and models at this level).
    std::array<float, 3> meanIrradiance{0, 0, 0};'''),
])
patch('src/viewport/game_lightmap.cpp', [
 ('''    out->rects.assign(size_t(std::max(lm.index, 0)) + 1, {0, 0, 0, 0});
    out->rects[size_t(std::max(lm.index, 0))] = {0, 0, 1, 1};
    return out;''', '''    out->rects.assign(size_t(std::max(lm.index, 0)) + 1, {0, 0, 0, 0});
    out->rects[size_t(std::max(lm.index, 0))] = {0, 0, 1, 1};
    double sum[3] = {0, 0, 0};
    size_t lit = 0;
    for (size_t i = 0; i < n; ++i) {
        const float* c = &lm.irradiance[i * 3];
        if (c[0] + c[1] + c[2] <= 1e-4f) continue;  // padding and unused texels
        for (int k = 0; k < 3; ++k) sum[k] += c[k];
        ++lit;
    }
    if (lit)
        for (int k = 0; k < 3; ++k) out->meanIrradiance[size_t(k)] = float(sum[k] / double(lit));
    return out;'''),
 ('''        out->rects[size_t(p.l->index)] = {float(p.x) / W, float(p.y) / H, float(p.l->width) / W, float(p.l->height) / H};
    }
    return out;''', '''        out->rects[size_t(p.l->index)] = {float(p.x) / W, float(p.y) / H, float(p.l->width) / W, float(p.l->height) / H};
        const double texels = double(p.l->width) * double(p.l->height);
        for (int k = 0; k < 3; ++k) out->meanIrradiance[size_t(k)] += float(one->meanIrradiance[size_t(k)] * texels);
        weight += texels;
    }
    if (weight > 0)
        for (float& m : out->meanIrradiance) m = float(m / weight);
    return out;'''),
 ('''    struct Place { const StockLightmap* l; int x, y; };''', '''    struct Place { const StockLightmap* l; int x, y; };
    double weight = 0;'''),
])

# ---------------- camera: fixed exposure for stock maps; the mean baked light as the unlit level
patch('src/qtapp/camera_view.h', [
 ('''    std::shared_ptr<const app::StockLightmapLayers> pendingStockLm_;''', '''    std::shared_ptr<const app::StockLightmapLayers> pendingStockLm_;
    std::array<float, 3> stockMeanLight_{0, 0, 0};'''),
])
patch('src/qtapp/camera_view.cpp', [
 ('''        if (l && l->width > 0) renderer_.setStockLightmap(l->width, l->height, l->a.data(), l->b.data(), l->d.data(), l->rects);''',
  '''        if (l && l->width > 0) renderer_.setStockLightmap(l->width, l->height, l->a.data(), l->b.data(), l->d.data(), l->rects);
        stockMeanLight_ = l ? l->meanIrradiance : std::array<float, 3>{0, 0, 0};'''),
 ('''        const Vec3 sky = gameSkyOf(ctx_->doc);
        g.sky[0] = float(sky.x), g.sky[1] = float(sky.y), g.sky[2] = float(sky.z);
        if (exposure.autoExposure != gameMetering_) {''', '''        const Vec3 sky = gameSkyOf(ctx_->doc);
        g.sky[0] = float(sky.x), g.sky[1] = float(sky.y), g.sky[2] = float(sky.z);
        if (stock && stockMeanLight_[0] + stockMeanLight_[1] + stockMeanLight_[2] > 0) {
            for (int k = 0; k < 3; ++k) g.sky[k] = stockMeanLight_[size_t(k)];
            g.skyIsStock = true;
        }
        // Stock maps: the exposure their vision gives a mid-bright scene (its mid EV), held fixed. Metering the frame
        // counted the editor's own empty background and markers (the game meters its sky and world only), which drove
        // the exposure up and washed the world out, and it kept the view repainting.
        exposure.autoExposure = false;
        if (stock) exposure.ev = stock->exposure.midEv, exposure.exposureAdjust = stock->exposure.midAdjust + stock->exposure.exposureAdjust;
        if (exposure.autoExposure != gameMetering_) {'''),
])
print('ok')
