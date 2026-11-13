// IW8 Radiant Suite (Qt shell) - Direct3D 11 3D camera view in a native child window.
#pragma once

#include <algorithm>

#include <memory>

#include "stock_probes.h"
#include "stock_layered_material.h"
#include "game_lightmap.h"
#include "game_look.h"
#include "vfx_sim.h"
#include "atmosphere.h"
#include "foliage_sway.h"

#include <QBasicTimer>
#include <QElapsedTimer>
#include <QImage>
#include <QPoint>
#include <QWidget>
#include <functional>
#include <array>
#include <optional>
#include <set>
#include <unordered_map>

#include "collision_world.h"
#include "editor_context.h"
#include "player_move.h"
#include "render.h"
#include "scene.h"

struct IDXGISwapChain;

namespace iw8r::qt {

class CameraView : public QWidget {
    Q_OBJECT
public:
    CameraView(EditorContext* ctx, ID3D11Device* dev, ID3D11DeviceContext* dc, QWidget* parent = nullptr);
    ~CameraView() override;
    QPaintEngine* paintEngine() const override { return nullptr; }
    bool hasSwapChain() const { return swap_ != nullptr; }
    // Renders now and returns the number of non-background pixels (smoke test).
    size_t renderAndCount();
    QImage grabFrame();  // last rendered frame (D3D readback)
    // Benchmark and tests (--viewport-bench): one frame now without a readback; a fixed frame size instead of the
    // widget's (0 x 0 = the widget's again); wait for the GPU; the renderer's counters of the last frame.
    void renderNow() { render(); }
    void setFixedRenderSize(int w, int h) { fixedW_ = w, fixedH_ = h; }
    void finishGpu() { renderer_.finishGpu(); }
    const app::SceneRenderer::FrameStats& frameStats() const { return renderer_.lastStats(); }
    bool texturesPending() const { return moreToLoad_; }
    // Tests: the kept geometry against a fresh buildCameraGeometry of the same scene. "" when they agree, else what
    // differs. Call after a frame.
    QString geometrySelfCheck();
    // Off-screen render of the current camera at w x h (3.21 screenshots, 8.15 path frames, 1.17 loadscreen).
    // clean: game surfaces only (no outlines, gizmo, entity markers, tool faces or volumes); aspect > 0 renders that
    // aspect into the w x h image (stretched back by whoever shows it). Textures still loading are drawn as they are;
    // settleMs keeps rendering until they arrive. Null on failure (size over the D3D limit, no device).
    QImage renderImage(int w, int h, bool clean, double aspect = 0, int settleMs = 0);
    // Reflection probe capture (5.5): renders the scene from `pos` along the six world axes and resamples it into six
    // size x size RGBA faces in the converter's cube layout (D3D face order, world Z-up directions; x64-zt
    // SampleCubemapLinear). Empty on failure. The camera and selection are restored afterwards.
    std::vector<uint8_t> captureCube(const Vec3& pos, int size);
    // The surface point under a widget position (nullopt: nothing there) and the item hit.
    std::optional<Vec3> surfacePoint(const QPointF& p, SelRef* hit = nullptr) const;
    // Sky preview (3.8): the cached stock sky DDS for the worldspawn skyname ("" = none / not exported yet). The
    // textured view draws it as a box around the map, in the cube layout the build uses.
    void setSkySource(std::function<QString()> fn) { skySource_ = std::move(fn); }
    bool skyLoaded() const { return !skyTex_.empty(); }
    // Direction of gizmo axis `axis`: the world axis, or with local space the selected entity's rotated axis.
    Vec3 gizmoAxisDir(int axis) const;


