import io


def patch(p, pairs):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    for a, b in pairs:
        a = a.replace('\n', nl); b = b.replace('\n', nl)
        assert s.count(a) == 1, (p, s.count(a), a[:80])
        s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


patch('src/qtapp/camera_view.h', [
 ('''    void invalidateGeometry() { geomDirty_ = true; filesDue_ = true; update(); }''',
  '''    void invalidateGeometry() { geomDirty_ = true; filesDue_ = true; update(); }
    // View > Game Lighting: the last preview bake (null until one finished) and a forced re-bake.
    std::shared_ptr<const app::GameLightmap> gameLightmap() const { return gameLm_; }
    bool gameBaking() const { return gameBaking_; }
    void rebakeGameLighting() { gameBakeRev_ = ~0ull; gameSeenAt_.invalidate(); update(); }'''),
 ('''    std::vector<app::SceneRenderer::PixelLight> pixelLightScratch_;''',
  '''    std::vector<app::SceneRenderer::PixelLight> pixelLightScratch_;
    // View > Game Lighting (game_lightmap.h): the preview bake, made again in the background once edits settle.
    std::shared_ptr<const app::GameLightmap> gameLm_;
    bool gameBaking_ = false, gameLutReady_ = false, gameWasOn_ = false;
    uint64_t gameBakeRev_ = ~0ull, gameSeenRev_ = ~0ull;
    QElapsedTimer gameSeenAt_;
    bool gameLookOn() const;
    void prepareGameLook();
    void maybeBakeGameLook();
    void gameBakeDone(std::shared_ptr<const app::GameLightmap> lm);'''),
])
s = io.open('src/qtapp/camera_view.h', encoding='utf-8').read()
if '#include "game_lightmap.h"' not in s:
    i = s.index('#include "')
    s = s[:i] + '#include "game_lightmap.h"\n' + s[i:]
    io.open('src/qtapp/camera_view.h', 'w', encoding='utf-8', newline='').write(s)

patch('src/qtapp/camera_view.cpp', [
 ('''        ctx_->scene.pixelLights = ctx_->scene.litPreview && ctx_->texturedView && !ctx_->scene.captureMode;
        if (ctx_->scene.pixelLights)
            for (const auto& L : ctx_->scene.lights) {''', '''        prepareGameLook();
        const bool game = gameLookOn();
        ctx_->scene.pixelLights = (ctx_->scene.litPreview || game) && ctx_->texturedView && !ctx_->scene.captureMode;
        if (ctx_->scene.pixelLights)
            for (const auto& L : ctx_->scene.lights) {'''),
 ('''                p.dir[0] = float(L.dir.x), p.dir[1] = float(L.dir.y), p.dir[2] = float(L.dir.z);
                pl.push_back(p);''', '''                p.dir[0] = float(L.dir.x), p.dir[1] = float(L.dir.y), p.dir[2] = float(L.dir.z);
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
                pl.push_back(p);'''),
 ('''        ctx_->scene.gpuShadows = enc && ctx_->scene.litShadows;''',
  '''        ctx_->scene.gpuShadows = (enc || game) && ctx_->scene.litShadows;'''),
 ('''        geomKey_ = key;
        geomDirty_ = moreToLoad_;  // textures still arriving: build again next frame
        ++geometryUpdates_;
    }''', '''        geomKey_ = key;
        geomDirty_ = moreToLoad_;  // textures still arriving: build again next frame
        ++geometryUpdates_;
    }
    maybeBakeGameLook();'''),
])

patch('src/qtapp/camera_view.cpp', [
 ('''#include "texture_decode.h"''', '''#include "texture_decode.h"
#include "game_look.h"
#include <QPointer>
#include <thread>'''),
])

