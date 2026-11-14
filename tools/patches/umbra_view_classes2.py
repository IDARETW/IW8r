import io

# Plan 2.2, second part: the renderer and the plumbing for the tome cull of lights, probes and decal collections.

ROOT = 'E:/IW8-Radiant/'


def read(p):
    return io.open(ROOT + p, encoding='utf-8', newline='').read()


def write(p, s):
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s)


def patch(p, a, b, count=1):
    s = read(p)
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == count, (p, s.count(a), a[:70])
    write(p, s.replace(a, b))


# ---- core: the light's row travels with its shadow record ----
patch('src/core/stock_lights.h', '''struct StockLightShadow {
    bool cast = false;''', '''struct StockLightShadow {
    int primaryIndex = -1;  // the light's row in primaryLights (the Umbra view's light bits are by this row)
    bool cast = false;''')
patch('src/qtapp/main_window.cpp', '''                        lightShadows->push_back(stockLightShadowCamera(l, g));  // research 106 section 4''',
      '''                        lightShadows->push_back(stockLightShadowCamera(l, g));  // research 106 section 4
                        lightShadows->back().primaryIndex = l.index;''')

# ---- scene.h: a decal volume batch knows its collection ----
patch('src/viewport/scene.h', '''    int decalOrder = 0;
''', '''    int decalOrder = 0;
    int decalCollection = -1;  // the volume's decal volume collection (the Umbra view culls by it), -1 unknown
''')
patch('src/qtapp/main_window.cpp', '''        rb.decalOrder = order;
        for (uint32_t i : b.idx) {''', '''        rb.decalOrder = order;
        rb.decalCollection = vol.collection;
        for (uint32_t i : b.idx) {''')

# ---- render.h ----
patch('src/viewport/render.h', '''    bool referenceTomeCull() const { return !tomeSurfBits_.empty() || !tomeModelBits_.empty(); }
    size_t tomeCulledProps() const { return tomeCulledProps_; }  // placements the tome hid in the last frame''',
      '''    bool referenceTomeCull() const { return !tomeSurfBits_.empty() || !tomeModelBits_.empty(); }
    size_t tomeCulledProps() const { return tomeCulledProps_; }  // placements the tome hid in the last frame
    // The same cull for the stock map's lamps (bits by primaryLights row), its reflection probe boxes (by probe
    // instance) and its decal volume batches (by decal volume collection): the game sets a light's, a probe's and a
    // collection's visibility bit from the same query. Empty = nothing hidden. The map-wide fallback probe stays.
    void setReferenceTomeClasses(std::vector<uint32_t> lightBits, std::vector<uint32_t> probeBits, std::vector<uint32_t> decalBits);
    void setReferenceDecalCollection(size_t batch, int collection);
    size_t tomeCulledLights() const { return tomeCulledLights_; }   // lamps in view the tome hid in the last frame
    size_t tomeCulledProbes() const { return tomeCulledProbes_; }   // probe boxes hidden now
    size_t tomeCulledDecals() const { return tomeCulledDecals_; }   // decal batches the tome hid in the last frame''')
patch('src/viewport/render.h', '''    size_t tomeCulledProps_ = 0;''', '''    size_t tomeCulledProps_ = 0;
    std::vector<uint32_t> tomeLightBits_, tomeProbeBits_, tomeDecalBits_;
    size_t tomeCulledLights_ = 0, tomeCulledProbes_ = 0, tomeCulledDecals_ = 0;
    std::vector<float> probeCb_;         // setStockProbes: the probe constants as built (no box hidden)
    std::vector<int> probeBoxInstance_;  // per packed box: its probe instance
    int probeFallbackBox_ = -1;          // the packed box of the map-wide fallback probe, -1 none
    void applyTomeProbes();''')
patch('src/viewport/render.h', '''        bool decal = false;  // a decal material's batch (research 98): drawn after every opaque batch and layer''',
      '''        bool decal = false;  // a decal material's batch (research 98): drawn after every opaque batch and layer
        int tomeCollection = -1;  // a decal volume batch's collection (setReferenceDecalCollection), -1 none''')