    // Material colour images for the textured view. lookup: nullopt = not known yet (request() is called once),
    // "" = known to have no image (drawn with the missing-material checker), else a DDS path.
    using ImageLookup = std::function<std::optional<QString>(const std::string& material)>;
    using ImageRequest = std::function<void(const std::string& material)>;
    void setMaterialImages(ImageLookup lookup, ImageRequest request);
    // Layered stock materials (research 97): a material's per-layer constants; nullopt while the material is not
    // resolved, an empty StockLayeredMaterial (layers 0) when it has none. Layer images are looked up through
    // ImageLookup as "<material>\x01<L>" (colour) and "<material>\x02<L>" (alpha + reveal).
    using LayerLookup = std::function<std::optional<StockLayeredMaterial>(const std::string& material)>;
    void setMaterialLayers(LayerLookup lookup) { layerLookup_ = std::move(lookup); }
    // Stock material facts the reference needs: a decal's sort key (0 = not a decal) and the colour tint (RRGGBB, -1
    // none; applied when no tint mask limits it). nullopt while the material is not resolved.
    struct StockMaterialFacts {
        int decalSortKey = 0;
        int tint = -1;
        bool tintMasked = false;
        std::string tintMaskFile;  // slot 59 DDS: the tint applies by its red channel
        StockLayeredMaterial constants;  // layer 0 at least when the body has a pixel constant buffer
    };
    using FactsLookup = std::function<std::optional<StockMaterialFacts>(const std::string& material)>;
    void setMaterialFacts(FactsLookup lookup) { factsLookup_ = std::move(lookup); }
    // Cut-out mask of a stock material ("" = none): a stock colour image's alpha is specular (`_c&_s`), so without a
    // mask the camera draws it opaque; with one (`_sdf`, foliage `_alpha`) the mask is the alpha.
    using MaskLookup = std::function<QString(const std::string& material)>;
    void setMaterialMasks(MaskLookup lookup) { maskLookup_ = std::move(lookup); }
    // Normal map of a stock material ("" = none): the IW8 packed `_n&_g` image, used for bump and gloss shading (3.2).
    void setMaterialNormals(MaskLookup lookup) { normalLookup_ = std::move(lookup); }
    void setMaterialEmissive(MaskLookup lookup) { emissiveLookup_ = std::move(lookup); }
    // 3.16: folder of captured reflection probes (probe<N>.rgba six 64x64 faces + probe<N>.pos); the probe nearest the
    // camera is used for reflections on glossy surfaces. "" = none.
    void setProbeFolder(std::function<QString()> fn) { probeFolder_ = std::move(fn); }
    // 8.1: the particle simulation of an effect name (nullptr when it has no export); the camera samples it per frame.
    using EffectSource = std::function<std::shared_ptr<const VfxSimDef>(const std::string& fx)>;
    void setEffectSource(EffectSource fn) { effectSource_ = std::move(fn); }
    size_t effectParticlesDrawn() const { return fxParticles_; }  // tests
    void setEffectTime(double t) { fxTimeOverride_ = t; }        // tests: < 0 = live
    QString activeProbe() const { return activeProbe_; }  // tests
    size_t loadedNormalMaps() const;
    void materialImagesChanged();  // re-resolve materials that had no answer yet
    size_t loadedTextures() const { return renderer_.textureCount(); }
    size_t texturedTriangles() const;

    // Translate gizmo (iw3xo-style): three axis handles at the selection centre; drag one to move the selection
    // along that axis in grid steps (one undo step). Exposed for --gui-test.
    struct GizmoAxis {
        QPointF from, to;  // widget pixels
        bool visible = false;
    };
    bool gizmoActive() const;
    GizmoAxis gizmoAxisOnScreen(int axis) const;
    // R toggles move / rotate. Rotate rings: the screen point of `degrees` on the ring around `axis`.
    void toggleGizmoMode() {
        gizmoRotate_ = !gizmoRotate_;
        gizmoScale_ = false;
        update();
    }
    // Scale gizmo (T): drag an axis handle; brushes scale along it, models scale uniformly (modelscale).
    void toggleGizmoScale() {
        gizmoScale_ = !gizmoScale_;
        if (gizmoScale_) gizmoRotate_ = false;
        update();
    }
    bool gizmoScaleMode() const { return gizmoScale_; }
    double scaleDragFactor() const { return scaleFactor_; }
    bool gizmoRotateMode() const { return gizmoRotate_; }
    std::optional<QPointF> gizmoRingPoint(int axis, double degrees) const;

    // Play preview: walk the map as the player with collision (P enters, Esc leaves). Collision is built from the
    // document when play starts (and rebuilt when the document changes). tickPlay is driven by the main tick.
    void startPlay();
    void stopPlay();
    bool playing() const { return ctx_->playing; }
    void tickPlay(double dt);
    const PlayerState& player() const { return player_; }
    MoveParams& moveParams() { return params_; }
    const CollisionWorld& collisionWorld() const { return world_; }

