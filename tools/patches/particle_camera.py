import io

# Research 138, the camera side of the effect preview: placement angles, the particle materials (sprite image with its
# own alpha, the emissive image of unlit sets, atlas size from constant 94, lit / unlit / skipped class), quads by
# element (billboard, tail, oriented sprite) with atlas cells and mirroring, a global far-to-near sort split into
# batches per image, handed to SceneRenderer::setParticles. The material cache keeps constant 94 (constRules 8).
# Bench: IW8R_BENCH_FX=<seconds> turns View > Effect Preview on at that effect time.

ROOT = 'E:/IW8-Radiant/'


def patch(p, a, b, count=1):
    s = io.open(ROOT + p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    if s.count(a) == 0 and b in s:
        return
    assert s.count(a) == count, (p, s.count(a), a[:70])
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s.replace(a, b))


CH = 'src/qtapp/camera_view.h'
patch(CH, '''    struct StockEffect {
        Vec3 origin;
        std::string name;
    };''', '''    struct StockEffect {
        Vec3 origin;
        std::string name;
        Vec3 angles;  // pitch, yaw, roll of the placement (the effect's axes)
    };
    // An effect material as the particle preview needs it (research 138 section 6): ready = the material cache has it.
    struct ParticleMaterial {
        bool ready = false;
        bool skip = false;          // distortion and flare sets: not drawn
        bool lit = true;            // lit sets (albedo, alpha blend); unlit sets add their emissive image
        std::string colorFile, alphaFile, emissiveFile;
        int cols = 1, rows = 1;     // the atlas (constant 94)
        float emissive[3] = {1, 1, 1};  // constant 36 of an unlit set
    };
    void setParticleMaterials(std::function<ParticleMaterial(const std::string&)> fn) { particleMaterial_ = std::move(fn); }''')
patch(CH, '''    size_t fxParticles_ = 0;''', '''    size_t fxParticles_ = 0;
    std::function<ParticleMaterial(const std::string&)> particleMaterial_;
    std::unordered_map<std::string, ParticleMaterial> particleMats_;  // ready ones, by material name
    int particleTexture(const std::string& material, const ParticleMaterial& pm);  // -1 while loading''')

CC = 'src/qtapp/camera_view.cpp'
patch(CC, '''    if (ctx_->effectPreview && ctx_->texturedView && effectSource_ && !cleanCapture_) {
        appendEffectParticles();
        if (!fxBatch_.v.empty()) extraBatches_.push_back(&fxBatch_);
    } else {
        fxParticles_ = 0;
    }''', '''    if (ctx_->effectPreview && ctx_->texturedView && effectSource_ && !cleanCapture_) {
        appendEffectParticles();  // their own pass (SceneRenderer::setParticles)
    } else {
        fxParticles_ = 0;
        renderer_.setParticles({});
    }''')