# ---- render.cpp ----
patch('src/viewport/render.cpp', '''void SceneRenderer::bindTomeVisibility() {''', '''namespace {
// A clear bit: the tome hides it. Past the bits, or a negative index: not hidden.
bool tomeBitClear(const std::vector<uint32_t>& bits, int i) {
    return i >= 0 && size_t(i >> 5) < bits.size() && ((bits[size_t(i >> 5)] >> (unsigned(i) & 31u)) & 1u) == 0u;
}
}  // namespace

void SceneRenderer::setReferenceTomeClasses(std::vector<uint32_t> lightBits, std::vector<uint32_t> probeBits, std::vector<uint32_t> decalBits) {
    tomeLightBits_ = std::move(lightBits);
    tomeDecalBits_ = std::move(decalBits);
    const bool probesChanged = probeBits != tomeProbeBits_;
    tomeProbeBits_ = std::move(probeBits);
    if (probesChanged) applyTomeProbes();
    if (tomeLightBits_.empty()) tomeCulledLights_ = 0;
    if (tomeDecalBits_.empty()) tomeCulledDecals_ = 0;
}

void SceneRenderer::setReferenceDecalCollection(size_t batch, int collection) {
    if (batch < refLayers_.size()) refLayers_[batch].tomeCollection = collection;
}

// The probe constants with the boxes the tome hides made empty (a half size far below zero: no point is inside, and
// the box is never the nearest one). The fallback box is kept: a pixel no visible box covers takes it, as in the game.
void SceneRenderer::applyTomeProbes() {
    tomeCulledProbes_ = 0;
    if (!cbProbes_ || probeCb_.empty()) return;
    std::vector<float> cb = probeCb_;
    if (!tomeProbeBits_.empty())
        for (size_t i = 0; i < probeBoxInstance_.size(); ++i) {
            if (int(i) == probeFallbackBox_ || !tomeBitClear(tomeProbeBits_, probeBoxInstance_[i])) continue;
            float* e = cb.data() + 12 + 20 * i;
            e[7] = e[11] = e[15] = -1.0e35f;
            ++tomeCulledProbes_;
        }
    ctx_->UpdateSubresource(cbProbes_, 0, nullptr, cb.data(), 0, 0);
}

void SceneRenderer::bindTomeVisibility() {''')
# Lights: the selection skips what the tome hides.
patch('src/viewport/render.cpp', '''    stockLightsOn_ = false;
    stockLightsDrawn_ = 0;
    if (!dev_) return;''', '''    stockLightsOn_ = false;
    stockLightsDrawn_ = 0;
    tomeCulledLights_ = 0;
    if (!dev_) return;''')
patch('src/viewport/render.cpp', '''        if (out) continue;
        const float dx = l.pos[0] - eye[0], dy = l.pos[1] - eye[1], dz = l.pos[2] - eye[2];
        stockLightOrder_.push_back({dx * dx + dy * dy + dz * dz, i});''', '''        if (out) continue;
        // Cull by tome: the game bins a light only when the camera's query returned it (research 110).
        if (!tomeLightBits_.empty() && i < stockLightShadows_.size() && tomeBitClear(tomeLightBits_, stockLightShadows_[i].primaryIndex)) {
            ++tomeCulledLights_;
            continue;
        }
        const float dx = l.pos[0] - eye[0], dy = l.pos[1] - eye[1], dz = l.pos[2] - eye[2];
        stockLightOrder_.push_back({dx * dx + dy * dy + dz * dz, i});''')
# Probes: keep the constants and the instance of each packed box.
patch('src/viewport/render.cpp', '''    safeRelease(probeGridSrv_), safeRelease(probeGridTex_);
    std::vector<float> cb(size_t(12 + 20 * kMaxProbeBoxes), 0.0f);
    const auto upload = [&] { ctx_->UpdateSubresource(cbProbes_, 0, nullptr, cb.data(), 0, 0); };''',
      '''    safeRelease(probeGridSrv_), safeRelease(probeGridTex_);
    std::vector<float> cb(size_t(12 + 20 * kMaxProbeBoxes), 0.0f);
    probeCb_.clear();
    probeBoxInstance_.clear();
    probeFallbackBox_ = -1;
    tomeCulledProbes_ = 0;
    const auto upload = [&] { ctx_->UpdateSubresource(cbProbes_, 0, nullptr, cb.data(), 0, 0); };''')
patch('src/viewport/render.cpp', '''        e[19] = std::max({b.feather[0], b.feather[1], b.feather[2], 1.0f});
        ++n;
    }
    cb[0] = perfOff("probes") ? 0.0f : float(n);  // benchmark switch: no probes''',
      '''        e[19] = std::max({b.feather[0], b.feather[1], b.feather[2], 1.0f});
        probeBoxInstance_.push_back(int(k));
        // The fallback box (priority -FLT_MAX, as large as the map): the last one. The Umbra view never hides it,
        // and the shader starts from its image where no box is left (probeGridMin.w).
        if (b.priority < -1.0e30f) probeFallbackBox_ = int(n), cb[7] = float(b.imageIndex);
        ++n;
    }
    cb[0] = perfOff("probes") ? 0.0f : float(n);  // benchmark switch: no probes''')