    // Tool paths for game data Game Lighting reads from the install (game_look_assets.h); set by the main window.
    void setToolPathSource(std::function<QString(const QString&)> fn) { toolPathFn_ = std::move(fn); }
public slots:
    void requestRender() { update(); }
    // Perf tests: how often the whole camera geometry was made again, how often it was brought up to date at all,
    // how many items were written in place, and how often streams were put together again (items added or removed).
    size_t geometryBuilds() const { return geo_.fullBuilds(); }
    size_t geometryUpdates() const { return geometryUpdates_; }
    size_t itemsUpdatedInPlace() const { return geo_.itemsUpdatedInPlace(); }
    size_t geometryRelayouts() const { return geo_.relayouts(); }
    // Changes when something that casts a sun shadow moved, appeared or went: the shadow map is drawn again then.
    uint64_t shadowCasterVersion() const { return geo_.casterVersion(); }
    void invalidateGeometry() { geomDirty_ = true; filesDue_ = true; update(); }
    // View > Game Lighting: the last preview bake (null until one finished) and a forced re-bake.
    std::shared_ptr<const app::GameLightmap> gameLightmap() const { return gameLm_; }
    bool gameBaking() const { return gameBaking_; }
    void rebakeGameLighting() { gameBakeRev_ = ~0ull; gameSeenAt_.invalidate(); update(); }
    // The stock map shown as the reference ("" = none): Game Lighting then uses that map's sun and auto exposure
    // (core game_look stockMapLook).
    void setGameStockMap(const std::string& zone) {
        if (zone != gameStockMap_) gameBakeRev_ = ~0ull, stockSunKnown_ = false, stockFog_ = GameFog{}, stockSkyIntensity_ = 0, stockGrade_.reset(), gameLutReady_ = false, gameExposureSnap_ = true;  // the bake follows the sun
        if (zone != gameStockMap_) {
            // Layered world materials are numbered per map (tw4/..., twc4/...: MaterialCache::mapLocalName): another
            // map's textures under the same names are not this map's.
            const auto local = [](const std::string& m) { return m.rfind("tw4/", 0) == 0 || m.rfind("twc4/", 0) == 0; };
            for (auto it = texIds_.begin(); it != texIds_.end();) it = local(it->first) ? texIds_.erase(it) : std::next(it);
            for (auto it = requested_.begin(); it != requested_.end();) it = local(*it) ? requested_.erase(it) : std::next(it);
        }
        gameStockMap_ = zone;
        update();
    }
    // The stock map's atmosphere (its vision text; core gameFogFromVision).
    // The sky preview: conversion exposure and the HDR source mean / 99.5th percentile luminance (diagnosis).
    std::array<float, 3> skyLuminance() const { return {skyExposure_, skyMeanLum_, skyHighLum_}; }
    std::array<size_t, 4> referenceLayerStats() const { return renderer_.referenceLayerStats(); }
    // Game Lighting's exposure now, its target, and whether the frame is metered (benchmark report).
    std::array<double, 3> gameExposureState() const { return {gameExposure_, gameExposureTarget_, gameMetering_ ? 1.0 : 0.0}; }
    // The stock map's grading CLUT (its vision's): baked into the colour LUT.
    void setGameStockGrading(std::shared_ptr<const GameGradingClut> grade) {
        stockGrade_ = std::move(grade);
        gameLutReady_ = false;
        update();
    }
    // The stock map's bakedLightScale (StockWorld): scales its lightmap and the light derived from it.
    void setGameStockBakedScale(double s) {
        stockBakedScale_ = s > 0 ? s : 1.0;
        update();
    }
    // The stock sky material's intensity (its first pixel constant): the sky image times this is the drawn sky.
    void setGameStockSkyIntensity(double intensity) {
        stockSkyIntensity_ = intensity;
        update();
    }
    void setGameStockVision(const std::string& visionText) {
        stockFog_ = gameFogFromVision(visionText);
        update();
    }
    // The stock map's own sun (its ComWorld sun light: intensity, linear colour, direction toward the sun).
    void setGameStockSun(double intensity, const Vec3& color, const Vec3& toward) {
        stockSunKnown_ = intensity > 0;
        stockSunIntensity_ = intensity, stockSunColor_ = color, stockSunToward_ = toward;
        gameBakeRev_ = ~0ull;
        update();
    }
    // The stock reference's own lightmap (null clears it), sent to the renderer on the next frame.
    void setStockLightmap(std::shared_ptr<const app::StockLightmapLayers> layers) {
        pendingStockLm_ = std::move(layers);
        stockLmDirty_ = true;
        update();
    }
    bool hasStockLightmap() const { return renderer_.hasStockLightmap(); }
    bool hasEnvBrdfLut() const { return renderer_.hasEnvBrdfLut(); }
    // The stock reference's reflection probes (null clears), sent to the renderer on the next frame.
    void setStockProbes(std::shared_ptr<const StockProbeSet> set) {
        pendingProbes_ = std::move(set);
        probesDirty_ = true;
        update();
    }
    size_t stockProbeCount() const { return renderer_.stockProbeCount(); }
    double gameExposure() const { return gameExposure_; }
    // Stock map reference geometry (View > Stock Map Reference), uploaded on the next frame. Empty clears it.
    void setReference(std::vector<app::GpuVertex> tris) {
        pendingRef_ = std::move(tris);
        refDirty_ = true;
        update();
    }
    // 3.20: frustum culling of the stock reference's spatial chunks (on by default).
    void setReferenceCulling(bool on) { renderer_.setReferenceCulling(on); update(); }
    bool referenceCulling() const { return renderer_.referenceCulling(); }
    size_t referenceChunks() const { return renderer_.referenceChunks(); }
    size_t lastCulledReferenceChunks() const { return renderer_.lastCulledReferenceChunks(); }
    size_t lastCulledSceneChunks() const { return renderer_.lastCulledSceneChunks(); }
    // 3.20 texture memory budget (MB, setting view/textureBudgetMB): past it, new textures load at 128 px.
    void setTextureBudgetMB(int mb) { textureBudget_ = size_t(std::max(1, mb)) * 1024 * 1024; }
    size_t textureBudgetBytes() const { return textureBudget_; }
    size_t textureBytes() const { return renderer_.textureBytes(); }
    int textureSideNow() const;
    int texturesReducedByBudget() const { return budgetReduced_; }
    // 3.20: reference props as instanced meshes, uploaded on the next frame.
    void setReferenceInstances(std::vector<app::InstancedMesh> meshes) {
        pendingInst_ = std::move(meshes);
        instDirty_ = true;
        update();
    }
    // Instanced prop draws: the plain shaded meshes plus the textured ones (one per model material).
    size_t referenceInstances() const {
        return (instDirty_ ? 0 : renderer_.referenceInstances()) + (texInstDirty_ ? 0 : renderer_.referenceTexInstances());
    }
    // Reference props with their textures: one mesh per model material (`materials` names each mesh's material).
    void setReferenceTexInstances(std::vector<app::InstancedTexMesh> meshes, std::vector<std::string> materials) {
        pendingTexInst_ = std::move(meshes);
        texInstMaterials_ = std::move(materials);
        texInstDirty_ = true;
        update();
    }
    size_t referenceTexInstanceMeshes() const { return texInstMaterials_.size(); }
    size_t referenceTexInstancesResolved() const {
        size_t n = 0;
        for (bool b : texInstResolved_) n += b ? 1 : 0;
        return n;
    }
    size_t referenceTriangles() const { return refDirty_ ? pendingRef_.size() / 3 : renderer_.referenceVertexCount() / 3; }
    // Textured stock reference (3.19): batches per stock material; textures load over the next frames.
    void setReferenceTextured(std::vector<app::ReferenceTexBatch> batches) {
        pendingRefTex_ = std::move(batches);
        refTexDirty_ = true;
        update();
    }
    size_t referenceTexturesResolved() const;

protected:
    void timerEvent(QTimerEvent* e) override;
    void paintEvent(QPaintEvent*) override;
    void resizeEvent(QResizeEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*) override;
    void keyPressEvent(QKeyEvent*) override;
    void keyReleaseEvent(QKeyEvent*) override;
    void focusOutEvent(QFocusEvent*) override;