# The particle builder.
s = io.open(ROOT + CC, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'
i = s.find('void CameraView::appendEffectParticles() {')
j = s.find('void CameraView::scanProbes() {', i)
assert i > 0 and j > i
new_fn = r'''int CameraView::particleTexture(const std::string& material, const ParticleMaterial& pm) {
    // "\x08<material>": the sprite as the effect draws it. Lit: the colour image with its opacity (slot 27's alpha when
    // it varies, else its red; without one the colour image's own alpha). Unlit: the emissive image, opaque.
    const std::string key = "\x08" + material;
    if (const auto it = texIds_.find(key); it != texIds_.end()) return it->second;
    const std::string& main = pm.lit ? pm.colorFile : (pm.emissiveFile.empty() ? pm.colorFile : pm.emissiveFile);
    if (main.empty()) return texIds_[key] = -1;
    DecodedTexture a, m;
    if (!decodedTexture(main, kViewTextureSide, 1, a)) {
        moreToLoad_ = true;
        return -1;
    }
    const bool withMask = pm.lit && !pm.alphaFile.empty();
    if (withMask && !decodedTexture(pm.alphaFile, kViewTextureSide, 1, m)) {
        moreToLoad_ = true;
        return -1;
    }
    if (!a.imageOk || a.image.width <= 0 || a.image.height <= 0) return texIds_[key] = -1;
    app::Rgba8Image& img = a.image;
    if (withMask && m.imageOk && m.image.width > 0 && m.image.height > 0) {
        int ch = 0;  // red, unless the alpha varies
        for (size_t k = 3; k < m.image.pixels.size(); k += 4)
            if (m.image.pixels[k] < 250) {
                ch = 3;
                break;
            }
        for (int y = 0; y < img.height; ++y)
            for (int x = 0; x < img.width; ++x) {
                const int mx = int(int64_t(x) * m.image.width / img.width), my = int(int64_t(y) * m.image.height / img.height);
                img.pixels[(size_t(y) * size_t(img.width) + size_t(x)) * 4 + 3] = m.image.pixels[(size_t(my) * size_t(m.image.width) + size_t(mx)) * 4 + size_t(ch)];
            }
    } else if (!pm.lit) {
        for (size_t k = 3; k < img.pixels.size(); k += 4) img.pixels[k] = 255;
    }
    const int id = renderer_.createTexture(img.pixels.data(), img.width, img.height);
    return texIds_[key] = id;
}

void CameraView::appendEffectParticles() {
    // Research 138: every placed effect's particles as world quads, sorted far to near, one batch per run of a sprite.
    if (whiteTex_ < 0) {
        const uint8_t px[4] = {255, 255, 255, 255};
        whiteTex_ = renderer_.createTexture(px, 1, 1);
    }
    const double yaw = ctx_->cam.yaw * 3.14159265358979 / 180.0, pitch = ctx_->cam.pitch * 3.14159265358979 / 180.0;
    const Vec3 fwd{std::cos(pitch) * std::cos(yaw), std::cos(pitch) * std::sin(yaw), std::sin(pitch)};
    const Vec3 right = normalize(cross(fwd, Vec3{0, 0, 1}));
    const Vec3 up = normalize(cross(right, fwd));
    if (!fxClock_.isValid()) fxClock_.start();
    const double t = fxTimeOverride_ >= 0 ? fxTimeOverride_ : fxClock_.elapsed() / 1000.0;
    fxParticles_ = 0;
    uint32_t seed = 1;
    struct Placed {
        Vec3 origin, angles;
        const std::string* fx;
    };
    // The mapper's fx_origin entities, then the stock map's own placed effects.
    std::vector<Placed> placed;
    for (const Entity& e : ctx_->doc.map().entities) {
        if (e.classname() != "fx_origin") continue;
        const std::string* fx = e.value("fx");
        const auto o = e.origin();
        Vec3 ang{0, 0, 0};
        if (const std::string* a = e.value("angles")) std::sscanf(a->c_str(), "%lf %lf %lf", &ang.x, &ang.y, &ang.z);
        if (fx && o) placed.push_back({*o, ang, fx});
    }
    for (const StockEffect& s : stockEffects_) placed.push_back({s.origin, s.angles, &s.name});
    const Vec3 eye = ctx_->cam.pos;
    struct Quad {
        float depth;
        int texture;
        bool lit;
        float emissive[3];
        app::ParticleVertex v[6];
    };
    std::vector<Quad> quads;
    const auto materialOf = [&](const std::string& name) -> const ParticleMaterial* {
        if (name.empty() || !particleMaterial_) return nullptr;
        if (const auto it = particleMats_.find(name); it != particleMats_.end()) return &it->second;
        ParticleMaterial pm = particleMaterial_(name);
        if (!pm.ready) {
            moreToLoad_ = true;  // asked again next frame
            return nullptr;
        }
        return &(particleMats_[name] = pm);
    };
    for (const auto& pl : placed) {
        ++seed;
        // Far effects are left out (the game culls them by distance too); the 20,000 particle budget goes to near ones.
        const double dist = length(pl.origin - eye);
        if (dist > 12000) continue;
        const auto def = effectSource_(*pl.fx);
        if (!def || def->emitters.empty()) continue;
        const double loop = def->duration > 0 ? def->duration + 0.5 : 60.0;  // one-shots replay after a short pause
        const VfxAxes ax = vfxAxesFromAngles(pl.angles.x, pl.angles.y, pl.angles.z);
        for (const VfxParticle& p : sampleVfx(*def, std::fmod(t, loop), seed, &ax)) {
            const VfxEmitterSim& em = def->emitters[size_t(p.emitter)];
            if (em.fadeOutMaxDistance > 0 && dist > em.fadeOutMaxDistance) continue;
            if (em.element != 0 && em.element != 8 && em.element != 10) continue;  // flares, models, lights, runners, decals
            const std::string& matName = size_t(p.material) < em.materials.size() ? em.materials[size_t(p.material)] : em.material;
            const ParticleMaterial* pm = materialOf(matName);
            if (pm && pm->skip) continue;
            int tex = pm ? particleTexture(matName, *pm) : -1;
            const bool lit = pm ? pm->lit : true;
            int cols = pm ? std::max(1, pm->cols) : 1, rows = pm ? std::max(1, pm->rows) : 1;
            if (tex < 0) tex = whiteTex_, cols = rows = 1;  // not loaded (yet): the particle's colour on a plain quad
            const Vec3 c = pl.origin + Vec3{p.pos[0], p.pos[1], p.pos[2]};
            const double w = std::max(0.25, p.size2[0] * 0.5), h = std::max(0.25, p.size2[1] * 0.5);
            Vec3 ax0, ax1;  // half extents
            if (em.element == 10) {
                // A tail: long along the velocity seen from the camera, behind the particle.
                const Vec3 vel{p.vel[0], p.vel[1], p.vel[2]};
                const double speed = length(vel);
                Vec3 dir = speed > 1e-6 ? vel - fwd * dot(vel, fwd) : up;
                dir = length(dir) > 1e-6 ? normalize(dir) : up;
                const Vec3 side = normalize(cross(dir, fwd));
                const double len = std::max(h, em.tailScaleWithVelocity ? speed * 0.033 * 0.5 : h);
                ax0 = side * w, ax1 = dir * len;
            } else if (em.element == 8) {
                // An oriented sprite: the quaternion's y and z axes in the effect's frame (INFERRED facing).
                const double* q = em.orientQuat;
                const auto rot = [&](const double v[3], double o[3]) {
                    const double qx = q[0], qy = q[1], qz = q[2], qw = q[3];
                    const double tx = 2 * (qy * v[2] - qz * v[1]), ty = 2 * (qz * v[0] - qx * v[2]), tz = 2 * (qx * v[1] - qy * v[0]);
                    o[0] = v[0] + qw * tx + (qy * tz - qz * ty), o[1] = v[1] + qw * ty + (qz * tx - qx * tz), o[2] = v[2] + qw * tz + (qx * ty - qy * tx);
                };
                const double ly[3] = {0, 1, 0}, lz[3] = {0, 0, 1};
                double ry[3], rz[3];
                rot(ly, ry), rot(lz, rz);
                const auto world = [&](const double l[3]) {
                    return Vec3{ax.x[0] * l[0] + ax.y[0] * l[1] + ax.z[0] * l[2], ax.x[1] * l[0] + ax.y[1] * l[1] + ax.z[1] * l[2],
                                ax.x[2] * l[0] + ax.y[2] * l[1] + ax.z[2] * l[2]};
                };
                const double ca = std::cos(p.angle), sa = std::sin(p.angle);
                const Vec3 a0 = world(ry), a1 = world(rz);
                ax0 = (a0 * ca + a1 * sa) * w, ax1 = (a1 * ca - a0 * sa) * h;
            } else {
                // A billboard, turned by the particle's angle.
                const double ca = std::cos(p.angle), sa = std::sin(p.angle);
                ax0 = (right * ca + up * sa) * w, ax1 = (up * ca - right * sa) * h;
            }
            // The atlas cell, mirrored as the particle says.
            const int frames = cols * rows;
            const int f = vfxAtlasFrame(em, p, frames);
            const int col = f % cols, row = f / cols;
            float u0 = float(col) / float(cols), u1 = float(col + 1) / float(cols);
            float v0 = float(row) / float(rows), v1 = float(row + 1) / float(rows);
            if (p.flipU) std::swap(u0, u1);
            if (p.flipV) std::swap(v0, v1);
            Quad qd;
            qd.depth = float(dot(c - eye, fwd));
            qd.texture = tex;
            qd.lit = lit;
            for (int k = 0; k < 3; ++k) qd.emissive[k] = pm ? pm->emissive[k] : 1.0f;
            const auto corner = [&](double sx, double sy, float u, float v) {
                const Vec3 q = c + ax0 * sx + ax1 * sy;
                return app::ParticleVertex{float(q.x), float(q.y), float(q.z), u, v, p.rgba[0], p.rgba[1], p.rgba[2], p.rgba[3]};
            };
            const app::ParticleVertex a = corner(-1, -1, u0, v1), b = corner(1, -1, u1, v1), cc = corner(1, 1, u1, v0), d = corner(-1, 1, u0, v0);
            qd.v[0] = a, qd.v[1] = b, qd.v[2] = cc, qd.v[3] = a, qd.v[4] = cc, qd.v[5] = d;
            quads.push_back(qd);
            if (++fxParticles_ >= 20000) break;
        }
        if (fxParticles_ >= 20000) break;
    }
    // Far to near, then runs of one sprite and kind become one batch (the premultiplied blend keeps the order right).
    std::stable_sort(quads.begin(), quads.end(), [](const Quad& x, const Quad& y) { return x.depth > y.depth; });
    std::vector<app::ParticleBatch> batches;
    for (const Quad& q : quads) {
        if (batches.empty() || batches.back().texture != q.texture || batches.back().lit != q.lit) {
            batches.emplace_back();
            batches.back().texture = q.texture, batches.back().lit = q.lit;
            std::copy(q.emissive, q.emissive + 3, batches.back().emissive);
        }
        batches.back().v.insert(batches.back().v.end(), q.v, q.v + 6);
    }
    renderer_.setParticles(std::move(batches));
}

'''
s = s[:i] + new_fn.replace('\n', nl) + s[j:]
io.open(ROOT + CC, 'w', encoding='utf-8', newline='').write(s)

MW = 'src/qtapp/main_window.cpp'
patch(MW, '''                fx.push_back({e.origin, e.effect});''', '''                fx.push_back({e.origin, e.effect, e.angles});''')
patch(MW, '''    camera_->setProbeFolder([this] {''', '''    // Research 138 section 6: an effect material for the particle preview, from the material cache (asked for when
    // missing). Distortion and flare sets are skipped; the 13th technique token ends in 23 on unlit sets (they add
    // their emissive image) and in b on lit ones; constant 94 = (columns, rows, ...) of the atlas.
    camera_->setParticleMaterials([this](const std::string& m) {
        CameraView::ParticleMaterial pm;
        const auto e = matCache_->lookup(qs(m));
        if (!e) {
            matCache_->request({qs(m)});
            return pm;
        }
        pm.ready = true;
        if (e->failed) return pm;
        const QString ts = e->techset.section('/', -1);
        pm.skip = ts.startsWith("dist_") || ts.contains("distortion") || qs(m).startsWith("ecf/");
        const QStringList tok = ts.split('_');
        if (tok.size() > 12) pm.lit = !tok[12].endsWith("23");
        if (e->colorDds.isEmpty() && !e->emissiveDds.isEmpty()) pm.lit = false;
        pm.colorFile = e->colorDds.toStdString();
        pm.alphaFile = e->maskDds.toStdString();
        pm.emissiveFile = e->emissiveDds.toStdString();
        if (const auto c = e->consts.find(94); c != e->consts.end() && c->second[0] >= 1 && c->second[1] >= 1)
            pm.cols = int(std::lround(c->second[0])), pm.rows = int(std::lround(c->second[1]));
        if (const auto c = e->consts.find(36); c != e->consts.end())
            for (int k = 0; k < 3; ++k) pm.emissive[k] = std::max(0.0f, c->second[size_t(k)]);
        return pm;
    });
    camera_->setProbeFolder([this] {''')

MC = 'src/qtapp/material_cache.cpp'
patch(MC, '''        if (finite && (index == 18 || index == 36 || index == 45 || index == 63 || (index >= 79 && index <= 82) || index == 103 ||
                       index == 104 || index == 110 || index == 111))''',
      '''        if (finite && (index == 18 || index == 36 || index == 45 || index == 63 || (index >= 79 && index <= 82) || index == 94 ||
                       index == 103 || index == 104 || index == 110 || index == 111))''')
patch(MC, '''            o.insert("constRules", 7);  // 1: the lit buffer, canonical rows, constants 18 / 36 / 103 / 104, slot 68; 2: bits order; 3: + 63; 4: + 79-82; 5: + 110 / 111; 6: code-layer rows; 7: + 45''',
      '''            o.insert("constRules", 8);  // 1: the lit buffer, canonical rows, constants 18 / 36 / 103 / 104, slot 68; 2: bits order; 3: + 63; 4: + 79-82; 5: + 110 / 111; 6: code-layer rows; 7: + 45; 8: + 94''')
patch(MC, '''        // Resolved before constant 45 was kept (static-model displacement, research 133 section 3): model materials with''',
      '''        // Resolved before constant 94 (the atlas of effect materials, research 138) was kept: effect sets resolve again once.
        if (!e.failed && o.value("constRules").toInt() < 8 && e.techset.contains("_effect_")) continue;
        // Resolved before constant 45 was kept (static-model displacement, research 133 section 3): model materials with''')

M = 'src/qtapp/main.cpp'
patch(M, '''    cam->setWindTime(0);
    cam->setEffectTime(0);''', '''    cam->setWindTime(0);
    cam->setEffectTime(0);
    // IW8R_BENCH_FX=<seconds>: View > Effect Preview on, the placed effects shown at that time of their run.
    if (const QString fxT = qEnvironmentVariable("IW8R_BENCH_FX"); !fxT.isEmpty()) {
        ctx.effectPreview = true;
        cam->setEffectTime(fxT.toDouble());
    }''')
print('ok')