s = read('src/viewport/render.cpp')
nl = '\r\n' if '\r\n' in s else '\n'
a = ('''            safeRelease(probeGridTex_);
        }
    }
    upload();
}''').replace('\n', nl)
assert s.count(a) == 1, s.count(a)
s = s.replace(a, ('''            safeRelease(probeGridTex_);
        }
    }
    probeCb_ = cb;
    applyTomeProbes();  // uploads; hides what the Umbra view hides
}''').replace('\n', nl))
a = '''    float nearRem = -1e30;
    float nearSlice = 0;'''.replace('\n', nl)
assert s.count(a) == 1, s.count(a)
s = s.replace(a, '''    float nearRem = -1e30;
    float nearSlice = probeGridMin.w;  // the fallback probe's image (0 without one): where every box is hidden'''.replace('\n', nl))
# Decals: a batch whose collection the tome hides is not drawn.
a = '''            layerPassDraws_ = layerPassTris_ = 0;'''
assert s.count(a) == 1, s.count(a)
s = s.replace(a, '''            layerPassDraws_ = layerPassTris_ = 0;
            tomeCulledDecals_ = 0;''')
a = '''                const RefLayer& L = refLayers_[k];
                if (!L.ready || L.drawClass != 0 || refTexIds_[k] < 0 || size_t(refTexIds_[k]) >= textures_.size()) continue;
                if (k >= refTexExtraFrom_) {'''.replace('\n', nl)
assert s.count(a) == 1, s.count(a)
s = s.replace(a, '''                const RefLayer& L = refLayers_[k];
                if (!L.ready || L.drawClass != 0 || refTexIds_[k] < 0 || size_t(refTexIds_[k]) >= textures_.size()) continue;
                // Cull by tome: the game draws a decal volume collection only with its visibility bit (research 110).
                if (!tomeDecalBits_.empty() && tomeBitClear(tomeDecalBits_, L.tomeCollection)) {
                    ++tomeCulledDecals_;
                    continue;
                }
                if (k >= refTexExtraFrom_) {'''.replace('\n', nl))
write('src/viewport/render.cpp', s)

# ---- camera_view ----
patch('src/qtapp/camera_view.h', '''    void setTomeVisibility(std::vector<uint32_t> surfaceBits, std::vector<uint32_t> modelBits) {
        renderer_.setReferenceTomeVisibility(std::move(surfaceBits), std::move(modelBits));
        update();
    }''', '''    void setTomeVisibility(std::vector<uint32_t> surfaceBits, std::vector<uint32_t> modelBits, std::vector<uint32_t> lightBits = {},
                           std::vector<uint32_t> probeBits = {}, std::vector<uint32_t> decalBits = {}) {
        renderer_.setReferenceTomeVisibility(std::move(surfaceBits), std::move(modelBits));
        renderer_.setReferenceTomeClasses(std::move(lightBits), std::move(probeBits), std::move(decalBits));
        update();
    }
    size_t tomeCulledLights() const { return renderer_.tomeCulledLights(); }
    size_t tomeCulledProbes() const { return renderer_.tomeCulledProbes(); }
    size_t tomeCulledDecals() const { return renderer_.tomeCulledDecals(); }''')
patch('src/qtapp/camera_view.h', '''        float a = 1, b = 0, tint[3] = {1, 1, 1};
        int order = 0;
    };
    std::vector<RefDecalVolume> refTexVolume_;''', '''        float a = 1, b = 0, tint[3] = {1, 1, 1};
        int order = 0;
        int collection = -1;  // the decal volume collection (Umbra view), -1 none
    };
    std::vector<RefDecalVolume> refTexVolume_;''')
patch('src/qtapp/camera_view.cpp', '''            dv.on = b.decalVolume, dv.mask = b.decalMask, dv.a = b.decalA, dv.b = b.decalB, dv.order = b.decalOrder;
            for (int c = 0; c < 3; ++c) dv.tint[c] = b.decalTint[c];
            refTexVolume_.push_back(dv);
            batches.push_back(std::move(b.tris));''', '''            dv.on = b.decalVolume, dv.mask = b.decalMask, dv.a = b.decalA, dv.b = b.decalB, dv.order = b.decalOrder;
            dv.collection = b.decalVolume ? b.decalCollection : -1;
            for (int c = 0; c < 3; ++c) dv.tint[c] = b.decalTint[c];
            refTexVolume_.push_back(dv);
            batches.push_back(std::move(b.tris));''')