    void dragEnterEvent(QDragEnterEvent*) override;
    void dragMoveEvent(QDragMoveEvent*) override;
    void dropEvent(QDropEvent*) override;
    void dragLeaveEvent(QDragLeaveEvent*) override;

signals:
    // A dragged asset is over widget position `pos` (material drags preview on the surface under it), or left the view.
    void assetHovered(const QString& kind, const QString& name, const QPointF& pos);
    void assetHoverEnded();
    // An asset dragged from a browser was dropped at widget position `pos` (4.24).
    void assetDropped(const QString& kind, const QString& name, bool loop, const QPointF& pos);
    // Right click without moving (3.7): the main window shows the camera context menu.
    void contextMenuAt(const QPoint& globalPos, const QPointF& widgetPos);

private:
    QBasicTimer windTimer_;  // repaints while the wind preview runs
    QElapsedTimer windClock_;
    double windTimeOverride_ = -1;  // smoke tests: a fixed wind time in seconds
public:
    void setCustomTintSource(std::function<std::optional<std::array<double, 3>>(const std::string& material)> fn) {
        customTint_ = std::move(fn);
    }
    void setWindTime(double seconds) { windTimeOverride_ = seconds; }
    // The image file the camera would load for a material ("" when unknown or not cached yet). Tests.
    QString materialImagePath(const std::string& material) const {
        const auto p = imageLookup_ ? imageLookup_(material) : std::nullopt;
        return p ? *p : QString();
    }
private:
    void render();
    void ensureSwapChain();