# Implementation appended at the end of camera_view.cpp (inside the file's namespace if any).
s = io.open('src/qtapp/camera_view.cpp', encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'
impl = '''
// ---- View > Game Lighting (game_lightmap.h, core game_look; research 95 "Editor maps in game")

bool CameraView::gameLookOn() const { return ctx_->scene.gameLook && ctx_->texturedView && !ctx_->scene.captureMode; }

namespace {
// The worldspawn's "r g b intensity" sky for the bake (iw8r_bake_sky), default a pale blue at 0.35 of the sun.
iw8r::Vec3 gameSkyOf(const iw8r::Document& doc) {
    iw8r::Vec3 sky{0.55 * 0.35, 0.65 * 0.35, 0.8 * 0.35};
    for (const auto& e : doc.map().entities)
        if (e.isWorldspawn()) {
            if (const std::string* s = e.value("iw8r_bake_sky")) {
                double v[4];
                if (std::sscanf(s->c_str(), "%lf %lf %lf %lf", &v[0], &v[1], &v[2], &v[3]) == 4) sky = iw8r::Vec3{v[0] * v[3], v[1] * v[3], v[2] * v[3]};
            }
            break;
        }
    return sky;
}
}  // namespace

void CameraView::prepareGameLook() {
    const bool game = gameLookOn();
    if (game != gameWasOn_) {
        gameWasOn_ = game;
        geomDirty_ = true;  // vertex colours and lightmap UVs change
    }
    ctx_->scene.gameLightmap = game ? gameLm_ : nullptr;
    app::SceneRenderer::GameLook g;
    g.on = game;
    if (game) {
        if (!gameLutReady_) {
            const std::vector<uint8_t> lut = buildGameTonemapLut(64);
            renderer_.setGameTonemapLut(64, lut.data());
            gameLutReady_ = true;
        }
        const Vec3& t = ctx_->scene.sunToward;
        const Vec3& c = ctx_->scene.sunColor;
        g.sunToward[0] = float(t.x), g.sunToward[1] = float(t.y), g.sunToward[2] = float(t.z);
        g.sunColor[0] = float(c.x), g.sunColor[1] = float(c.y), g.sunColor[2] = float(c.z);
        const Vec3 sky = gameSkyOf(ctx_->doc);
        g.sky[0] = float(sky.x), g.sky[1] = float(sky.y), g.sky[2] = float(sky.z);
        g.lutScale = float(gameLutInputScale(10.0));
    }
    renderer_.setGameLook(g);
}

void CameraView::maybeBakeGameLook() {
    if (!gameLookOn() || gameBaking_) return;
    const uint64_t rev = ctx_->doc.revision() * 1000003ull ^ ctx_->scene.generation();
    if (rev == gameBakeRev_) return;
    if (rev != gameSeenRev_) {
        gameSeenRev_ = rev;
        gameSeenAt_.restart();
    }
    // Wait until edits settle (a drag changes the revision every frame); the first bake starts at once.
    if (gameLm_ && gameSeenAt_.isValid() && gameSeenAt_.elapsed() < 600) {
        QTimer::singleShot(650, this, [this] { update(); });
        return;
    }
    app::GameBakeInput in = app::gatherGameBake(ctx_->scene, ctx_->scene.drawClassFor);
    // The game draws the sun (suncolor) and the lights in real time; the bake holds the sky, their bounce and the sun
    // visibility, as the build's bake (compile_iw8.cpp) does.
    in.scene.sunDir = ctx_->scene.sunToward;
    in.scene.sunColor = ctx_->scene.sunColor;
    in.scene.sunIntensity = 1.0;
    in.scene.sunDirect = false;
    const Vec3 sky = gameSkyOf(ctx_->doc);
    in.scene.skyColor = sky;
    in.scene.skyIntensity = 1.0;
    for (const auto& L : ctx_->scene.lights) {
        BakeLight b;
        b.pos = L.pos;
        b.color = L.color;
        b.radius = L.radius;
        b.spot = L.cosOuter > -1.5;
        b.dir = L.dir;
        b.cosOuter = L.cosOuter, b.cosInner = L.cosInner;
        b.direct = in.scene.lights.size() >= 254;  // past the real-time limit: baked whole
        in.scene.lights.push_back(b);
    }
    for (const auto& e : ctx_->doc.map().entities)
        if (e.isWorldspawn()) {
            if (const std::string* ts = e.value("iw8r_lightmap_texel")) in.settings.texelSize = std::max(1.0, std::atof(ts->c_str()));
            if (const std::string* rs = e.value("iw8r_bake_rays")) in.settings.skyRays = std::clamp(std::atoi(rs->c_str()), 1, 1024);
            break;
        }
    gameBaking_ = true;
    gameBakeRev_ = rev;
    emit ctx_->statusMessage(QString("Game lighting: baking %1 faces...").arg(in.polys.size()));
    QPointer<CameraView> self(this);
    std::thread([self, in = std::move(in)]() {
        std::shared_ptr<const app::GameLightmap> lm = app::bakeGameLightmap(in);
        QMetaObject::invokeMethod(qApp, [self, lm]() {
            if (self) self->gameBakeDone(lm);
        }, Qt::QueuedConnection);
    }).detach();
}

void CameraView::gameBakeDone(std::shared_ptr<const app::GameLightmap> lm) {
    gameBaking_ = false;
    if (!lm) return;
    if (!lm->error.empty()) {
        emit ctx_->statusMessage(QString("Game lighting: %1").arg(QString::fromStdString(lm->error)));
    } else {
        renderer_.setGameLightmap(lm->width, lm->height, lm->a.data(), lm->b.data(), lm->d.data());
        emit ctx_->statusMessage(QString("Game lighting: baked %1 x %2 lightmap, %3 faces, %4 units per texel, %5 s")
                                     .arg(lm->width).arg(lm->height).arg(lm->polys).arg(lm->texelSize).arg(lm->seconds, 0, 'f', 1));
    }
    gameLm_ = lm;
    geomDirty_ = true;
    update();
}
'''
s = s.rstrip() + nl + impl.replace('\n', nl)
io.open('src/qtapp/camera_view.cpp', 'w', encoding='utf-8', newline='').write(s)

patch('src/qtapp/main_window.cpp', [
 ('''    litToggle->setCheckable(true);
    addCommand(view, "StockReference",''', '''    litToggle->setCheckable(true);
    QAction* gameToggle = addCommand(view, "ToggleGameLighting", "Game Lighting (as the built map looks in game)", QKeySequence("Ctrl+Alt+L"), [this] {
        // research 95 "Editor maps in game": the converter's world shader, the editor's lightmap bake (background,
        // again after edits), the game's sun / lights / exposure / tone map.
        ctx_.scene.gameLook = !ctx_.scene.gameLook;
        if (QAction* a = action("ToggleGameLighting")) a->setChecked(ctx_.scene.gameLook);
        ctx_.cameraMoved();
    });
    gameToggle->setCheckable(true);
    addCommand(view, "StockReference",'''),
])
print('ok')