patch('src/qtapp/camera_view.cpp', '''                    for (int c = 0; c < 3; ++c) dv.tint[c] = b.decalTint[c];
                    refTexVolume_.push_back(dv);
                    refTexResolved_.push_back(false);''', '''                    for (int c = 0; c < 3; ++c) dv.tint[c] = b.decalTint[c];
                    dv.collection = b.terrainLayer ? -1 : b.decalCollection;
                    refTexVolume_.push_back(dv);
                    refTexResolved_.push_back(false);''')
patch('src/qtapp/camera_view.cpp', '''                    renderer_.setReferenceTexture(k, cid);
                    renderer_.setReferenceDecal(k, 1000 + dv.order, mid == checkerTex_ ? -1 : mid, p);
                    continue;''', '''                    renderer_.setReferenceTexture(k, cid);
                    renderer_.setReferenceDecal(k, 1000 + dv.order, mid == checkerTex_ ? -1 : mid, p);
                    renderer_.setReferenceDecalCollection(k, dv.collection);
                    continue;''')

# ---- main_window_umbra ----
patch('src/qtapp/main_window_umbra.cpp', '''                if (umbraCull_ && res->culls) camera_->setTomeVisibility(res->surfaceBits, res->modelBits);''',
      '''                if (umbraCull_ && res->culls)
                    camera_->setTomeVisibility(res->surfaceBits, res->modelBits, res->lightBits, res->probeBits, res->decalBits);''')
patch('src/qtapp/main_window_umbra.cpp', '''                umbraLast_.surfaceBits.clear(), umbraLast_.modelBits.clear(), umbraLast_.visibleCells.clear();''',
      '''                umbraLast_.surfaceBits.clear(), umbraLast_.modelBits.clear(), umbraLast_.visibleCells.clear();
                umbraLast_.lightBits.clear(), umbraLast_.probeBits.clear(), umbraLast_.decalBits.clear();''')
patch('src/qtapp/main_window_umbra.cpp', '''                    text = QString("Umbra: %1 / %2 surfaces, %3 / %4 models, %5 / %6 lights, %7 / %8 probes, %9 cells (%10 ms)%11")
                               .arg(v[kUmbraSurface]).arg(t[kUmbraSurface]).arg(v[kUmbraStaticModel]).arg(t[kUmbraStaticModel])
                               .arg(v[kUmbraPrimaryLight]).arg(t[kUmbraPrimaryLight]).arg(v[kUmbraReflectionProbe]).arg(t[kUmbraReflectionProbe])
                               .arg(res->cells).arg(int(std::lround(res->seconds * 1000)))
                               .arg(res->truncated ? " (query cut short)" : "");
                else
                    text = QString("Umbra: camera %1: nothing is culled here").arg(umbraStatusText(res->status));''',
      '''                    text = QString("Umbra: %1 / %2 surfaces, %3 / %4 models, %5 / %6 lights, %7 / %8 probes, %9 / %10 decal groups, %11 cells (%12 ms)%13")
                               .arg(v[kUmbraSurface]).arg(t[kUmbraSurface]).arg(v[kUmbraStaticModel]).arg(t[kUmbraStaticModel])
                               .arg(v[kUmbraPrimaryLight]).arg(t[kUmbraPrimaryLight]).arg(v[kUmbraReflectionProbe]).arg(t[kUmbraReflectionProbe])
                               .arg(v[kUmbraDecalCollection]).arg(t[kUmbraDecalCollection])
                               .arg(res->cells).arg(int(std::lround(res->seconds * 1000)))
                               .arg(res->truncated ? " (query cut short)" : "");
                else if (res->status == UmbraQueryStatus::OutsideTome || res->status == UmbraQueryStatus::NoTile)
                    // Research 121: the game's query fails here; it draws the whole static map and no moving object.
                    text = QString("Umbra: camera %1: the game draws the whole map from here, and no players or vehicles")
                               .arg(umbraStatusText(res->status));
                else
                    text = QString("Umbra: camera %1: nothing is culled here").arg(umbraStatusText(res->status));''')
print('ok')