    EditorContext* ctx_;
    ID3D11Device* dev_;
    ID3D11DeviceContext* dc_;
    app::SceneRenderer renderer_;
    size_t textureBudget_ = size_t(1024) * 1024 * 1024;
    int budgetReduced_ = 0;
    bool rendererOk_ = false;
    const char* playMode_ = nullptr;
    std::vector<app::GpuVertex> pendingRef_;
    std::vector<app::InstancedTexMesh> pendingTexInst_;
    std::vector<std::string> texInstMaterials_;
    std::vector<bool> texInstResolved_;
    bool texInstDirty_ = false;
    std::vector<app::InstancedMesh> pendingInst_;
    bool instDirty_ = false;
    std::vector<app::ReferenceTexBatch> pendingRefTex_;
    std::vector<std::string> refTexMaterials_;
    std::vector<int> refTexLayer_;  // parallel: 0 base, L >= 1 a layer batch of refTexMaterials_[k]
    LayerLookup layerLookup_;
    FactsLookup factsLookup_;
    bool resolveReferenceDecal(size_t k, int textureId);  // true when settled
    bool resolveReferenceLayer(size_t k);  // true when settled (drawn or left out)
    std::vector<bool> refTexResolved_;
    bool refTexDirty_ = false;
    // Reference materials waiting for an image: asked again on events and twice a second (render()).
    bool refTexPending_ = false, texInstPending_ = false, refPollDue_ = false, refPollTimerArmed_ = false;
    QElapsedTimer refPoll_;
    uint64_t geomKey_ = 0;
    bool geomDirty_ = true;
    int captureW_ = 0, captureH_ = 0;  // renderImage: target size instead of the widget's
    int fixedW_ = 0, fixedH_ = 0;      // setFixedRenderSize
    double captureAspect_ = 0;
    bool cleanCapture_ = false;
    size_t geometryUpdates_ = 0;
    app::CameraGeometry geo_;                           // the scene's geometry, kept between frames
    app::GeoStream<app::GpuVertex> gizmoLines_;         // the gizmo, on top of everything
    std::vector<std::unique_ptr<app::SceneBatch>> skyBatches_;  // the six sky box faces
    app::SceneBatch fxBatch_;                           // effect particles of this frame
    std::vector<app::SceneBatch*> extraBatches_;
    app::CameraGeometry::TextureFn texFn_;
    bool refDirty_ = false;
    IDXGISwapChain* swap_ = nullptr;
    int swapW_ = 0, swapH_ = 0;
    void* swapHwnd_ = nullptr;  // recreate the swap chain when docking re-parents the native window
    QPoint pressPos_, lockPos_;
    bool lmbDown_ = false;
    CollisionWorld world_;
    PlayerState player_;
    MoveParams params_;
    uint64_t worldRevision_ = 0;
    int textureFor(const std::string& material);
    Vec3 gizmoPivot() const;
    double gizmoLength() const;
    double axisParamForMouse(const QPointF& p, int axis) const;  // closest point on the drag axis to the mouse ray
    void addGizmoLines();
    int gizmoDragAxis_ = -1;
    bool gizmoRotate_ = false;
    bool gizmoScale_ = false;
    double lookTravel_ = 0;  // mouse travel while the right button is down: a still click opens the context menu
    double scaleFactor_ = 1.0;  // current factor while dragging a scale handle
    int ringDragAxis_ = -1;
    double ringStartAngle_ = 0, ringDeltaDeg_ = 0;
    Vec3 ringPivot_;
    std::optional<double> ringAngleForMouse(const QPointF& p, int axis) const;
    std::optional<QPointF> projectToWidget(const Vec3& p) const;
    double gizmoStartT_ = 0;
    Vec3 gizmoStartPivot_;
    ImageLookup imageLookup_;
    // 12.6: a custom material's tint (r, g, b multipliers; nullopt = none), so the camera shows what the build makes.
    std::function<std::optional<std::array<double, 3>>(const std::string& material)> customTint_;
    std::function<QString()> skySource_;
    QString skyPath_;
    float skyExposure_ = 0, skyMeanLum_ = 0, skyHighLum_ = 0;  // the 8-bit sky preview = HDR sky x this (sky_image.h auto exposure); 0 = LDR source
    std::vector<int> skyTex_;  // six face textures (D3D order)
    void updateSkyBox();
    MaskLookup maskLookup_;
    MaskLookup normalLookup_;
    std::unordered_map<std::string, int> nrmIds_;  // >= 0 texture, -1 none; unresolved materials are not stored
    int normalFor(const std::string& material);
    MaskLookup emissiveLookup_;
    std::unordered_map<std::string, int> emiIds_;
    // Worldspawn settings parsed per document revision, and the files looked at now and then (render()).
    uint64_t settingsRev_ = 0;
    std::string settingsPath_;
    Atmosphere atmo_;
    WindSettings wind_;
    std::string lutPath_, lutLoadedPath_;  // absolute path of the map's grade LUT, and the one loaded
    qint64 lutMtime_ = 0, lutLoadedMtime_ = 0;
    bool lutLoadedOn_ = false;
    bool filesDue_ = true, pollFiles_ = true;
    QElapsedTimer frameGap_, filePoll_;
    struct ProbeFile {
        Vec3 pos;
        QString rgba;
        qint64 mtime = 0;
    };
    std::vector<ProbeFile> probes_;
    QString activeProbePath_;
    qint64 activeProbeMtime_ = 0;
    void scanProbes();
    std::vector<app::SceneRenderer::PixelLight> pixelLightScratch_;
    // View > Game Lighting (game_lightmap.h): the preview bake, made again in the background once edits settle.
    std::shared_ptr<const app::GameLightmap> gameLm_;
    bool gameBaking_ = false, gameLutReady_ = false, gameWasOn_ = false, gameMetering_ = false;
    std::string gameStockMap_;
    bool stockSunKnown_ = false;
    GameFog stockFog_;
    double stockSkyIntensity_ = 0;
    double stockBakedScale_ = 1.0;
    std::shared_ptr<const GameGradingClut> stockGrade_;
    bool gameExposureSnap_ = true;  // the first metered exposure after a map opens is taken at once
    double stockSunIntensity_ = 0;
    Vec3 stockSunColor_{1, 1, 1}, stockSunToward_{0, 0, 1};
    // The stock map's look for Game Lighting: its own entry, else the shipped map with the nearest sun.
    const StockMapLook* gameStockLook() const;
    Vec3 gameSunToward() const;
    std::function<QString(const QString&)> toolPathFn_;
    bool envBrdfAsked_ = false;
    std::shared_ptr<std::vector<uint8_t>> pendingEnvBrdf_;
    int pendingEnvBrdfW_ = 0, pendingEnvBrdfH_ = 0;
    std::shared_ptr<const app::StockLightmapLayers> pendingStockLm_;
    std::array<float, 3> stockMeanLight_{0, 0, 0};
    bool stockLmDirty_ = false;
    std::shared_ptr<const StockProbeSet> pendingProbes_;
    bool probesDirty_ = false;
    double gameExposure_ = 0, gameExposureTarget_ = 0;
    QElapsedTimer gameExposureClock_;
    uint64_t gameBakeRev_ = ~0ull, gameSeenRev_ = ~0ull;
    QElapsedTimer gameSeenAt_;
    bool gameLookOn() const;
    void prepareGameLook();
    void maybeBakeGameLook();
    void gameBakeDone(std::shared_ptr<const app::GameLightmap> lm);
    EffectSource effectSource_;
    size_t fxParticles_ = 0;
    double fxTimeOverride_ = -1;
    int whiteTex_ = -1;
    QElapsedTimer fxClock_;
    void appendEffectParticles();
    std::function<QString()> probeFolder_;
    QString activeProbe_;  // probe file in use (path | mtime)
    void updateReflectionProbe();
    int emissiveFor(const std::string& material);
    ImageRequest imageRequest_;
    std::unordered_map<std::string, std::pair<int, int>> texSizes_;  // decoded colour size before the 512 cap (Density mode)
    std::unordered_map<std::string, int> texIds_;  // resolved: >= 0 texture, -1 flat colour
    std::set<std::string> requested_;
    int checkerTex_ = -1;
    int loadsThisFrame_ = 0;
    bool moreToLoad_ = false;
};

}  // namespace iw8r::qt
